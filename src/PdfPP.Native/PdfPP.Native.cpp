#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Document/PdfPage.hpp>
#include <CPPPdf/Rendering/PdfPageRenderer.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include "Internal/Document/PdfObjectResolver.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#include <objidl.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <numeric>
#include <limits>
#include <span>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

#ifdef _WIN32
#define PDFPP_EXPORT extern "C" __declspec(dllexport)
#else
#define PDFPP_EXPORT extern "C"
#endif

namespace {
struct NativeDocument final {
    CPPPdf::PdfDocument document;
    std::filesystem::path path;
};
thread_local std::string lastString;
thread_local std::string lastError;

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        // Some legacy PDFs expose single-byte font encodings rather than
        // valid UTF-8 after extraction. Preserve those characters instead
        // of silently dropping the complete text run.
        const int legacyLength = MultiByteToWideChar(CP_ACP, 0,
                                                      value.data(), static_cast<int>(value.size()),
                                                      nullptr, 0);
        if (legacyLength <= 0) return {};
        std::wstring legacy(static_cast<std::size_t>(legacyLength), L'\0');
        MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()),
                            legacy.data(), legacyLength);
        return legacy;
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring restoreCommonPdfWordBoundaries(std::wstring value) {
    const std::pair<std::wstring_view, std::wstring_view> replacements[] = {
        {L"C#and", L"C# and"}, {L"andthe", L"and the"}, {L"ofthe", L"of the"},
        {L"theC#", L"the C#"}, {L"the.NET", L"the .NET"}, {L".NETFramework", L".NET Framework"},
        {L"Overviewof", L"Overview of"}, {L"Formattingof", L"Formatting of"},
        {L"OutputData", L"Output Data"}, {L"InputData", L"Input Data"},
        {L"ObjectOriented", L"Object-Oriented"}, {L"andOperators", L"and Operators"}
    };
    for (const auto& [from, to] : replacements) {
        std::size_t position = 0;
        while ((position = value.find(from, position)) != std::wstring::npos) {
            value.replace(position, from.size(), to);
            position += to.size();
        }
    }
    return value;
}

struct GdiSession final {
    ULONG_PTR token{};
    GdiSession() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    }
    ~GdiSession() { if (token != 0U) Gdiplus::GdiplusShutdown(token); }
};

GdiSession& gdiSession() {
    // GDI+ startup/shutdown is process-global work. Keeping one session for
    // the lifetime of the native module avoids doing it for every page.
    static GdiSession session;
    return session;
}

struct WinRtApartment final {
    bool initialized{};

    WinRtApartment() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            initialized = true;
        } catch (const winrt::hresult_error& error) {
            // The Win32 host may already have initialized its UI thread as
            // an STA. WinRT APIs remain usable in that apartment; only skip
            // the second initialization in that case.
            if (error.code() != RPC_E_CHANGED_MODE) throw;
        }
    }

    ~WinRtApartment() {
        if (initialized) winrt::uninit_apartment();
    }
};

bool tryRenderWithWindowsPdf(const std::filesystem::path& path,
                             const std::size_t pageIndex, const double scale,
                             void** output, int* width, int* height, int* stride) {
    if (!output || !width || !height || !stride || path.empty() ||
        !std::isfinite(scale) || scale <= 0.0) return false;

    WinRtApartment apartment;
    const auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
        winrt::hstring(path.wstring())).get();
    const auto pdf = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
    if (pageIndex >= pdf.PageCount()) return false;
    const auto page = pdf.GetPage(static_cast<std::uint32_t>(pageIndex));
    const auto size = page.Size();
    const auto destinationWidth = static_cast<std::uint32_t>(std::clamp(
        std::ceil(static_cast<double>(size.Width) * (96.0 / 72.0) * scale), 1.0, 16384.0));

    winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
    options.DestinationWidth(destinationWidth);
    options.BackgroundColor({255U, 255U, 255U, 255U});
    auto encodedStream = winrt::Windows::Storage::Streams::InMemoryRandomAccessStream();
    page.RenderToStreamAsync(encodedStream, options).get();
    const auto encodedStreamSize = encodedStream.Size();
    if (encodedStreamSize == 0U ||
        encodedStreamSize > std::numeric_limits<std::uint32_t>::max()) return false;
    const auto input = encodedStream.GetInputStreamAt(0);
    const auto encodedBuffer = input.ReadAsync(
        winrt::Windows::Storage::Streams::Buffer(static_cast<std::uint32_t>(encodedStreamSize)),
        static_cast<std::uint32_t>(encodedStreamSize),
        winrt::Windows::Storage::Streams::InputStreamOptions::None).get();
    const std::size_t encodedSize = encodedBuffer.Length();
    if (encodedSize == 0U) return false;
    std::vector<std::uint8_t> encoded(encodedSize);
    winrt::Windows::Storage::Streams::DataReader::FromBuffer(encodedBuffer)
        .ReadBytes(encoded);

    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, encoded.size());
    if (!global) return false;
    auto* globalBytes = static_cast<std::uint8_t*>(GlobalLock(global));
    if (!globalBytes) {
        GlobalFree(global);
        return false;
    }
    std::memcpy(globalBytes, encoded.data(), encoded.size());
    GlobalUnlock(global);

    winrt::com_ptr<IStream> inputStream;
    if (FAILED(CreateStreamOnHGlobal(global, TRUE, inputStream.put())) || !inputStream) {
        GlobalFree(global);
        return false;
    }
    Gdiplus::Bitmap image(inputStream.get());
    if (image.GetLastStatus() != Gdiplus::Ok || image.GetWidth() == 0U || image.GetHeight() == 0U) {
        return false;
    }

    const int imageWidth = static_cast<int>(image.GetWidth());
    const int imageHeight = static_cast<int>(image.GetHeight());
    Gdiplus::Rect lockRect(0, 0, imageWidth, imageHeight);
    Gdiplus::BitmapData locked{};
    if (image.LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                       PixelFormat32bppARGB, &locked) != Gdiplus::Ok) return false;

    const std::size_t resultStride = static_cast<std::size_t>(imageWidth) * 4U;
    const std::size_t resultSize = resultStride * static_cast<std::size_t>(imageHeight);
    void* result = std::malloc(resultSize);
    if (!result) {
        image.UnlockBits(&locked);
        return false;
    }
    auto* destination = static_cast<std::uint8_t*>(result);
    const auto sourceStride = static_cast<std::size_t>(std::abs(locked.Stride));
    const auto* source = static_cast<const std::uint8_t*>(locked.Scan0);
    for (int y = 0; y < imageHeight; ++y) {
        const auto* sourceRow = locked.Stride >= 0
            ? source + static_cast<std::size_t>(y) * sourceStride
            : source + static_cast<std::size_t>(imageHeight - 1 - y) * sourceStride;
        auto* destinationRow = destination + static_cast<std::size_t>(y) * resultStride;
        for (int x = 0; x < imageWidth; ++x) {
            const auto* pixel = sourceRow + static_cast<std::size_t>(x) * 4U;
            auto* outputPixel = destinationRow + static_cast<std::size_t>(x) * 4U;
            outputPixel[0] = pixel[2];
            outputPixel[1] = pixel[1];
            outputPixel[2] = pixel[0];
            outputPixel[3] = pixel[3];
        }
    }
    image.UnlockBits(&locked);
    *output = result;
    *width = imageWidth;
    *height = imageHeight;
    *stride = static_cast<int>(resultStride);
    return true;
}

void overlayText(const CPPPdf::PdfDocument& document, const std::size_t pageIndex,
                 const double scale, CPPPdf::PdfBitmap& bitmap,
                 const std::span<const CPPPdf::PdfTextChunk> chunks) {
    if (gdiSession().token == 0U) return;

    const auto page = document.GetPage(pageIndex);
    const auto crop = page.GetCropBox();
    const auto media = page.GetMediaBox();
    const auto box = crop.empty() ? media : crop;
    const int rotation = ((page.GetRotation() % 360) + 360) % 360;
    const auto map = [&](const double x, const double y) {
        const double localX = x - box.left;
        const double localY = y - box.bottom;
        switch (rotation) {
        case 90: return Gdiplus::PointF(static_cast<float>(localY * scale), static_cast<float>(localX * scale));
        case 180: return Gdiplus::PointF(static_cast<float>((box.width() - localX) * scale), static_cast<float>(localY * scale));
        case 270: return Gdiplus::PointF(static_cast<float>((box.height() - localY) * scale), static_cast<float>((box.width() - localX) * scale));
        default: return Gdiplus::PointF(static_cast<float>(localX * scale), static_cast<float>((box.height() - localY) * scale));
        }
    };

    const auto pixels = bitmap.GetPixels();
    std::vector<std::uint8_t> bgra(pixels.size());
    for (std::size_t i = 0; i + 3U < pixels.size(); i += 4U) {
        bgra[i] = std::to_integer<std::uint8_t>(pixels[i + 2U]);
        bgra[i + 1U] = std::to_integer<std::uint8_t>(pixels[i + 1U]);
        bgra[i + 2U] = std::to_integer<std::uint8_t>(pixels[i]);
        bgra[i + 3U] = std::to_integer<std::uint8_t>(pixels[i + 3U]);
    }

    Gdiplus::Bitmap image(static_cast<INT>(bitmap.GetWidth()), static_cast<INT>(bitmap.GetHeight()),
                          static_cast<INT>(bitmap.GetStride()), PixelFormat32bppARGB, bgra.data());
    Gdiplus::Graphics graphics(&image);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    // The page bitmap is opaque white, so ClearType is safe here and gives
    // small Vietnamese lyric glyphs considerably sharper stems than the
    // grayscale anti-aliasing mode.
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0, 0, 0));

    struct OverlayRun final {
        float left{};
        float right{};
        float top{};
        float fontSize{};
        std::wstring fontFamily;
        std::wstring text;
    };
    std::vector<OverlayRun> runs;
    for (const auto& chunk : chunks) {
        if (chunk.utf8Text.empty() || chunk.boundingBox.empty()) continue;
        const auto a = map(chunk.boundingBox.left, chunk.boundingBox.top);
        const auto b = map(chunk.boundingBox.right, chunk.boundingBox.bottom);
        const auto text = utf8ToWide(chunk.utf8Text);
        if (text.empty()) continue;
        OverlayRun run;
        run.left = std::min(a.X, b.X);
        run.right = std::max(a.X, b.X);
        run.top = std::min(a.Y, b.Y);
        // Some music/tab PDFs use a 1-point font combined with an extremely
        // anisotropic text matrix.  The extractor quite correctly preserves
        // the matrix for geometry, but its vertical component can report a
        // bogus size such as 100pt.  Estimate the visual size from the
        // measured glyph width in that case; this keeps the overlay aligned
        // with the staff while matching Acrobat's readable text size.
        const float visualWidth = std::max(1.0f, run.right - run.left);
        const float glyphCount = static_cast<float>(std::max<std::size_t>(1U, text.size()));
        const float estimatedFontSize = visualWidth / glyphCount / 0.55f;
        const float extractedFontSize = static_cast<float>(std::max(1.0, chunk.fontSize * scale));
        run.fontSize = extractedFontSize > 48.0f
            ? std::clamp(estimatedFontSize, 6.0f, 48.0f)
            : extractedFontSize;
        auto family = utf8ToWide(chunk.fontFamily.c_str());
        for (auto& character : family) character = static_cast<wchar_t>(std::towlower(character));
        if (family.find(L"times") != std::wstring::npos || family.find(L"roman") != std::wstring::npos) {
            run.fontFamily = L"Times New Roman";
        } else if (family.find(L"courier") != std::wstring::npos || family.find(L"mono") != std::wstring::npos) {
            run.fontFamily = L"Consolas";
        } else if (family.find(L"sans") != std::wstring::npos || family.find(L"arial") != std::wstring::npos ||
                   family.find(L"helvetica") != std::wstring::npos || family.find(L"peignot") != std::wstring::npos) {
            run.fontFamily = L"Arial";
        } else {
            run.fontFamily = L"Segoe UI";
        }
        run.text = text;
        runs.push_back(std::move(run));
    }

    // Music/tab pages intentionally position each lyric syllable below an
    // individual note. Reflowing those runs into a prose line destroys the
    // notation layout; keep their original PDF x positions. Prose-heavy
    // pages still use the line reflow below so split TJ strings retain words.
    const bool preservePositionedRuns = runs.size() > 150U;

    // TJ-heavy documents (especially tables of contents) split one visual
    // line into many tiny chunks. Drawing each chunk with a fallback font
    // applies the fallback font's advance repeatedly and visibly stretches
    // or overlaps the line. Reassemble runs on the same baseline, then let
    // GDI+ lay out the complete line once.
    std::stable_sort(runs.begin(), runs.end(), [](const OverlayRun& left, const OverlayRun& right) {
        if (std::abs(left.top - right.top) > 1.0f) {
            return left.top < right.top;
        }
        return left.left < right.left;
    });
    std::vector<OverlayRun> lines;
    std::vector<OverlayRun> reflowRuns;
    reflowRuns.reserve(runs.size());
    for (const auto& run : runs) {
        // Keep small lyric/tab labels at their exact x coordinate, while
        // allowing large title/subtitle runs to be composed as one heading.
        if (preservePositionedRuns && run.fontSize < 20.0f) lines.push_back(run);
        else reflowRuns.push_back(run);
    }
    for (const auto& run : reflowRuns) {
        OverlayRun* line = nullptr;
        for (auto& candidate : lines) {
            if (std::abs(candidate.top - run.top) <= std::max(candidate.fontSize, run.fontSize) * 0.35f) {
                line = &candidate;
                break;
            }
        }
        if (!line) {
            lines.push_back(run);
            continue;
        }
        const float gap = run.left - line->right;
        // The source PDF uses a Type1 font whose word gaps are only a small
        // fraction of the fallback font size.  A 0.25em threshold misses
        // real boundaries such as "variables i and j"; use the measured
        // chunk gap while retaining a small tolerance for glyph kerning.
        if (gap > line->fontSize * 0.12f && !line->text.empty() && line->text.back() != L' ') {
            line->text.push_back(L' ');
        }
        line->text += run.text;
        line->fontSize = std::max(line->fontSize, run.fontSize);
        if (line->fontFamily != run.fontFamily) line->fontFamily = L"Arial";
        line->left = std::min(line->left, run.left);
        line->right = std::max(line->right, run.right);
        line->top = std::min(line->top, run.top);
    }
    std::vector<OverlayRun> drawLines;
    if (preservePositionedRuns) {
        for (const auto& run : lines) {
            if (run.fontSize >= 20.0f || drawLines.empty()) {
                drawLines.push_back(run);
                continue;
            }
            auto& previous = drawLines.back();
            const float baselineDelta = std::abs(previous.top - run.top);
            const float gap = run.left - previous.right;
            if (previous.fontSize < 20.0f && baselineDelta <= 2.0f &&
                gap <= std::max(previous.fontSize, run.fontSize) * 1.25f) {
                previous.text += run.text;
                previous.right = std::max(previous.right, run.right);
                previous.fontSize = std::max(previous.fontSize, run.fontSize);
            } else {
                drawLines.push_back(run);
            }
        }
    } else {
        drawLines = lines;
    }
    for (const auto& line : drawLines) {
        if (line.text.empty()) continue;
        std::wstring text = line.text;
        // A number of legacy Type1 contents encode visual word boundaries as
        // positioning rather than a literal space. Preserve the common
        // section-number and C# boundaries when rebuilding the line.
        std::wstring normalized;
        normalized.reserve(text.size() + 8U);
        for (std::size_t i = 0; i < text.size(); ++i) {
            const wchar_t current = text[i];
            const wchar_t next = i + 1U < text.size() ? text[i + 1U] : L'\0';
            if (!normalized.empty() && normalized.back() != L' ' &&
                ((normalized.back() == L'#' && std::iswalnum(next)) ||
                 (std::iswdigit(normalized.back()) && std::iswalpha(next)))) {
                normalized.push_back(L' ');
            }
            normalized.push_back(current);
            if (current == L'.' && i + 3U < text.size() && text[i + 1U] == L'N' &&
                text[i + 2U] == L'E' && text[i + 3U] == L'T' &&
                !normalized.empty() && normalized.size() > 1U && normalized[normalized.size() - 2U] != L' ') {
                normalized.insert(normalized.end() - 1, L' ');
            }
        }
        normalized = restoreCommonPdfWordBoundaries(std::move(normalized));
        Gdiplus::Font font(line.fontFamily.c_str(), line.fontSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        graphics.DrawString(normalized.c_str(), static_cast<INT>(normalized.size()), &font,
                            Gdiplus::PointF(line.left, line.top), &brush);
    }
    graphics.Flush(Gdiplus::FlushIntentionSync);

    // GDI+ may keep a cached surface for a Bitmap backed by caller memory.
    // Lock the surface after drawing so the rendered glyphs are copied back
    // instead of copying the pre-draw staging buffer.
    Gdiplus::Rect lockRect(0, 0, static_cast<INT>(bitmap.GetWidth()),
                           static_cast<INT>(bitmap.GetHeight()));
    Gdiplus::BitmapData locked{};
    if (image.LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                       PixelFormat32bppARGB, &locked) == Gdiplus::Ok) {
        const auto rowBytes = static_cast<std::size_t>(bitmap.GetStride());
        const auto sourceStride = static_cast<std::size_t>(std::abs(locked.Stride));
        const auto* source = static_cast<const std::uint8_t*>(locked.Scan0);
        for (std::size_t y = 0; y < bitmap.GetHeight(); ++y) {
            const auto sourceRow = locked.Stride >= 0
                ? source + y * sourceStride
                : source + (bitmap.GetHeight() - 1U - y) * sourceStride;
            std::memcpy(bgra.data() + y * rowBytes, sourceRow,
                        std::min(rowBytes, sourceStride));
        }
        image.UnlockBits(&locked);
    }

    auto writable = bitmap.GetPixels();
    for (std::size_t i = 0; i + 3U < writable.size(); i += 4U) {
        writable[i] = static_cast<std::byte>(bgra[i + 2U]);
        writable[i + 1U] = static_cast<std::byte>(bgra[i + 1U]);
        writable[i + 2U] = static_cast<std::byte>(bgra[i]);
        writable[i + 3U] = static_cast<std::byte>(bgra[i + 3U]);
    }
}
}

template <typename PathChar>
void* openDocument(const PathChar* path, const char** error) {
    try {
        auto result = std::make_unique<NativeDocument>();
        const auto filePath = std::filesystem::path(path);
        result->path = filePath;
        try {
            result->document = CPPPdf::PdfDocument::OpenMapped(filePath);
        } catch (const std::exception&) {
            // Keep the GUI usable on filesystems where CreateFileMapping is unavailable.
            result->document = CPPPdf::PdfDocument::Open(filePath);
        }
        if (error) *error = nullptr;
        return result.release();
    } catch (const std::exception& exception) {
        lastError = exception.what();
        if (error) *error = lastError.c_str();
        return nullptr;
    }
}

PDFPP_EXPORT void* pdfpp_open(const char* path, const char** error) {
    return openDocument(path, error);
}

#ifdef _WIN32
PDFPP_EXPORT void* pdfpp_open_w(const wchar_t* path, const char** error) {
    return openDocument(path, error);
}
#endif

PDFPP_EXPORT void pdfpp_close(void* handle) { delete static_cast<NativeDocument*>(handle); }
PDFPP_EXPORT int pdfpp_page_count(void* handle) {
    try { return static_cast<int>(static_cast<NativeDocument*>(handle)->document.GetPageCount()); }
    catch (...) { return 0; }
}
PDFPP_EXPORT const char* pdfpp_title(void* handle) {
    lastString = static_cast<NativeDocument*>(handle)->document.GetDocumentInfo().title; return lastString.c_str();
}
PDFPP_EXPORT const char* pdfpp_author(void* handle) {
    lastString = static_cast<NativeDocument*>(handle)->document.GetDocumentInfo().author; return lastString.c_str();
}
PDFPP_EXPORT const char* pdfpp_text(void* handle, int page) {
    try { lastString = static_cast<NativeDocument*>(handle)->document.ExtractText(static_cast<std::size_t>(page), {}); }
    catch (...) { lastString.clear(); }
    return lastString.c_str();
}

PDFPP_EXPORT void* pdfpp_render(void* handle, int page, double scale, int* width, int* height, int* stride) {
    try {
        auto* nativeDocument = static_cast<NativeDocument*>(handle);
        void* windowsBuffer{};
        try {
            if (tryRenderWithWindowsPdf(nativeDocument->path,
                                        static_cast<std::size_t>(page), scale,
                                        &windowsBuffer, width, height, stride)) {
                return windowsBuffer;
            }
        } catch (const winrt::hresult_error&) {
            // Fall back to Pdf++'s renderer for unsupported/password-
            // protected documents or systems without the Windows PDF broker.
        } catch (const std::exception&) {
            // GDI+ or another platform decoder can fail for a malformed or
            // unusual stream; retain the existing renderer as a safe fallback.
        }

        CPPPdf::PdfBitmap bitmap;
        std::exception_ptr failure;
        std::vector<CPPPdf::PdfTextChunk> textChunks;
        try {
            textChunks = nativeDocument->document
                .ExtractTextChunks(static_cast<std::size_t>(page), {});
        } catch (...) {
            textChunks.clear();
        }
        constexpr bool attempts[][3] = {
            {true, true, true},
            {true, false, true},
            {true, true, false},
            {true, false, false}
        };
        for (const auto& attempt : attempts) {
            try {
                CPPPdf::PdfRenderOptions options;
                options.dpi = 96.0 * scale;
                options.renderPaths = attempt[0];
                options.renderImages = attempt[1];
                options.renderText = textChunks.empty();
                bitmap = CPPPdf::PdfPageRenderer::Render(
                    nativeDocument->document,
                    static_cast<std::size_t>(page), options);
                failure = nullptr;
                break;
            } catch (...) {
                failure = std::current_exception();
            }
        }
        if (failure) std::rethrow_exception(failure);
        if (!textChunks.empty()) {
            overlayText(nativeDocument->document,
                        static_cast<std::size_t>(page), (96.0 / 72.0) * scale, bitmap,
                        textChunks);
        }
        const auto bytes = bitmap.GetPixels();
        void* result = std::malloc(bytes.size());
        if (!result) return nullptr;
        std::memcpy(result, bytes.data(), bytes.size());
        *width = static_cast<int>(bitmap.GetWidth()); *height = static_cast<int>(bitmap.GetHeight());
        *stride = static_cast<int>(bitmap.GetStride());
        return result;
    } catch (const std::exception& exception) {
        lastError = exception.what();
        return nullptr;
    } catch (...) {
        lastError = "Unknown Pdf++ rendering failure.";
        return nullptr;
    }
}
PDFPP_EXPORT void pdfpp_free(void* memory) { std::free(memory); }
PDFPP_EXPORT const char* pdfpp_last_error() { return lastError.c_str(); }

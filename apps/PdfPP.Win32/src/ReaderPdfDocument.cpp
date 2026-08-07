#include <PdfPP/Win32/ReaderPdfDocument.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Document/PdfPage.hpp>
#include <CPPPdf/Document/PdfPageImporter.hpp>
#include <CPPPdf/Document/PdfPageOrganizer.hpp>
#include <CPPPdf/Rendering/PdfPageRenderer.hpp>
#include <CPPPdf/Security/PdfSecurity.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef GetObject
#undef GetObject
#endif
#include <gdiplus.h>
#include <objidl.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace {
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

// PDF outline titles are PDF strings, not necessarily UTF-8.  A BOM marks
// UTF-16BE; strings without a BOM use PDFDocEncoding (which is close to
// Windows-1252 for the printable range). Normalize both forms to UTF-8 before
// handing the title to the Win32 tree view.
std::string pdfStringToUtf8(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() >= 2U &&
        static_cast<unsigned char>(value[0]) == 0xFEU &&
        static_cast<unsigned char>(value[1]) == 0xFFU) {
        std::wstring wide;
        wide.reserve((value.size() - 2U) / 2U);
        for (std::size_t i = 2U; i + 1U < value.size(); i += 2U) {
            wide.push_back(static_cast<wchar_t>(
                (static_cast<unsigned int>(static_cast<unsigned char>(value[i])) << 8U) |
                static_cast<unsigned int>(static_cast<unsigned char>(value[i + 1U]))));
        }
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0) return {};
        std::string result(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
            static_cast<int>(wide.size()), result.data(), length, nullptr, nullptr);
        return result;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), nullptr, 0) > 0) {
        return value;
    }
    const int wideLength = MultiByteToWideChar(1252, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (wideLength <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(1252, 0, value.data(), static_cast<int>(value.size()),
                        wide.data(), wideLength);
    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength,
        nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) return {};
    std::string result(static_cast<std::size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, result.data(),
                        utf8Length, nullptr, nullptr);
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
    // PdfPage::Size is already expressed in device-independent pixels (DIPs),
    // and DestinationWidth/DestinationHeight also expect DIPs. Multiplying by
    // 96/72 here rendered every Windows-PDF bitmap 4/3 larger than the page
    // geometry used by the Win32 reader. The following page was therefore
    // positioned before the previous bitmap actually ended, producing the
    // apparent page overlap while scrolling.
    // Clamp while the value is still double. This keeps all three std::clamp
    // arguments the same type on MSVC; std::lround returns long, whereas the
    // former 1LL/16384LL bounds were long long and prevented template deduction.
    const double scaledWidth = std::clamp(
        static_cast<double>(size.Width) * scale, 1.0, 16384.0);
    const double scaledHeight = std::clamp(
        static_cast<double>(size.Height) * scale, 1.0, 16384.0);
    const auto destinationWidth = static_cast<std::uint32_t>(std::lround(scaledWidth));
    const auto destinationHeight = static_cast<std::uint32_t>(std::lround(scaledHeight));

    winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
    options.DestinationWidth(destinationWidth);
    options.DestinationHeight(destinationHeight);
    constexpr auto opaque = static_cast<std::uint8_t>(255);
    options.BackgroundColor({opaque, opaque, opaque, opaque});
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
        // PixelFormat32bppARGB is already laid out as BGRA in memory, exactly
        // what a Win32 32-bit BI_RGB DIB consumes. Preserve it directly instead
        // of swapping red/blue here and swapping them back in the reader.
        std::memcpy(destinationRow, sourceRow, std::min(resultStride, sourceStride));
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

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        throw std::runtime_error("Unable to convert the file prefix to UTF-8.");
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

template <typename Operation>
bool runCoreOperation(std::string& error, Operation&& operation) noexcept {
    try {
        operation();
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "Unknown Pdf++.Core operation failure.";
        return false;
    }
}

void requireSelectedPages(const std::vector<std::size_t>& pages) {
    if (pages.empty()) {
        throw std::invalid_argument("At least one page must be selected.");
    }
}

CPPPdf::PdfEncryptionOptions encryptionOptions(
    const std::string& userPassword,
    const std::string& ownerPassword) {
    CPPPdf::PdfEncryptionOptions options;
    options.userPassword = userPassword;
    options.ownerPassword = ownerPassword.empty() ? userPassword : ownerPassword;
    options.algorithm = CPPPdf::PdfEncryptionAlgorithm::Aes256;
    return options;
}

} // namespace

namespace PdfPP::Win32 {

struct ReaderPdfDocument::Impl final {
    CPPPdf::PdfDocument document;
    std::filesystem::path path;

    Impl(CPPPdf::PdfDocument source, std::filesystem::path sourcePath)
        : document(std::move(source)), path(std::move(sourcePath)) {}
};

bool PageBitmap::IsValid() const noexcept {
    return width > 0 && height > 0 && stride >= width * 4 &&
        pixels.size() >= static_cast<std::size_t>(height) *
            static_cast<std::size_t>(stride);
}

ReaderPdfDocument::ReaderPdfDocument(
    std::unique_ptr<Impl> implementation) noexcept
    : impl_(std::move(implementation)) {}

ReaderPdfDocument::~ReaderPdfDocument() = default;

std::shared_ptr<ReaderPdfDocument> ReaderPdfDocument::Open(
    const std::wstring& path, std::string& error) {
    try {
        const std::filesystem::path filePath(path);
        CPPPdf::PdfDocument opened;
        try {
            opened = CPPPdf::PdfDocument::OpenMapped(filePath);
        } catch (const std::exception&) {
            // Preserve the old reader behavior on filesystems where mapped
            // input is unavailable.
            opened = CPPPdf::PdfDocument::Open(filePath);
        }
        error.clear();
        return std::shared_ptr<ReaderPdfDocument>(
            new ReaderPdfDocument(std::make_unique<Impl>(
                std::move(opened), filePath)));
    } catch (const std::exception& exception) {
        error = exception.what();
        return {};
    } catch (...) {
        error = "Unable to open PDF.";
        return {};
    }
}

bool ReaderPdfDocument::MergeDocuments(
    const std::vector<std::wstring>& inputPaths,
    const std::wstring& outputPath,
    std::string& error) {
    return runCoreOperation(error, [&] {
        if (inputPaths.size() < 2U) {
            throw std::invalid_argument(
                "Select at least two PDF files to merge.");
        }
        std::vector<std::filesystem::path> inputs;
        inputs.reserve(inputPaths.size());
        for (const auto& input : inputPaths) inputs.emplace_back(input);
        (void)CPPPdf::PdfPageImporter::MergeDocuments(
            inputs, std::filesystem::path(outputPath));
    });
}

bool ReaderPdfDocument::ExtractPages(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::vector<std::size_t>& pageIndices,
    std::string& error) {
    return runCoreOperation(error, [&] {
        requireSelectedPages(pageIndices);
        (void)CPPPdf::PdfPageOrganizer::ExtractPages(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputPath), pageIndices);
    });
}

bool ReaderPdfDocument::RemovePages(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::vector<std::size_t>& pageIndices,
    std::string& error) {
    return runCoreOperation(error, [&] {
        requireSelectedPages(pageIndices);
        (void)CPPPdf::PdfPageOrganizer::RemovePages(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputPath), pageIndices);
    });
}

bool ReaderPdfDocument::DuplicatePages(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::vector<std::size_t>& pageIndices,
    std::string& error) {
    return runCoreOperation(error, [&] {
        requireSelectedPages(pageIndices);
        (void)CPPPdf::PdfPageOrganizer::DuplicatePages(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputPath), pageIndices);
    });
}

bool ReaderPdfDocument::MovePage(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::size_t fromIndex,
    const std::size_t toIndex,
    std::string& error) {
    return runCoreOperation(error, [&] {
        (void)CPPPdf::PdfPageOrganizer::MovePage(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputPath), fromIndex, toIndex);
    });
}

bool ReaderPdfDocument::ReorderPages(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::vector<std::size_t>& pageOrder,
    std::string& error) {
    return runCoreOperation(error, [&] {
        requireSelectedPages(pageOrder);
        (void)CPPPdf::PdfPageOrganizer::ReorderPages(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputPath), pageOrder);
    });
}

bool ReaderPdfDocument::SplitEvery(
    const std::wstring& inputPath,
    const std::wstring& outputDirectory,
    const std::size_t pagesPerFile,
    const std::wstring& filePrefix,
    std::string& error) {
    return runCoreOperation(error, [&] {
        if (pagesPerFile == 0U) {
            throw std::invalid_argument(
                "Pages per file must be greater than zero.");
        }
        const std::string prefix = filePrefix.empty()
            ? std::string("part") : wideToUtf8(filePrefix);
        (void)CPPPdf::PdfPageOrganizer::SplitEvery(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputDirectory), pagesPerFile, prefix);
    });
}

bool ReaderPdfDocument::AddPassword(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::string& currentPassword,
    const std::string& userPassword,
    const std::string& ownerPassword,
    std::string& error) {
    return runCoreOperation(error, [&] {
        CPPPdf::PdfPasswordManager::Encrypt(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputPath),
            encryptionOptions(userPassword, ownerPassword), currentPassword);
    });
}

bool ReaderPdfDocument::RemovePassword(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::string& currentPassword,
    std::string& error) {
    return runCoreOperation(error, [&] {
        CPPPdf::PdfPasswordManager::RemovePassword(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputPath), currentPassword);
    });
}

bool ReaderPdfDocument::ChangePassword(
    const std::wstring& inputPath,
    const std::wstring& outputPath,
    const std::string& currentPassword,
    const std::string& userPassword,
    const std::string& ownerPassword,
    std::string& error) {
    return runCoreOperation(error, [&] {
        CPPPdf::PdfPasswordManager::ChangePassword(
            std::filesystem::path(inputPath),
            std::filesystem::path(outputPath), currentPassword,
            encryptionOptions(userPassword, ownerPassword));
    });
}

int ReaderPdfDocument::PageCount() const noexcept {
    try {
        return impl_ ? static_cast<int>(impl_->document.GetPageCount()) : 0;
    } catch (...) {
        return 0;
    }
}

std::string ReaderPdfDocument::Title() const {
    if (!impl_) return {};
    return impl_->document.GetDocumentInfo().title;
}

std::vector<TocItem> ReaderPdfDocument::TableOfContents() const {
    std::vector<TocItem> result;
    if (!impl_) return result;
    try {
        const auto outlines = impl_->document.GetOutlines();
        result.reserve(outlines.size());
        for (const auto& outline : outlines) {
            TocItem item;
            item.level = static_cast<int>(outline.depth);
            item.page = outline.destinationPageIndex
                ? static_cast<int>(*outline.destinationPageIndex) : -1;
            item.title = utf8ToWide(pdfStringToUtf8(outline.title));
            result.push_back(std::move(item));
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

std::string ReaderPdfDocument::Text(const int page) const {
    if (!impl_ || page < 0) return {};
    try {
        return impl_->document.ExtractText(
            static_cast<std::size_t>(page), {});
    } catch (...) {
        return {};
    }
}

std::vector<std::string> ReaderPdfDocument::Comments(const int page) const {
    std::vector<std::string> result;
    if (!impl_ || page < 0) return result;
    try {
        const auto annotations = impl_->document.GetAnnotations(
            static_cast<std::size_t>(page));
        result.reserve(annotations.size());
        for (const auto& annotation : annotations) {
            std::string searchable;
            if (!annotation.title.empty()) searchable = annotation.title;
            if (!annotation.contents.empty()) {
                if (!searchable.empty()) searchable.push_back('\n');
                searchable += annotation.contents;
            }
            if (!searchable.empty()) result.push_back(std::move(searchable));
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

std::vector<CommentItem> ReaderPdfDocument::CommentsDetailed(const int page) const {
    std::vector<CommentItem> result;
    if (!impl_ || page < 0) return result;
    try {
        const auto annotations = impl_->document.GetAnnotations(
            static_cast<std::size_t>(page));
        result.reserve(annotations.size());
        for (const auto& annotation : annotations) {
            CommentItem item;
            item.page = page;
            item.subtype = utf8ToWide(annotation.subtype.c_str());
            item.title = utf8ToWide(pdfStringToUtf8(annotation.title));
            item.subject = utf8ToWide(pdfStringToUtf8(annotation.subject));
            item.contents = utf8ToWide(pdfStringToUtf8(annotation.contents));
            item.objectNumber = annotation.objectNumber;
            item.left = annotation.rect.left;
            item.bottom = annotation.rect.bottom;
            item.right = annotation.rect.right;
            item.top = annotation.rect.top;
            const bool interactiveComment = item.subtype != L"Link" &&
                item.subtype != L"Widget" && item.subtype != L"Popup";
            if (interactiveComment || !item.title.empty() || !item.subject.empty() ||
                !item.contents.empty()) {
                result.push_back(std::move(item));
            }
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

bool ReaderPdfDocument::PagePointSize(
    const int page, double& width, double& height) const {
    width = 0.0;
    height = 0.0;
    if (!impl_ || page < 0) return false;
    try {
        const auto info = impl_->document.GetPageInfo(
            static_cast<std::size_t>(page));
        const double boxWidth = info.cropBox.empty()
            ? info.mediaBox.width() : info.cropBox.width();
        const double boxHeight = info.cropBox.empty()
            ? info.mediaBox.height() : info.cropBox.height();
        const int rotation = ((info.rotation % 360) + 360) % 360;
        width = rotation == 90 || rotation == 270 ? boxHeight : boxWidth;
        height = rotation == 90 || rotation == 270 ? boxWidth : boxHeight;
        return width > 0.0 && height > 0.0;
    } catch (...) {
        width = 0.0;
        height = 0.0;
        return false;
    }
}

bool ReaderPdfDocument::PageCoordinates(
    const int page, PageCoordinateInfo& result) const {
    result = {};
    if (!impl_ || page < 0) return false;
    try {
        const auto info = impl_->document.GetPageInfo(static_cast<std::size_t>(page));
        const auto box = info.cropBox.empty() ? info.mediaBox : info.cropBox;
        if (box.empty()) return false;
        result.left = box.left;
        result.bottom = box.bottom;
        result.right = box.right;
        result.top = box.top;
        result.rotation = ((info.rotation % 360) + 360) % 360;
        return true;
    } catch (...) {
        result = {};
        return false;
    }
}

std::vector<TextChunk> ReaderPdfDocument::TextChunks(const int page) const {
    std::vector<TextChunk> result;
    if (!impl_ || page < 0) return result;
    try {
        const auto chunks = impl_->document.ExtractTextChunks(
            static_cast<std::size_t>(page), {});
        result.reserve(chunks.size());
        for (const auto& chunk : chunks) {
            result.push_back(TextChunk{
                chunk.utf8Text,
                chunk.boundingBox.left,
                chunk.boundingBox.bottom,
                chunk.boundingBox.right,
                chunk.boundingBox.top});
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

bool ReaderPdfDocument::PageSize(
    const int page, const double scale,
    int& width, int& height) const {
    width = 0;
    height = 0;
    if (!impl_ || page < 0 || !std::isfinite(scale) || scale <= 0.0) {
        return false;
    }
    try {
        const auto info = impl_->document.GetPageInfo(
            static_cast<std::size_t>(page));
        const double boxWidth = info.cropBox.empty()
            ? info.mediaBox.width() : info.cropBox.width();
        const double boxHeight = info.cropBox.empty()
            ? info.mediaBox.height() : info.cropBox.height();
        const int rotation = ((info.rotation % 360) + 360) % 360;
        const double pointsWidth = rotation == 90 || rotation == 270
            ? boxHeight : boxWidth;
        const double pointsHeight = rotation == 90 || rotation == 270
            ? boxWidth : boxHeight;
        constexpr double pixelsPerPoint = 96.0 / 72.0;
        width = std::max(1, static_cast<int>(std::lround(
            pointsWidth * pixelsPerPoint * scale)));
        height = std::max(1, static_cast<int>(std::lround(
            pointsHeight * pixelsPerPoint * scale)));
        return true;
    } catch (...) {
        width = 0;
        height = 0;
        return false;
    }
}

PageBitmap ReaderPdfDocument::Render(
    const int page, const double zoom,
    const unsigned int dpi, std::string& error) const {
    PageBitmap result;
    result.page = page;
    result.zoom = zoom;
    result.dpi = dpi;
    if (!impl_ || page < 0 || !std::isfinite(zoom) || zoom <= 0.0 || dpi == 0U) {
        error = "Invalid render request.";
        return result;
    }

    try {
        const double renderScale = zoom *
            static_cast<double>(dpi) / 96.0;
        void* windowsBuffer{};
        int width{};
        int height{};
        int stride{};
        try {
            if (tryRenderWithWindowsPdf(
                    impl_->path, static_cast<std::size_t>(page), renderScale,
                    &windowsBuffer, &width, &height, &stride)) {
                const auto releaseBuffer = [](void* memory) noexcept {
                    std::free(memory);
                };
                const std::unique_ptr<void, decltype(releaseBuffer)> owner(
                    windowsBuffer, releaseBuffer);
                result.width = width;
                result.height = height;
                result.stride = stride;
                const std::size_t byteCount =
                    static_cast<std::size_t>(height) *
                    static_cast<std::size_t>(stride);
                result.pixels.resize(byteCount);
                std::memcpy(result.pixels.data(), windowsBuffer, byteCount);
                error.clear();
                return result;
            }
        } catch (const winrt::hresult_error&) {
            // Fall through to the portable Pdf++.Core renderer.
        } catch (const std::exception&) {
            // GDI+/WinRT failures must not make the reader unusable.
        }

        CPPPdf::PdfBitmap bitmap;
        std::exception_ptr failure;
        std::vector<CPPPdf::PdfTextChunk> textChunks;
        try {
            textChunks = impl_->document.ExtractTextChunks(
                static_cast<std::size_t>(page), {});
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
                options.dpi = 96.0 * renderScale;
                options.renderPaths = attempt[0];
                options.renderImages = attempt[1];
                options.renderText = textChunks.empty() && attempt[2];
                bitmap = CPPPdf::PdfPageRenderer::Render(
                    impl_->document, static_cast<std::size_t>(page), options);
                failure = nullptr;
                break;
            } catch (...) {
                failure = std::current_exception();
            }
        }
        if (failure) std::rethrow_exception(failure);

        if (!textChunks.empty()) {
            overlayText(
                impl_->document, static_cast<std::size_t>(page),
                (96.0 / 72.0) * renderScale, bitmap, textChunks);
        }

        result.width = static_cast<int>(bitmap.GetWidth());
        result.height = static_cast<int>(bitmap.GetHeight());
        result.stride = static_cast<int>(bitmap.GetStride());
        const auto source = bitmap.GetPixels();
        result.pixels.resize(source.size());
        const auto* rgba = reinterpret_cast<const std::uint8_t*>(source.data());
        for (std::size_t index = 0; index + 3U < source.size(); index += 4U) {
            result.pixels[index] = rgba[index + 2U];
            result.pixels[index + 1U] = rgba[index + 1U];
            result.pixels[index + 2U] = rgba[index];
            result.pixels[index + 3U] = rgba[index + 3U];
        }
        error.clear();
        return result;
    } catch (const std::exception& exception) {
        error = exception.what();
        return result;
    } catch (...) {
        error = "Unknown Pdf++.Core rendering failure.";
        return result;
    }
}

} // namespace PdfPP::Win32

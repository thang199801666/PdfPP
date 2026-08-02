#include <PdfPP/Win32/NativePdf.hpp>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <utility>

extern "C" {
void* pdfpp_open_w(const wchar_t*, const char**);
void pdfpp_close(void*);
int pdfpp_page_count(void*);
void pdfpp_page_size(void*, int, double, int*, int*);
const char* pdfpp_title(void*);
void* pdfpp_render(void*, int, double, int*, int*, int*);
void pdfpp_free(void*);
const char* pdfpp_last_error();
const char* pdfpp_text(void*, int);
const char* pdfpp_toc(void*);
struct PdfTextChunkView { char* text; double left; double bottom; double right; double top; };
void* pdfpp_text_chunks(void*, int, int*);
void pdfpp_free_text_chunks(void*, int);
}

namespace PdfPP::Win32 {

bool PageBitmap::IsValid() const noexcept {
    return width > 0 && height > 0 && stride >= width * 4 &&
           pixels.size() >= static_cast<std::size_t>(height) * stride;
}

NativePdfDocument::NativePdfDocument(void* handle) noexcept : handle_(handle) {}

NativePdfDocument::~NativePdfDocument() {
    if (handle_) pdfpp_close(handle_);
}

std::shared_ptr<NativePdfDocument> NativePdfDocument::Open(
    const std::wstring& path, std::string& error) {
    const char* nativeError{};
    void* handle = pdfpp_open_w(path.c_str(), &nativeError);
    if (!handle) {
        error = nativeError && *nativeError ? nativeError : "Unable to open PDF.";
        return {};
    }
    error.clear();
    return std::shared_ptr<NativePdfDocument>(new NativePdfDocument(handle));
}

int NativePdfDocument::PageCount() const noexcept {
    return handle_ ? pdfpp_page_count(handle_) : 0;
}

bool NativePdfDocument::PageSize(const int page, const double scale,
                                 int& width, int& height) const {
    if (!handle_ || page < 0) return false;
    pdfpp_page_size(handle_, page, scale, &width, &height);
    return width > 0 && height > 0;
}

std::string NativePdfDocument::Title() const {
    const char* value = handle_ ? pdfpp_title(handle_) : nullptr;
    return value ? value : "";
}

std::vector<TocItem> NativePdfDocument::TableOfContents() const {
    std::vector<TocItem> result;
    if (!handle_) return result;
    const char* raw = pdfpp_toc(handle_);
    if (!raw || !*raw) return result;
    std::string line;
    std::istringstream input(raw);
    while (std::getline(input, line)) {
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1U);
        if (first == std::string::npos || second == std::string::npos) continue;
        try {
            TocItem item;
            item.level = std::stoi(line.substr(0, first));
            item.page = std::stoi(line.substr(first + 1U, second - first - 1U));
            const std::string title = line.substr(second + 1U);
            const int length = MultiByteToWideChar(CP_UTF8, 0, title.data(), static_cast<int>(title.size()), nullptr, 0);
            if (length > 0) {
                item.title.resize(static_cast<std::size_t>(length));
                MultiByteToWideChar(CP_UTF8, 0, title.data(), static_cast<int>(title.size()), item.title.data(), length);
            }
            result.push_back(std::move(item));
        } catch (...) {
        }
    }
    return result;
}

std::string NativePdfDocument::Text(const int page) const {
    const char* value = handle_ ? pdfpp_text(handle_, page) : nullptr;
    return value ? value : "";
}

std::vector<TextChunk> NativePdfDocument::TextChunks(const int page) const {
    std::vector<TextChunk> result;
    if (!handle_) return result;
    int count = 0;
    void* raw = pdfpp_text_chunks(handle_, page, &count);
    if (!raw || count <= 0) return result;
    const auto* chunks = static_cast<const PdfTextChunkView*>(raw);
    result.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const auto& chunk = chunks[index];
        if (!chunk.text || !*chunk.text) continue;
        result.push_back(TextChunk{chunk.text, chunk.left, chunk.bottom, chunk.right, chunk.top});
    }
    pdfpp_free_text_chunks(raw, count);
    return result;
}

PageBitmap NativePdfDocument::Render(const int page, const double zoom,
                                     const unsigned int dpi, std::string& error) const {
    PageBitmap bitmap;
    bitmap.page = page;
    bitmap.zoom = zoom;
    bitmap.dpi = dpi;
    if (!handle_) {
        error = "No PDF document is open.";
        return bitmap;
    }

    const double renderScale = zoom * dpi / USER_DEFAULT_SCREEN_DPI;
    void* buffer = pdfpp_render(handle_, page, renderScale,
                                &bitmap.width, &bitmap.height, &bitmap.stride);
    if (!buffer) {
        const char* nativeError = pdfpp_last_error();
        error = nativeError && *nativeError ? nativeError : "Unable to render PDF page.";
        return bitmap;
    }
    const std::unique_ptr<void, void (*)(void*)> bufferOwner(buffer, pdfpp_free);

    bitmap.pixels.resize(static_cast<std::size_t>(bitmap.height) * bitmap.stride);
    const auto* source = static_cast<const std::uint8_t*>(bufferOwner.get());
    for (int y = 0; y < bitmap.height; ++y) {
        for (int x = 0; x < bitmap.width; ++x) {
            const auto* src = source + static_cast<std::size_t>(y) * bitmap.stride + x * 4;
            auto* dst = bitmap.pixels.data() + static_cast<std::size_t>(y) * bitmap.stride + x * 4;
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
        }
    }
    error.clear();
    return bitmap;
}

} // namespace PdfPP::Win32

#include <PdfPP/Win32/NativePdf.hpp>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <utility>

extern "C" {
void* pdfpp_open_w(const wchar_t*, const char**);
void pdfpp_close(void*);
int pdfpp_page_count(void*);
const char* pdfpp_title(void*);
void* pdfpp_render(void*, int, double, int*, int*, int*);
void pdfpp_free(void*);
const char* pdfpp_last_error();
const char* pdfpp_text(void*, int);
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

std::string NativePdfDocument::Title() const {
    const char* value = handle_ ? pdfpp_title(handle_) : nullptr;
    return value ? value : "";
}

std::string NativePdfDocument::Text(const int page) const {
    const char* value = handle_ ? pdfpp_text(handle_, page) : nullptr;
    return value ? value : "";
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

#include <PdfPP/Win32/NativePdf.hpp>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
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
int pdfpp_merge_documents_w(const wchar_t* const*, std::size_t, const wchar_t*);
int pdfpp_extract_pages_w(const wchar_t*, const wchar_t*, const std::size_t*, std::size_t);
int pdfpp_remove_pages_w(const wchar_t*, const wchar_t*, const std::size_t*, std::size_t);
int pdfpp_duplicate_pages_w(const wchar_t*, const wchar_t*, const std::size_t*, std::size_t);
int pdfpp_move_page_w(const wchar_t*, const wchar_t*, std::size_t, std::size_t);
int pdfpp_reorder_pages_w(const wchar_t*, const wchar_t*, const std::size_t*, std::size_t);
int pdfpp_split_every_w(const wchar_t*, const wchar_t*, std::size_t, const wchar_t*);
int pdfpp_add_password_w(const wchar_t*, const wchar_t*, const char*, const char*, const char*);
int pdfpp_remove_password_w(const wchar_t*, const wchar_t*, const char*);
int pdfpp_change_password_w(const wchar_t*, const wchar_t*, const char*, const char*, const char*);
}

namespace PdfPP::Win32 {

bool PageBitmap::IsValid() const noexcept {
    return width > 0 && height > 0 && stride >= width * 4 &&
           pixels.size() >= static_cast<std::size_t>(height) * stride;
}

namespace {

bool operationSucceeded(const int result, std::string& error) {
    if (result == 0) {
        error.clear();
        return true;
    }
    const char* nativeError = pdfpp_last_error();
    error = nativeError && *nativeError
        ? nativeError : "Pdf++ document operation failed.";
    return false;
}

} // namespace

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

bool NativePdfDocument::MergeDocuments(
    const std::vector<std::wstring>& inputPaths, const std::wstring& outputPath,
    std::string& error) {
    std::vector<const wchar_t*> paths;
    paths.reserve(inputPaths.size());
    for (const auto& path : inputPaths) paths.push_back(path.c_str());
    return operationSucceeded(pdfpp_merge_documents_w(
        paths.data(), paths.size(), outputPath.c_str()), error);
}

bool NativePdfDocument::ExtractPages(
    const std::wstring& inputPath, const std::wstring& outputPath,
    const std::vector<std::size_t>& pageIndices, std::string& error) {
    return operationSucceeded(pdfpp_extract_pages_w(
        inputPath.c_str(), outputPath.c_str(), pageIndices.data(), pageIndices.size()), error);
}

bool NativePdfDocument::RemovePages(
    const std::wstring& inputPath, const std::wstring& outputPath,
    const std::vector<std::size_t>& pageIndices, std::string& error) {
    return operationSucceeded(pdfpp_remove_pages_w(
        inputPath.c_str(), outputPath.c_str(), pageIndices.data(), pageIndices.size()), error);
}

bool NativePdfDocument::DuplicatePages(
    const std::wstring& inputPath, const std::wstring& outputPath,
    const std::vector<std::size_t>& pageIndices, std::string& error) {
    return operationSucceeded(pdfpp_duplicate_pages_w(
        inputPath.c_str(), outputPath.c_str(), pageIndices.data(), pageIndices.size()), error);
}

bool NativePdfDocument::MovePage(
    const std::wstring& inputPath, const std::wstring& outputPath,
    const std::size_t fromIndex, const std::size_t toIndex, std::string& error) {
    return operationSucceeded(pdfpp_move_page_w(
        inputPath.c_str(), outputPath.c_str(), fromIndex, toIndex), error);
}

bool NativePdfDocument::ReorderPages(
    const std::wstring& inputPath, const std::wstring& outputPath,
    const std::vector<std::size_t>& pageOrder, std::string& error) {
    return operationSucceeded(pdfpp_reorder_pages_w(
        inputPath.c_str(), outputPath.c_str(), pageOrder.data(), pageOrder.size()), error);
}

bool NativePdfDocument::SplitEvery(
    const std::wstring& inputPath, const std::wstring& outputDirectory,
    const std::size_t pagesPerFile, const std::wstring& filePrefix,
    std::string& error) {
    return operationSucceeded(pdfpp_split_every_w(
        inputPath.c_str(), outputDirectory.c_str(), pagesPerFile,
        filePrefix.c_str()), error);
}

bool NativePdfDocument::AddPassword(
    const std::wstring& inputPath, const std::wstring& outputPath,
    const std::string& currentPassword, const std::string& userPassword,
    const std::string& ownerPassword, std::string& error) {
    return operationSucceeded(pdfpp_add_password_w(
        inputPath.c_str(), outputPath.c_str(), currentPassword.c_str(),
        userPassword.c_str(), ownerPassword.c_str()), error);
}

bool NativePdfDocument::RemovePassword(
    const std::wstring& inputPath, const std::wstring& outputPath,
    const std::string& currentPassword, std::string& error) {
    return operationSucceeded(pdfpp_remove_password_w(
        inputPath.c_str(), outputPath.c_str(), currentPassword.c_str()), error);
}

bool NativePdfDocument::ChangePassword(
    const std::wstring& inputPath, const std::wstring& outputPath,
    const std::string& currentPassword, const std::string& userPassword,
    const std::string& ownerPassword, std::string& error) {
    return operationSucceeded(pdfpp_change_password_w(
        inputPath.c_str(), outputPath.c_str(), currentPassword.c_str(),
        userPassword.c_str(), ownerPassword.c_str()), error);
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
    // pdfpp_render guarantees a top-down BGRA buffer matching the Win32 DIB
    // layout. Adopt it with one contiguous copy; the former nested channel-swap
    // loop was a noticeable part of every zoom render for large pages.
    std::memcpy(bitmap.pixels.data(), source, bitmap.pixels.size());
    error.clear();
    return bitmap;
}

} // namespace PdfPP::Win32

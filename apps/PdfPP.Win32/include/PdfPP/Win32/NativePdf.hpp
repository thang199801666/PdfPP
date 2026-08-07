#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace PdfPP::Win32 {

struct PageBitmap final {
    int page{};
    double zoom{};
    unsigned int dpi{96};
    int width{};
    int height{};
    int stride{};
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool IsValid() const noexcept;
};

struct TextChunk final {
    std::string text;
    double left{};
    double bottom{};
    double right{};
    double top{};
};

struct TocItem final {
    int level{};
    int page{-1};
    std::wstring title;
};

class NativePdfDocument final {
public:
    static std::shared_ptr<NativePdfDocument> Open(const std::wstring& path, std::string& error);

    // Document-level operations are implemented by PdfPP.Native so the Win32
    // UI remains a thin client and does not link Pdf++.Core directly. Page
    // indices are zero-based.
    [[nodiscard]] static bool MergeDocuments(
        const std::vector<std::wstring>& inputPaths, const std::wstring& outputPath,
        std::string& error);
    [[nodiscard]] static bool ExtractPages(
        const std::wstring& inputPath, const std::wstring& outputPath,
        const std::vector<std::size_t>& pageIndices, std::string& error);
    [[nodiscard]] static bool RemovePages(
        const std::wstring& inputPath, const std::wstring& outputPath,
        const std::vector<std::size_t>& pageIndices, std::string& error);
    [[nodiscard]] static bool DuplicatePages(
        const std::wstring& inputPath, const std::wstring& outputPath,
        const std::vector<std::size_t>& pageIndices, std::string& error);
    [[nodiscard]] static bool MovePage(
        const std::wstring& inputPath, const std::wstring& outputPath,
        std::size_t fromIndex, std::size_t toIndex, std::string& error);
    [[nodiscard]] static bool ReorderPages(
        const std::wstring& inputPath, const std::wstring& outputPath,
        const std::vector<std::size_t>& pageOrder, std::string& error);
    [[nodiscard]] static bool SplitEvery(
        const std::wstring& inputPath, const std::wstring& outputDirectory,
        std::size_t pagesPerFile, const std::wstring& filePrefix, std::string& error);
    [[nodiscard]] static bool AddPassword(
        const std::wstring& inputPath, const std::wstring& outputPath,
        const std::string& currentPassword, const std::string& userPassword,
        const std::string& ownerPassword, std::string& error);
    [[nodiscard]] static bool RemovePassword(
        const std::wstring& inputPath, const std::wstring& outputPath,
        const std::string& currentPassword, std::string& error);
    [[nodiscard]] static bool ChangePassword(
        const std::wstring& inputPath, const std::wstring& outputPath,
        const std::string& currentPassword, const std::string& userPassword,
        const std::string& ownerPassword, std::string& error);

    NativePdfDocument(const NativePdfDocument&) = delete;
    NativePdfDocument& operator=(const NativePdfDocument&) = delete;
    ~NativePdfDocument();

    [[nodiscard]] int PageCount() const noexcept;
    [[nodiscard]] std::string Title() const;
    [[nodiscard]] std::vector<TocItem> TableOfContents() const;
    [[nodiscard]] std::string Text(int page) const;
    [[nodiscard]] std::vector<TextChunk> TextChunks(int page) const;
    [[nodiscard]] PageBitmap Render(int page, double zoom, unsigned int dpi,
                                    std::string& error) const;
    // Pixel dimensions of a page at the given render scale, without rendering.
    [[nodiscard]] bool PageSize(int page, double scale, int& width, int& height) const;

private:
    explicit NativePdfDocument(void* handle) noexcept;

    void* handle_{};
};

} // namespace PdfPP::Win32

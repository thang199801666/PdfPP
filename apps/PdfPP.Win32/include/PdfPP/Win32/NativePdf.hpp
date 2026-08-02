#pragma once

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

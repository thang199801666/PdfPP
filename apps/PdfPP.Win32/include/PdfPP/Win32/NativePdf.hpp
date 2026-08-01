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

class NativePdfDocument final {
public:
    static std::shared_ptr<NativePdfDocument> Open(const std::wstring& path, std::string& error);

    NativePdfDocument(const NativePdfDocument&) = delete;
    NativePdfDocument& operator=(const NativePdfDocument&) = delete;
    ~NativePdfDocument();

    [[nodiscard]] int PageCount() const noexcept;
    [[nodiscard]] std::string Title() const;
    [[nodiscard]] std::string Text(int page) const;
    [[nodiscard]] PageBitmap Render(int page, double zoom, unsigned int dpi,
                                    std::string& error) const;

private:
    explicit NativePdfDocument(void* handle) noexcept;

    void* handle_{};
};

} // namespace PdfPP::Win32

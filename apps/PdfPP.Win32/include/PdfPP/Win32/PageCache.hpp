#pragma once

#include <PdfPP/Win32/NativePdf.hpp>

#include <cstddef>
#include <deque>
#include <optional>

namespace PdfPP::Win32 {

class PageCache final {
public:
    explicit PageCache(std::size_t capacity = 4) noexcept;

    void Clear() noexcept;
    void Store(PageBitmap bitmap);
    [[nodiscard]] bool Contains(int page, double zoom, unsigned int dpi) const noexcept;
    [[nodiscard]] std::optional<PageBitmap> Get(int page, double zoom, unsigned int dpi);
    // Transfers ownership of a cached bitmap without copying its pixel buffer.
    [[nodiscard]] std::optional<PageBitmap> Take(int page, double zoom, unsigned int dpi);

private:
    [[nodiscard]] static bool Matches(const PageBitmap& bitmap, int page,
                                      double zoom, unsigned int dpi) noexcept;

    std::size_t capacity_;
    std::deque<PageBitmap> entries_;
};

} // namespace PdfPP::Win32

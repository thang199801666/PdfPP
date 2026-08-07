#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace CPPPdf {

enum class PdfBlendMode {
    SourceOver,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity
};

struct PdfRgbaColor final {
    std::uint8_t red{0};
    std::uint8_t green{0};
    std::uint8_t blue{0};
    std::uint8_t alpha{255};

    [[nodiscard]] static constexpr PdfRgbaColor Black() noexcept { return {0, 0, 0, 255}; }
    [[nodiscard]] static constexpr PdfRgbaColor White() noexcept { return {255, 255, 255, 255}; }
};

class PdfBitmap final {
public:
    PdfBitmap() = default;
    PdfBitmap(std::size_t width, std::size_t height, PdfRgbaColor background = PdfRgbaColor::White());

    [[nodiscard]] std::size_t GetWidth() const noexcept { return width_; }
    [[nodiscard]] std::size_t GetHeight() const noexcept { return height_; }
    [[nodiscard]] std::size_t GetStride() const noexcept { return width_ * 4U; }
    [[nodiscard]] std::span<const std::byte> GetPixels() const noexcept { return pixels_; }
    [[nodiscard]] std::span<std::byte> GetPixels() noexcept { return pixels_; }
    [[nodiscard]] PdfRgbaColor GetPixel(std::size_t x, std::size_t y) const;

    void Clear(PdfRgbaColor color);
    void SetPixel(std::int32_t x, std::int32_t y, PdfRgbaColor color);
    void BlendPixel(std::int32_t x, std::int32_t y, PdfRgbaColor color);
    void BlendPixel(std::int32_t x, std::int32_t y, PdfRgbaColor color, PdfBlendMode mode);
    // Faster variant for rasterizers that already clipped coordinates.
    void BlendPixelInBounds(std::size_t x, std::size_t y, PdfRgbaColor color) noexcept;
    void BlendBitmap(const PdfBitmap& source, std::int32_t destinationX,
                     std::int32_t destinationY, std::uint8_t opacity = 255U);
    void BlendBitmap(const PdfBitmap& source, std::int32_t destinationX,
                     std::int32_t destinationY, PdfBlendMode mode,
                     std::uint8_t opacity = 255U);
    void SavePpm(const std::filesystem::path& path) const;
    // Writes a true-color 8-bit PNG (RGBA, non-interlaced, zlib-compressed)
    // without external libraries.
    void SavePng(const std::filesystem::path& path) const;
    // Writes a baseline JPEG (DCT, quality 1-100) without external libraries.
    void SaveJpeg(const std::filesystem::path& path, int quality = 85) const;
    // Writes a 32-bit BMP (uncompressed, bottom-up) without external libraries.
    void SaveBmp(const std::filesystem::path& path) const;

    // Returns a scaled copy (bilinear). Empty width/height keeps aspect ratio.
    [[nodiscard]] PdfBitmap Resize(std::size_t width, std::size_t height) const;
    // Returns a copy of the given rectangle (clamped to bounds).
    [[nodiscard]] PdfBitmap Crop(std::size_t x, std::size_t y,
                                 std::size_t width, std::size_t height) const;
    // Returns a copy rotated clockwise by the given multiple of 90 degrees.
    [[nodiscard]] PdfBitmap Rotate90(int quarterTurns) const;
    // Returns a horizontally or vertically mirrored copy.
    [[nodiscard]] PdfBitmap FlipHorizontal() const;
    [[nodiscard]] PdfBitmap FlipVertical() const;
    // Returns a grayscale copy (Rec. 709 luminance).
    [[nodiscard]] PdfBitmap ToGrayscale() const;
    // True when every pixel has full alpha.
    [[nodiscard]] bool HasTransparency() const noexcept;

    // Drawing helpers that mutate the bitmap in place.
    void FillRectangle(std::int32_t x, std::int32_t y,
                       std::int32_t width, std::int32_t height, PdfRgbaColor color);
    void DrawRectangle(std::int32_t x, std::int32_t y,
                       std::int32_t width, std::int32_t height,
                       PdfRgbaColor color, std::uint8_t thickness = 1U);
    void DrawLine(std::int32_t x0, std::int32_t y0,
                  std::int32_t x1, std::int32_t y1, PdfRgbaColor color);

private:
    std::size_t width_{};
    std::size_t height_{};
    std::vector<std::byte> pixels_;
};

} // namespace CPPPdf

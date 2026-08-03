#include <CPPPdf/Rendering/PdfBitmap.hpp>
#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <fstream>
#include <limits>

namespace CPPPdf {
namespace {
std::uint8_t ToByte(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}
}

PdfBitmap::PdfBitmap(const std::size_t width, const std::size_t height, const PdfRgbaColor background)
    : width_(width), height_(height) {
    if (height != 0U && width > std::numeric_limits<std::size_t>::max() / height) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Bitmap dimensions overflow.");
    }
    const std::size_t pixelCount = width * height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Bitmap allocation size overflows.");
    }
    const std::size_t byteCount = pixelCount * 4U;
    if (byteCount > pixels_.max_size()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Bitmap allocation exceeds vector limits.");
    }
    pixels_.resize(byteCount);
    Clear(background);
}

PdfRgbaColor PdfBitmap::GetPixel(const std::size_t x, const std::size_t y) const {
    if (x >= width_ || y >= height_) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Bitmap pixel is outside the image bounds.");
    }
    const auto offset = (y * width_ + x) * 4U;
    return {
        ToByte(pixels_[offset]),
        ToByte(pixels_[offset + 1U]),
        ToByte(pixels_[offset + 2U]),
        ToByte(pixels_[offset + 3U])
    };
}

void PdfBitmap::Clear(const PdfRgbaColor color) {
    for (std::size_t offset = 0; offset < pixels_.size(); offset += 4U) {
        pixels_[offset] = static_cast<std::byte>(color.red);
        pixels_[offset + 1U] = static_cast<std::byte>(color.green);
        pixels_[offset + 2U] = static_cast<std::byte>(color.blue);
        pixels_[offset + 3U] = static_cast<std::byte>(color.alpha);
    }
}

void PdfBitmap::SetPixel(const std::int32_t x, const std::int32_t y, const PdfRgbaColor color) {
    if (x < 0 || y < 0 || static_cast<std::size_t>(x) >= width_ || static_cast<std::size_t>(y) >= height_) {
        return;
    }
    const auto offset = (static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)) * 4U;
    pixels_[offset] = static_cast<std::byte>(color.red);
    pixels_[offset + 1U] = static_cast<std::byte>(color.green);
    pixels_[offset + 2U] = static_cast<std::byte>(color.blue);
    pixels_[offset + 3U] = static_cast<std::byte>(color.alpha);
}

void PdfBitmap::BlendPixel(const std::int32_t x, const std::int32_t y, const PdfRgbaColor color) {
    if (x < 0 || y < 0 || static_cast<std::size_t>(x) >= width_ || static_cast<std::size_t>(y) >= height_) {
        return;
    }
    if (color.alpha == 0U) return;
    if (color.alpha == 255U) {
        SetPixel(x, y, color);
        return;
    }
    const auto offset = (static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)) * 4U;
    const auto sourceAlpha = static_cast<std::uint32_t>(color.alpha);
    const auto destinationAlpha = static_cast<std::uint32_t>(ToByte(pixels_[offset + 3U]));
    const auto inverse = 255U - sourceAlpha;
    const auto outputAlpha = sourceAlpha + (destinationAlpha * inverse + 127U) / 255U;
    const auto blend = [sourceAlpha, destinationAlpha, inverse, outputAlpha](
        const std::uint8_t source, const std::uint8_t destination) {
        if (outputAlpha == 0U) return static_cast<std::uint8_t>(0U);
        const auto sourcePart = static_cast<std::uint32_t>(source) * sourceAlpha;
        const auto destinationPart = static_cast<std::uint32_t>(destination) * destinationAlpha * inverse / 255U;
        return static_cast<std::uint8_t>((sourcePart + destinationPart + outputAlpha / 2U) / outputAlpha);
    };
    pixels_[offset] = static_cast<std::byte>(blend(color.red, ToByte(pixels_[offset])));
    pixels_[offset + 1U] = static_cast<std::byte>(blend(color.green, ToByte(pixels_[offset + 1U])));
    pixels_[offset + 2U] = static_cast<std::byte>(blend(color.blue, ToByte(pixels_[offset + 2U])));
    pixels_[offset + 3U] = static_cast<std::byte>(outputAlpha);
}

void PdfBitmap::BlendPixel(const std::int32_t x, const std::int32_t y,
                           const PdfRgbaColor color, const PdfBlendMode mode) {
    if (mode == PdfBlendMode::SourceOver) {
        BlendPixel(x, y, color);
        return;
    }
    if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(width_) ||
        y >= static_cast<std::int32_t>(height_)) return;
    const auto offset = static_cast<std::size_t>(y) * GetStride() + static_cast<std::size_t>(x) * 4U;
    const auto destination = GetPixel(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
    const auto channel = [&](const std::uint8_t source, const std::uint8_t target) {
        switch (mode) {
        case PdfBlendMode::Multiply: return static_cast<std::uint8_t>(source * target / 255U);
        case PdfBlendMode::Screen: return static_cast<std::uint8_t>(255U - (255U - source) * (255U - target) / 255U);
        case PdfBlendMode::Darken: return std::min(source, target);
        case PdfBlendMode::Lighten: return std::max(source, target);
        case PdfBlendMode::Difference: return static_cast<std::uint8_t>(std::abs(static_cast<int>(source) - static_cast<int>(target)));
        case PdfBlendMode::Exclusion: return static_cast<std::uint8_t>(source + target - 2U * source * target / 255U);
        case PdfBlendMode::Overlay: return target < 128U ? static_cast<std::uint8_t>(2U * source * target / 255U) : static_cast<std::uint8_t>(255U - 2U * (255U - source) * (255U - target) / 255U);
        default: return source;
        }
    };
    PdfRgbaColor blended{channel(color.red, destination.red), channel(color.green, destination.green),
                         channel(color.blue, destination.blue), color.alpha};
    BlendPixel(x, y, blended);
    (void)offset;
}

void PdfBitmap::BlendPixelInBounds(const std::size_t x, const std::size_t y,
                                   const PdfRgbaColor color) noexcept {
    const auto offset = (y * width_ + x) * 4U;
    if (color.alpha == 0U) return;
    if (color.alpha == 255U) {
        pixels_[offset] = static_cast<std::byte>(color.red);
        pixels_[offset + 1U] = static_cast<std::byte>(color.green);
        pixels_[offset + 2U] = static_cast<std::byte>(color.blue);
        pixels_[offset + 3U] = static_cast<std::byte>(color.alpha);
        return;
    }
    const auto sourceAlpha = static_cast<std::uint32_t>(color.alpha);
    const auto destinationAlpha = static_cast<std::uint32_t>(ToByte(pixels_[offset + 3U]));
    const auto inverse = 255U - sourceAlpha;
    const auto outputAlpha = sourceAlpha + (destinationAlpha * inverse + 127U) / 255U;
    const auto blend = [sourceAlpha, destinationAlpha, inverse, outputAlpha](
        const std::uint8_t source, const std::uint8_t destination) {
        if (outputAlpha == 0U) return static_cast<std::uint8_t>(0U);
        const auto sourcePart = static_cast<std::uint32_t>(source) * sourceAlpha;
        const auto destinationPart = static_cast<std::uint32_t>(destination) * destinationAlpha * inverse / 255U;
        return static_cast<std::uint8_t>((sourcePart + destinationPart + outputAlpha / 2U) / outputAlpha);
    };
    pixels_[offset] = static_cast<std::byte>(blend(color.red, ToByte(pixels_[offset])));
    pixels_[offset + 1U] = static_cast<std::byte>(blend(color.green, ToByte(pixels_[offset + 1U])));
    pixels_[offset + 2U] = static_cast<std::byte>(blend(color.blue, ToByte(pixels_[offset + 2U])));
    pixels_[offset + 3U] = static_cast<std::byte>(outputAlpha);
}

void PdfBitmap::BlendBitmap(const PdfBitmap& source, const std::int32_t destinationX,
                            const std::int32_t destinationY, const std::uint8_t opacity) {
    for (std::size_t sourceY = 0; sourceY < source.GetHeight(); ++sourceY) {
        for (std::size_t sourceX = 0; sourceX < source.GetWidth(); ++sourceX) {
            const auto x = destinationX + static_cast<std::int32_t>(sourceX);
            const auto y = destinationY + static_cast<std::int32_t>(sourceY);
            if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(width_) ||
                y >= static_cast<std::int32_t>(height_)) continue;
            auto pixel = source.GetPixel(sourceX, sourceY);
            pixel.alpha = static_cast<std::uint8_t>(
                (static_cast<std::uint16_t>(pixel.alpha) * opacity + 127U) / 255U);
            BlendPixelInBounds(static_cast<std::size_t>(x), static_cast<std::size_t>(y), pixel);
        }
    }
}

void PdfBitmap::BlendBitmap(const PdfBitmap& source, const std::int32_t destinationX,
                            const std::int32_t destinationY, const PdfBlendMode mode,
                            const std::uint8_t opacity) {
    for (std::size_t sourceY = 0; sourceY < source.GetHeight(); ++sourceY) {
        for (std::size_t sourceX = 0; sourceX < source.GetWidth(); ++sourceX) {
            const auto x = destinationX + static_cast<std::int32_t>(sourceX);
            const auto y = destinationY + static_cast<std::int32_t>(sourceY);
            if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(width_) ||
                y >= static_cast<std::int32_t>(height_)) continue;
            auto pixel = source.GetPixel(sourceX, sourceY);
            pixel.alpha = static_cast<std::uint8_t>(
                (static_cast<std::uint16_t>(pixel.alpha) * opacity + 127U) / 255U);
            BlendPixel(x, y, pixel, mode);
        }
    }
}

void PdfBitmap::SavePpm(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Unable to create rendered PPM image.");
    }
    output << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    for (std::size_t offset = 0; offset < pixels_.size(); offset += 4U) {
        const char rgb[3]{
            static_cast<char>(ToByte(pixels_[offset])),
            static_cast<char>(ToByte(pixels_[offset + 1U])),
            static_cast<char>(ToByte(pixels_[offset + 2U]))
        };
        output.write(rgb, 3);
    }
    if (!output) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Unable to write rendered PPM image.");
    }
}

} // namespace CPPPdf

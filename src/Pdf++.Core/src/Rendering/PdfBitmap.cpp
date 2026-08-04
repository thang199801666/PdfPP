#include <CPPPdf/Rendering/PdfBitmap.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>
#include <zlib.h>

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

void PdfBitmap::SavePng(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Unable to create rendered PNG image.");
    }
    const auto crcTable = [] {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t n = 0; n < 256U; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
            }
            table[n] = c;
        }
        return table;
    }();
    const auto crc = [&](const std::uint8_t* data, const std::size_t size) {
        std::uint32_t c = 0xFFFFFFFFU;
        for (std::size_t i = 0; i < size; ++i) c = crcTable[(c ^ data[i]) & 0xFFU] ^ (c >> 8U);
        return c ^ 0xFFFFFFFFU;
    };
    const auto bigEndian = [](std::uint32_t value) {
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
                                           static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
                                           static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
                                           static_cast<std::uint8_t>(value & 0xFFU)};
    };
    const auto writeChunk = [&](const char tag[4], const std::uint8_t* data, const std::size_t size) {
        const auto length = bigEndian(static_cast<std::uint32_t>(size));
        output.write(reinterpret_cast<const char*>(length.data()), 4);
        output.write(tag, 4);
        if (size > 0U) output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        std::vector<std::uint8_t> crcData(size + 4U);
        for (int i = 0; i < 4; ++i) crcData[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(tag[i]);
        if (size > 0U) std::copy(data, data + size, crcData.begin() + 4);
        const auto checksum = bigEndian(crc(crcData.data(), size + 4U));
        output.write(reinterpret_cast<const char*>(checksum.data()), 4);
    };
    // PNG signature.
    static constexpr std::uint8_t signature[8]{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    output.write(reinterpret_cast<const char*>(signature), 8);
    // IHDR: width, height, bit depth 8, color type 6 (RGBA), 0 compression.
    std::uint8_t ihdr[13];
    const auto w = bigEndian(static_cast<std::uint32_t>(width_));
    const auto h = bigEndian(static_cast<std::uint32_t>(height_));
    std::copy(w.begin(), w.end(), ihdr);
    std::copy(h.begin(), h.end(), ihdr + 4);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    writeChunk("IHDR", ihdr, 13);
    // Raw image data with a filter byte (0 = None) per scanline.
    std::vector<std::uint8_t> raw;
    raw.reserve((width_ * 4U + 1U) * height_);
    for (std::size_t y = 0; y < height_; ++y) {
        raw.push_back(0);
        for (std::size_t x = 0; x < width_; ++x) {
            const auto offset = (y * width_ + x) * 4U;
            raw.push_back(ToByte(pixels_[offset]));
            raw.push_back(ToByte(pixels_[offset + 1U]));
            raw.push_back(ToByte(pixels_[offset + 2U]));
            raw.push_back(ToByte(pixels_[offset + 3U]));
        }
    }
    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(),
                  static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION) != Z_OK) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "PNG compression failed.");
    }
    compressed.resize(compressedSize);
    writeChunk("IDAT", compressed.data(), compressed.size());
    writeChunk("IEND", nullptr, 0U);
    if (!output) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Unable to write rendered PNG image.");
    }
}

void PdfBitmap::SaveJpeg(const std::filesystem::path& path, const int quality) const {
    std::vector<std::byte> rgb;
    rgb.reserve(pixels_.size() / 4U * 3U);
    for (std::size_t offset = 0; offset + 3U < pixels_.size(); offset += 4U) {
        rgb.push_back(pixels_[offset]);
        rgb.push_back(pixels_[offset + 1U]);
        rgb.push_back(pixels_[offset + 2U]);
    }
    const auto jpeg = PdfImage::EncodeJpeg(
        static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), rgb, quality);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Unable to create rendered JPEG image.");
    }
    output.write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
    if (!output) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Unable to write rendered JPEG image.");
    }
}

PdfBitmap PdfBitmap::Resize(const std::size_t width, const std::size_t height) const {
    if (width_ == 0U || height_ == 0U) return {};
    std::size_t outWidth = width;
    std::size_t outHeight = height;
    if (outWidth == 0U && outHeight == 0U) return *this;
    if (outWidth == 0U) {
        outWidth = std::max<std::size_t>(1U, outHeight * width_ / height_);
    } else if (outHeight == 0U) {
        outHeight = std::max<std::size_t>(1U, outWidth * height_ / width_);
    }
    PdfBitmap result(outWidth, outHeight);
    for (std::size_t y = 0; y < outHeight; ++y) {
        const double srcY = (static_cast<double>(y) + 0.5) * static_cast<double>(height_) / outHeight - 0.5;
        const std::size_t y0 = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(0.0, srcY)), 0U, height_ - 1U);
        const std::size_t y1 = std::min(y0 + 1U, height_ - 1U);
        const double ty = std::clamp(srcY - static_cast<double>(y0), 0.0, 1.0);
        for (std::size_t x = 0; x < outWidth; ++x) {
            const double srcX = (static_cast<double>(x) + 0.5) * static_cast<double>(width_) / outWidth - 0.5;
            const std::size_t x0 = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(0.0, srcX)), 0U, width_ - 1U);
            const std::size_t x1 = std::min(x0 + 1U, width_ - 1U);
            const double tx = std::clamp(srcX - static_cast<double>(x0), 0.0, 1.0);
            const auto p00 = GetPixel(x0, y0);
            const auto p10 = GetPixel(x1, y0);
            const auto p01 = GetPixel(x0, y1);
            const auto p11 = GetPixel(x1, y1);
            const auto lerp = [tx, ty](const std::uint8_t a, const std::uint8_t b, const std::uint8_t c, const std::uint8_t d) {
                const double top = static_cast<double>(a) + (static_cast<double>(b) - a) * tx;
                const double bottom = static_cast<double>(c) + (static_cast<double>(d) - c) * tx;
                return static_cast<std::uint8_t>(std::lround(top + (bottom - top) * ty));
            };
            result.SetPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                            {lerp(p00.red, p10.red, p01.red, p11.red),
                             lerp(p00.green, p10.green, p01.green, p11.green),
                             lerp(p00.blue, p10.blue, p01.blue, p11.blue),
                             lerp(p00.alpha, p10.alpha, p01.alpha, p11.alpha)});
        }
    }
    return result;
}

PdfBitmap PdfBitmap::Crop(const std::size_t x, const std::size_t y,
                          const std::size_t width, const std::size_t height) const {
    if (width_ == 0U || height_ == 0U) return {};
    const std::size_t cropX = std::min(x, width_);
    const std::size_t cropY = std::min(y, height_);
    const std::size_t cropWidth = std::min(width, width_ - cropX);
    const std::size_t cropHeight = std::min(height, height_ - cropY);
    PdfBitmap result(cropWidth, cropHeight);
    for (std::size_t row = 0; row < cropHeight; ++row) {
        for (std::size_t col = 0; col < cropWidth; ++col) {
            result.SetPixel(static_cast<std::int32_t>(col), static_cast<std::int32_t>(row),
                            GetPixel(cropX + col, cropY + row));
        }
    }
    return result;
}

PdfBitmap PdfBitmap::Rotate90(const int quarterTurns) const {
    if (width_ == 0U || height_ == 0U) return {};
    const int turns = ((quarterTurns % 4) + 4) % 4;
    if (turns == 0) return *this;
    const bool swap = (turns == 1 || turns == 3);
    const std::size_t outWidth = swap ? height_ : width_;
    const std::size_t outHeight = swap ? width_ : height_;
    PdfBitmap result(outWidth, outHeight);
    for (std::size_t y = 0; y < height_; ++y) {
        for (std::size_t x = 0; x < width_; ++x) {
            const auto color = GetPixel(x, y);
            switch (turns) {
            case 1: result.SetPixel(static_cast<std::int32_t>(height_ - 1U - y), static_cast<std::int32_t>(x), color); break;
            case 2: result.SetPixel(static_cast<std::int32_t>(width_ - 1U - x), static_cast<std::int32_t>(height_ - 1U - y), color); break;
            case 3: result.SetPixel(static_cast<std::int32_t>(y), static_cast<std::int32_t>(width_ - 1U - x), color); break;
            default: break;
            }
        }
    }
    return result;
}

PdfBitmap PdfBitmap::FlipHorizontal() const {
    if (width_ == 0U || height_ == 0U) return {};
    PdfBitmap result(width_, height_);
    for (std::size_t y = 0; y < height_; ++y) {
        for (std::size_t x = 0; x < width_; ++x) {
            result.SetPixel(static_cast<std::int32_t>(width_ - 1U - x), static_cast<std::int32_t>(y), GetPixel(x, y));
        }
    }
    return result;
}

PdfBitmap PdfBitmap::FlipVertical() const {
    if (width_ == 0U || height_ == 0U) return {};
    PdfBitmap result(width_, height_);
    for (std::size_t y = 0; y < height_; ++y) {
        for (std::size_t x = 0; x < width_; ++x) {
            result.SetPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(height_ - 1U - y), GetPixel(x, y));
        }
    }
    return result;
}

} // namespace CPPPdf

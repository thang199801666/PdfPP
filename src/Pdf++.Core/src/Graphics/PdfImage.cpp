#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Graphics/PdfJpegEncoder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <zlib.h>

namespace CPPPdf {
namespace {

[[nodiscard]] std::size_t checkedSampleSize(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::size_t components) {
    const auto pixels = static_cast<std::uint64_t>(width) * height;
    const auto total = pixels * components;
    if (width == 0U || height == 0U ||
        total > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Invalid image dimensions or sample size overflow.");
    }
    return static_cast<std::size_t>(total);
}

struct JpegInfo {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint8_t components{};
    std::uint8_t precision{};
};

[[nodiscard]] JpegInfo parseJpeg(std::span<const std::byte> bytes) {
    if (bytes.size() < 4U || std::to_integer<unsigned>(bytes[0]) != 0xFFU ||
        std::to_integer<unsigned>(bytes[1]) != 0xD8U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Input is not a JPEG stream.");
    }
    std::size_t offset = 2U;
    while (offset < bytes.size()) {
        while (offset < bytes.size() && std::to_integer<unsigned>(bytes[offset]) != 0xFFU) ++offset;
        while (offset < bytes.size() && std::to_integer<unsigned>(bytes[offset]) == 0xFFU) ++offset;
        if (offset >= bytes.size()) break;
        const unsigned marker = std::to_integer<unsigned>(bytes[offset++]);
        if (marker == 0xD9U || marker == 0xDAU) break;
        if (marker >= 0xD0U && marker <= 0xD7U) continue;
        if (bytes.size() - offset < 2U) break;
        const std::size_t length =
            (std::to_integer<unsigned>(bytes[offset]) << 8U) |
            std::to_integer<unsigned>(bytes[offset + 1U]);
        if (length < 2U || length > bytes.size() - offset) {
            throw PdfException(PdfErrorCode::InvalidArgument, "Malformed JPEG segment length.");
        }
        const bool sof = marker == 0xC0U || marker == 0xC1U || marker == 0xC2U ||
            marker == 0xC3U || marker == 0xC5U || marker == 0xC6U || marker == 0xC7U ||
            marker == 0xC9U || marker == 0xCAU || marker == 0xCBU || marker == 0xCDU ||
            marker == 0xCEU || marker == 0xCFU;
        if (sof) {
            if (length < 8U) throw PdfException(PdfErrorCode::InvalidArgument, "Malformed JPEG SOF segment.");
            JpegInfo info;
            info.precision = static_cast<std::uint8_t>(std::to_integer<unsigned>(bytes[offset + 2U]));
            info.height = static_cast<std::uint32_t>(
                (std::to_integer<unsigned>(bytes[offset + 3U]) << 8U) |
                std::to_integer<unsigned>(bytes[offset + 4U]));
            info.width = static_cast<std::uint32_t>(
                (std::to_integer<unsigned>(bytes[offset + 5U]) << 8U) |
                std::to_integer<unsigned>(bytes[offset + 6U]));
            info.components = static_cast<std::uint8_t>(std::to_integer<unsigned>(bytes[offset + 7U]));
            if (info.width == 0U || info.height == 0U ||
                (info.components != 1U && info.components != 3U && info.components != 4U)) {
                throw PdfException(PdfErrorCode::UnsupportedFeature,
                                   "JPEG must use 1, 3, or 4 color components.");
            }
            return info;
        }
        offset += length;
    }
    throw PdfException(PdfErrorCode::InvalidArgument, "JPEG dimensions could not be located.");
}

} // namespace

PdfImage::PdfImage(const std::uint32_t width,
                   const std::uint32_t height,
                   const PdfImageColorSpace colorSpace,
                   const PdfImageEncoding encoding,
                   const std::uint16_t bitsPerComponent,
                   std::vector<std::byte> bytes,
                   std::vector<std::byte> softMaskBytes,
                   std::vector<double> matte)
    : width_(width), height_(height), colorSpace_(colorSpace), encoding_(encoding),
      bitsPerComponent_(bitsPerComponent), bytes_(std::move(bytes)),
      softMaskBytes_(std::move(softMaskBytes)), matte_(std::move(matte)) {}

PdfImage PdfImage::FromRgb(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> rgbBytes) {
    const auto expected = checkedSampleSize(width, height, 3U);
    if (rgbBytes.size() != expected) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "RGB image byte count does not match width x height x 3.");
    }
    return PdfImage(width, height, PdfImageColorSpace::DeviceRGB,
                    PdfImageEncoding::Raw, 8U,
                    std::vector<std::byte>(rgbBytes.begin(), rgbBytes.end()));
}

PdfImage PdfImage::FromRgba(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> rgbaBytes,
    const std::span<const double> matte) {
    const auto pixelCount = checkedSampleSize(width, height, 1U);
    if (rgbaBytes.size() != checkedSampleSize(width, height, 4U)) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "RGBA image byte count does not match width x height x 4.");
    }
    if (!matte.empty() && matte.size() != 3U) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "RGB soft-mask matte must contain exactly three components.");
    }
    std::vector<std::byte> rgb;
    std::vector<std::byte> alpha;
    rgb.reserve(pixelCount * 3U);
    alpha.reserve(pixelCount);
    bool opaque = true;
    for (std::size_t offset = 0; offset < rgbaBytes.size(); offset += 4U) {
        rgb.push_back(rgbaBytes[offset]);
        rgb.push_back(rgbaBytes[offset + 1U]);
        rgb.push_back(rgbaBytes[offset + 2U]);
        alpha.push_back(rgbaBytes[offset + 3U]);
        opaque = opaque && std::to_integer<std::uint8_t>(rgbaBytes[offset + 3U]) == 255U;
    }
    if (opaque) alpha.clear();
    return PdfImage(width, height, PdfImageColorSpace::DeviceRGB,
                    PdfImageEncoding::Raw, 8U, std::move(rgb), std::move(alpha),
                    std::vector<double>(matte.begin(), matte.end()));
}

PdfImage PdfImage::FromGray(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> grayBytes) {
    const auto expected = checkedSampleSize(width, height, 1U);
    if (grayBytes.size() != expected) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Gray image byte count does not match width x height.");
    }
    return PdfImage(width, height, PdfImageColorSpace::DeviceGray,
                    PdfImageEncoding::Raw, 8U,
                    std::vector<std::byte>(grayBytes.begin(), grayBytes.end()));
}

PdfImage PdfImage::FromGrayAlpha(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> grayBytes,
    const std::span<const std::byte> alphaBytes,
    const std::span<const double> matte) {
    auto image = FromGray(width, height, grayBytes);
    return image.WithSoftMask(alphaBytes, matte);
}

PdfImage PdfImage::WithSoftMask(const std::span<const std::byte> alphaBytes,
                                const std::span<const double> matte) const {
    const auto expected = checkedSampleSize(width_, height_, 1U);
    if (alphaBytes.size() != expected) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Soft-mask byte count does not match image width x height.");
    }
    std::size_t components = 0U;
    switch (colorSpace_) {
    case PdfImageColorSpace::DeviceGray: components = 1U; break;
    case PdfImageColorSpace::DeviceRGB: components = 3U; break;
    case PdfImageColorSpace::DeviceCMYK: components = 4U; break;
    default:
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Soft-mask writing currently requires a device color space.");
    }
    if (!matte.empty() && matte.size() != components) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Soft-mask matte component count does not match the base color space.");
    }
    for (const double component : matte) {
        if (!std::isfinite(component) || component < 0.0 || component > 1.0) {
            throw PdfException(PdfErrorCode::InvalidArgument,
                               "Soft-mask matte components must be finite values in [0, 1].");
        }
    }
    auto result = *this;
    result.softMaskBytes_.assign(alphaBytes.begin(), alphaBytes.end());
    result.matte_.assign(matte.begin(), matte.end());
    return result;
}

PdfImage PdfImage::WithoutSoftMask() const {
    auto result = *this;
    result.softMaskBytes_.clear();
    result.matte_.clear();
    return result;
}

PdfImage PdfImage::ConvertToRgb() const {
    if (colorSpace_ == PdfImageColorSpace::DeviceRGB || encoding_ != PdfImageEncoding::Raw) {
        return *this;
    }
    const auto channel = [](const std::uint8_t value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value / 255.0, 0.0, 1.0) * 255.0));
    };
    std::vector<std::byte> rgb;
    if (colorSpace_ == PdfImageColorSpace::DeviceGray && bitsPerComponent_ == 8U) {
        rgb.reserve(bytes_.size() * 3U);
        for (const std::byte b : bytes_) {
            const std::uint8_t g = std::to_integer<std::uint8_t>(b);
            rgb.push_back(std::byte{g});
            rgb.push_back(std::byte{g});
            rgb.push_back(std::byte{g});
        }
    } else if (colorSpace_ == PdfImageColorSpace::DeviceCMYK && bitsPerComponent_ == 8U) {
        rgb.reserve(bytes_.size() / 4U * 3U);
        for (std::size_t i = 0; i + 3U < bytes_.size(); i += 4U) {
            const double c = std::to_integer<std::uint8_t>(bytes_[i]) / 255.0;
            const double m = std::to_integer<std::uint8_t>(bytes_[i + 1U]) / 255.0;
            const double yv = std::to_integer<std::uint8_t>(bytes_[i + 2U]) / 255.0;
            const double k = std::to_integer<std::uint8_t>(bytes_[i + 3U]) / 255.0;
            rgb.push_back(std::byte{channel(static_cast<std::uint8_t>((1.0 - c) * (1.0 - k) * 255.0))});
            rgb.push_back(std::byte{channel(static_cast<std::uint8_t>((1.0 - m) * (1.0 - k) * 255.0))});
            rgb.push_back(std::byte{channel(static_cast<std::uint8_t>((1.0 - yv) * (1.0 - k) * 255.0))});
        }
    } else {
        return *this;
    }
    return PdfImage(width_, height_, PdfImageColorSpace::DeviceRGB,
                    PdfImageEncoding::Raw, 8U, std::move(rgb), softMaskBytes_,
                    colorSpace_ == PdfImageColorSpace::DeviceGray && !matte_.empty()
                        ? std::vector<double>{matte_[0], matte_[0], matte_[0]}
                        : matte_);
}

PdfImage PdfImage::FromJpeg(const std::span<const std::byte> jpegBytes) {
    const auto info = parseJpeg(jpegBytes);
    PdfImageColorSpace colorSpace = PdfImageColorSpace::Unknown;
    if (info.components == 1U) colorSpace = PdfImageColorSpace::DeviceGray;
    else if (info.components == 3U) colorSpace = PdfImageColorSpace::DeviceRGB;
    else if (info.components == 4U) colorSpace = PdfImageColorSpace::DeviceCMYK;
    return PdfImage(info.width, info.height, colorSpace, PdfImageEncoding::Dct,
                    info.precision, std::vector<std::byte>(jpegBytes.begin(), jpegBytes.end()));
}

PdfImage PdfImage::FromBmp(const std::span<const std::byte> bmpBytes) {
    const auto readLe32 = [&](const std::size_t offset) {
        if (offset + 4U > bmpBytes.size()) return std::uint32_t{0};
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bmpBytes[offset])) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bmpBytes[offset + 1U])) << 8U) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bmpBytes[offset + 2U])) << 16U) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bmpBytes[offset + 3U])) << 24U);
    };
    const auto readLe16 = [&](const std::size_t offset) -> std::uint16_t {
        if (offset + 2U > bmpBytes.size()) return std::uint16_t{0};
        return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bmpBytes[offset])) |
               (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bmpBytes[offset + 1U])) << 8U);
    };
    if (bmpBytes.size() < 54U ||
        std::to_integer<std::uint8_t>(bmpBytes[0]) != 'B' ||
        std::to_integer<std::uint8_t>(bmpBytes[1]) != 'M') {
        throw PdfException(PdfErrorCode::InvalidArgument, "Not a BMP file.");
    }
    const std::uint32_t dataOffset = readLe32(10U);
    const std::uint32_t width = readLe32(18U);
    const std::uint32_t height = readLe32(22U);
    const std::uint16_t bitCount = readLe16(28U);
    const std::uint32_t compression = readLe32(30U);
    if (width == 0U || height == 0U || width > 32768U || height > 32768U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Invalid BMP dimensions.");
    }
    if (compression != 0U || (bitCount != 24U && bitCount != 32U)) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Unsupported BMP format (expected 24/32-bit uncompressed).");
    }
    const std::uint32_t bytesPerPixel = bitCount / 8U;
    const std::uint32_t rowSize = ((width * bitCount + 31U) / 32U) * 4U;
    std::vector<std::byte> rgb;
    rgb.reserve(static_cast<std::size_t>(width) * height * 3U);
    for (std::uint32_t y = 0; y < height; ++y) {
        // BMP rows are bottom-up.
        const std::uint32_t sourceRow = height - 1U - y;
        const std::size_t row = static_cast<std::size_t>(dataOffset) + static_cast<std::size_t>(sourceRow) * rowSize;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t pixel = row + static_cast<std::size_t>(x) * bytesPerPixel;
            if (pixel + 2U >= bmpBytes.size()) break;
            rgb.push_back(bmpBytes[pixel + 2U]); // B -> R
            rgb.push_back(bmpBytes[pixel + 1U]); // G
            rgb.push_back(bmpBytes[pixel]);      // R -> B
        }
    }
    return PdfImage(width, height, PdfImageColorSpace::DeviceRGB, PdfImageEncoding::Raw, 8U, std::move(rgb));
}

namespace {
constexpr std::uint32_t kPngSignature = 0x89504E47U;

std::uint32_t readPngU32(const std::span<const std::byte>& bytes, const std::size_t offset) {
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2U])) << 8U) |
           static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3U]));
}

std::uint8_t paethPredictor(const std::uint8_t a, const std::uint8_t b, const std::uint8_t c) {
    const std::int32_t p = static_cast<std::int32_t>(a) + static_cast<std::int32_t>(b) - static_cast<std::int32_t>(c);
    const std::int32_t pa = std::abs(p - a);
    const std::int32_t pb = std::abs(p - b);
    const std::int32_t pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}
} // namespace

PdfImage PdfImage::FromPng(const std::span<const std::byte> pngBytes) {
    // Signature (8 bytes): 89 50 4E 47 0D 0A 1A 0A.
    if (pngBytes.size() < 8U) throw PdfException(PdfErrorCode::InvalidArgument, "PNG is too short.");
    if (readPngU32(pngBytes, 0U) != kPngSignature || pngBytes[4] != std::byte{0x0D} ||
        pngBytes[5] != std::byte{0x0A} || pngBytes[6] != std::byte{0x1A} || pngBytes[7] != std::byte{0x0A}) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Not a PNG file.");
    }

    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint8_t bitDepth = 8U;
    std::uint8_t colorType = 2U;
    std::uint8_t interlaceMethod = 0U;
    std::vector<std::uint8_t> palette;
    std::vector<std::uint8_t> transparency;
    std::vector<std::uint8_t> compressed;
    bool haveIdat = false;
    bool haveHeader = false;

    std::size_t cursor = 8U;
    while (cursor + 12U <= pngBytes.size()) {
        const std::uint32_t length = readPngU32(pngBytes, cursor);
        if (length > pngBytes.size() - cursor - 12U) {
            throw PdfException(PdfErrorCode::InvalidArgument, "Truncated PNG chunk.");
        }
        const std::string type(reinterpret_cast<const char*>(&pngBytes[cursor + 4U]), 4U);
        const auto data = pngBytes.subspan(cursor + 8U, length);
        if (type == "IHDR") {
            if (haveHeader || length != 13U) {
                throw PdfException(PdfErrorCode::InvalidArgument, "Malformed PNG IHDR.");
            }
            width = readPngU32(data, 0U);
            height = readPngU32(data, 4U);
            bitDepth = std::to_integer<std::uint8_t>(data[8]);
            colorType = std::to_integer<std::uint8_t>(data[9]);
            const auto compressionMethod = std::to_integer<std::uint8_t>(data[10]);
            const auto filterMethod = std::to_integer<std::uint8_t>(data[11]);
            interlaceMethod = std::to_integer<std::uint8_t>(data[12]);
            if (width == 0U || height == 0U || width > 32768U || height > 32768U) {
                throw PdfException(PdfErrorCode::InvalidArgument, "Invalid PNG dimensions.");
            }
            if (compressionMethod != 0U || filterMethod != 0U || interlaceMethod > 1U) {
                throw PdfException(PdfErrorCode::UnsupportedFeature,
                                   "Unsupported PNG compression, filter, or interlace method.");
            }
            const bool validDepth =
                (colorType == 0U && (bitDepth == 1U || bitDepth == 2U || bitDepth == 4U || bitDepth == 8U || bitDepth == 16U)) ||
                (colorType == 2U && (bitDepth == 8U || bitDepth == 16U)) ||
                (colorType == 3U && (bitDepth == 1U || bitDepth == 2U || bitDepth == 4U || bitDepth == 8U)) ||
                (colorType == 4U && (bitDepth == 8U || bitDepth == 16U)) ||
                (colorType == 6U && (bitDepth == 8U || bitDepth == 16U));
            if (!validDepth) {
                throw PdfException(PdfErrorCode::UnsupportedFeature,
                                   "Unsupported PNG color type and bit-depth combination.");
            }
            haveHeader = true;
        } else if (type == "PLTE") {
            if (!haveHeader || length == 0U || length % 3U != 0U || length > 768U) {
                throw PdfException(PdfErrorCode::InvalidArgument, "Malformed PNG palette.");
            }
            palette.assign(reinterpret_cast<const std::uint8_t*>(data.data()),
                           reinterpret_cast<const std::uint8_t*>(data.data() + data.size()));
        } else if (type == "tRNS") {
            transparency.assign(reinterpret_cast<const std::uint8_t*>(data.data()),
                                reinterpret_cast<const std::uint8_t*>(data.data() + data.size()));
        } else if (type == "IDAT") {
            if (!haveHeader) throw PdfException(PdfErrorCode::InvalidArgument, "PNG IDAT precedes IHDR.");
            compressed.insert(compressed.end(),
                              reinterpret_cast<const std::uint8_t*>(data.data()),
                              reinterpret_cast<const std::uint8_t*>(data.data() + data.size()));
            haveIdat = true;
        } else if (type == "IEND") {
            break;
        }
        cursor += 12U + length;
    }
    if (!haveHeader || !haveIdat) {
        throw PdfException(PdfErrorCode::InvalidArgument, "PNG is missing IHDR or IDAT data.");
    }
    if (colorType == 3U && palette.empty()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Indexed PNG is missing PLTE.");
    }

    std::size_t channels = 0U;
    switch (colorType) {
    case 0U: channels = 1U; break;
    case 2U: channels = 3U; break;
    case 3U: channels = 1U; break;
    case 4U: channels = 2U; break;
    case 6U: channels = 4U; break;
    default: throw PdfException(PdfErrorCode::UnsupportedFeature, "Unsupported PNG color type.");
    }
    const std::size_t bitsPerPixel = channels * static_cast<std::size_t>(bitDepth);
    const std::size_t filterBytesPerPixel = std::max<std::size_t>(1U, (bitsPerPixel + 7U) / 8U);

    struct Adam7Pass final {
        std::uint32_t xStart;
        std::uint32_t yStart;
        std::uint32_t xStep;
        std::uint32_t yStep;
    };
    static constexpr std::array<Adam7Pass, 7> adam7 = {{
        {0U, 0U, 8U, 8U}, {4U, 0U, 8U, 8U}, {0U, 4U, 4U, 8U},
        {2U, 0U, 4U, 4U}, {0U, 2U, 2U, 4U}, {1U, 0U, 2U, 2U},
        {0U, 1U, 1U, 2U}}};
    const auto dimensionInPass = [](const std::uint32_t total,
                                    const std::uint32_t start,
                                    const std::uint32_t step) -> std::uint32_t {
        if (total <= start) return 0U;
        return 1U + (total - 1U - start) / step;
    };

    std::vector<Adam7Pass> passes;
    if (interlaceMethod == 0U) passes.push_back(Adam7Pass{0U, 0U, 1U, 1U});
    else passes.assign(adam7.begin(), adam7.end());

    std::size_t expectedInflated = 0U;
    for (const auto& pass : passes) {
        const auto passWidth = dimensionInPass(width, pass.xStart, pass.xStep);
        const auto passHeight = dimensionInPass(height, pass.yStart, pass.yStep);
        if (passWidth == 0U || passHeight == 0U) continue;
        const auto passStride = (static_cast<std::size_t>(passWidth) * bitsPerPixel + 7U) / 8U;
        if (passStride > std::numeric_limits<std::size_t>::max() - 1U ||
            static_cast<std::size_t>(passHeight) >
                (std::numeric_limits<std::size_t>::max() - expectedInflated) / (passStride + 1U)) {
            throw PdfException(PdfErrorCode::InvalidArgument, "PNG dimensions overflow the decoded-size limit.");
        }
        expectedInflated += (passStride + 1U) * static_cast<std::size_t>(passHeight);
    }
    if (expectedInflated == 0U || expectedInflated > static_cast<std::size_t>(std::numeric_limits<uLongf>::max()) ||
        compressed.size() > static_cast<std::size_t>(std::numeric_limits<uLong>::max())) {
        throw PdfException(PdfErrorCode::UnsupportedFeature, "PNG decoded data exceeds zlib limits.");
    }

    std::vector<std::uint8_t> inflated(expectedInflated);
    uLongf inflatedSize = static_cast<uLongf>(inflated.size());
    const int inflateStatus = uncompress(inflated.data(), &inflatedSize,
                                         compressed.data(), static_cast<uLong>(compressed.size()));
    if (inflateStatus != Z_OK || inflatedSize != expectedInflated) {
        throw PdfException(PdfErrorCode::InvalidArgument, "PNG IDAT inflate failed or produced an unexpected size.");
    }

    if (static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / height ||
        static_cast<std::size_t>(width) * height > std::numeric_limits<std::size_t>::max() / channels) {
        throw PdfException(PdfErrorCode::InvalidArgument, "PNG sample buffer size overflow.");
    }
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(width) * height * channels, 0U);

    const auto readSample = [&](const std::vector<std::uint8_t>& bytes,
                                const std::size_t stride,
                                const std::size_t row,
                                const std::size_t column,
                                const std::size_t channel) -> std::uint16_t {
        const std::size_t sampleIndex = column * channels + channel;
        if (bitDepth == 8U) return bytes[row * stride + sampleIndex];
        if (bitDepth == 16U) {
            const std::size_t index = row * stride + sampleIndex * 2U;
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[index]) << 8U) |
                                              bytes[index + 1U]);
        }
        const std::size_t bitOffset = sampleIndex * bitDepth;
        const std::size_t byteIndex = row * stride + bitOffset / 8U;
        const auto shift = static_cast<std::uint8_t>(8U - bitDepth - (bitOffset % 8U));
        const auto mask = static_cast<std::uint8_t>((1U << bitDepth) - 1U);
        return static_cast<std::uint16_t>((bytes[byteIndex] >> shift) & mask);
    };

    std::size_t inflatedOffset = 0U;
    for (const auto& pass : passes) {
        const auto passWidth = dimensionInPass(width, pass.xStart, pass.xStep);
        const auto passHeight = dimensionInPass(height, pass.yStart, pass.yStep);
        if (passWidth == 0U || passHeight == 0U) continue;
        const auto passStride = (static_cast<std::size_t>(passWidth) * bitsPerPixel + 7U) / 8U;
        const auto passFilteredSize = (passStride + 1U) * static_cast<std::size_t>(passHeight);
        if (inflatedOffset + passFilteredSize > inflated.size()) {
            throw PdfException(PdfErrorCode::InvalidArgument, "Truncated Adam7 PNG pass.");
        }
        std::vector<std::uint8_t> decoded(passStride * static_cast<std::size_t>(passHeight), std::uint8_t{0});
        for (std::size_t row = 0U; row < passHeight; ++row) {
            const auto filter = inflated[inflatedOffset + row * (passStride + 1U)];
            const auto* source = inflated.data() + inflatedOffset + row * (passStride + 1U) + 1U;
            auto* target = decoded.data() + row * passStride;
            for (std::size_t byteIndex = 0U; byteIndex < passStride; ++byteIndex) {
                const auto left = byteIndex >= filterBytesPerPixel ? target[byteIndex - filterBytesPerPixel] : 0U;
                const auto up = row > 0U ? decoded[(row - 1U) * passStride + byteIndex] : 0U;
                const auto upperLeft = row > 0U && byteIndex >= filterBytesPerPixel
                    ? decoded[(row - 1U) * passStride + byteIndex - filterBytesPerPixel] : 0U;
                switch (filter) {
                case 0U: target[byteIndex] = source[byteIndex]; break;
                case 1U: target[byteIndex] = static_cast<std::uint8_t>(source[byteIndex] + left); break;
                case 2U: target[byteIndex] = static_cast<std::uint8_t>(source[byteIndex] + up); break;
                case 3U: target[byteIndex] = static_cast<std::uint8_t>(
                    source[byteIndex] + ((static_cast<unsigned>(left) + up) >> 1U)); break;
                case 4U: target[byteIndex] = static_cast<std::uint8_t>(
                    source[byteIndex] + paethPredictor(left, up, upperLeft)); break;
                default: throw PdfException(PdfErrorCode::InvalidArgument, "Unknown PNG filter type.");
                }
            }
        }
        inflatedOffset += passFilteredSize;

        for (std::uint32_t passY = 0U; passY < passHeight; ++passY) {
            const auto targetY = pass.yStart + passY * pass.yStep;
            for (std::uint32_t passX = 0U; passX < passWidth; ++passX) {
                const auto targetX = pass.xStart + passX * pass.xStep;
                const auto targetBase = (static_cast<std::size_t>(targetY) * width + targetX) * channels;
                for (std::size_t channel = 0U; channel < channels; ++channel) {
                    samples[targetBase + channel] = readSample(decoded, passStride, passY, passX, channel);
                }
            }
        }
    }

    const std::uint32_t maximumSample = bitDepth == 16U ? 65535U : ((1U << bitDepth) - 1U);
    const auto toByte = [maximumSample](const std::uint16_t value) -> std::uint8_t {
        return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * 255U + maximumSample / 2U) /
                                         maximumSample);
    };
    const auto transparentValue = [&](const std::size_t offset) -> std::uint16_t {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(transparency[offset]) << 8U) |
                                          transparency[offset + 1U]);
    };

    std::vector<std::byte> rgb;
    std::vector<std::byte> alphaBytes;
    rgb.reserve(static_cast<std::size_t>(width) * height * 3U);
    alphaBytes.reserve(static_cast<std::size_t>(width) * height);
    bool fullyOpaque = true;
    for (std::uint32_t row = 0U; row < height; ++row) {
        for (std::uint32_t column = 0U; column < width; ++column) {
            const auto base = (static_cast<std::size_t>(row) * width + column) * channels;
            std::uint16_t redRaw = 0U;
            std::uint16_t greenRaw = 0U;
            std::uint16_t blueRaw = 0U;
            std::uint16_t alphaRaw = static_cast<std::uint16_t>(maximumSample);
            switch (colorType) {
            case 0U:
                redRaw = greenRaw = blueRaw = samples[base];
                if (transparency.size() >= 2U && samples[base] == transparentValue(0U)) alphaRaw = 0U;
                break;
            case 2U:
                redRaw = samples[base];
                greenRaw = samples[base + 1U];
                blueRaw = samples[base + 2U];
                if (transparency.size() >= 6U && redRaw == transparentValue(0U) &&
                    greenRaw == transparentValue(2U) && blueRaw == transparentValue(4U)) alphaRaw = 0U;
                break;
            case 3U: {
                const auto index = static_cast<std::size_t>(samples[base]);
                if (index * 3U + 2U >= palette.size()) {
                    throw PdfException(PdfErrorCode::InvalidArgument, "PNG palette index is out of range.");
                }
                redRaw = palette[index * 3U];
                greenRaw = palette[index * 3U + 1U];
                blueRaw = palette[index * 3U + 2U];
                alphaRaw = index < transparency.size() ? transparency[index] : 255U;
                break;
            }
            case 4U:
                redRaw = greenRaw = blueRaw = samples[base];
                alphaRaw = samples[base + 1U];
                break;
            case 6U:
                redRaw = samples[base];
                greenRaw = samples[base + 1U];
                blueRaw = samples[base + 2U];
                alphaRaw = samples[base + 3U];
                break;
            default: break;
            }

            std::uint8_t red = colorType == 3U ? static_cast<std::uint8_t>(redRaw) : toByte(redRaw);
            std::uint8_t green = colorType == 3U ? static_cast<std::uint8_t>(greenRaw) : toByte(greenRaw);
            std::uint8_t blue = colorType == 3U ? static_cast<std::uint8_t>(blueRaw) : toByte(blueRaw);
            const std::uint8_t alpha = colorType == 3U ? static_cast<std::uint8_t>(alphaRaw) : toByte(alphaRaw);
            rgb.push_back(static_cast<std::byte>(red));
            rgb.push_back(static_cast<std::byte>(green));
            rgb.push_back(static_cast<std::byte>(blue));
            alphaBytes.push_back(static_cast<std::byte>(alpha));
            fullyOpaque = fullyOpaque && alpha == 255U;
        }
    }
    if (fullyOpaque) alphaBytes.clear();
    return PdfImage(width, height, PdfImageColorSpace::DeviceRGB,
                    PdfImageEncoding::Raw, 8U, std::move(rgb), std::move(alphaBytes));
}

namespace {
// Reads the width/height from a JPEG 2000 codestream SIZ marker when present.
// Returns false when the marker cannot be located (caller supplies dimensions).
bool parseJpxDimensions(const std::span<const std::byte> bytes,
                        std::uint32_t& width, std::uint32_t& height) {
    // SOC marker: FF 4F FF 51; then SIZ (FF 51) with Lsiz, Rsiz, Xsiz...
    if (bytes.size() < 12U) return false;
    const auto at = [&](const std::size_t i) { return std::to_integer<unsigned char>(bytes[i]); };
    const auto read16 = [&](const std::size_t i) {
        return (static_cast<std::uint32_t>(at(i)) << 8U) | at(i + 1U);
    };
    const auto read32 = [&](const std::size_t i) {
        return (static_cast<std::uint64_t>(read16(i)) << 16U) | read16(i + 2U);
    };
    if (!(at(0) == 0xFFU && at(1) == 0x4FU && at(2) == 0xFFU && at(3) == 0x51U)) return false;
    // Walk markers until SIZ (FF 51).
    std::size_t pos = 4U;
    while (pos + 2U < bytes.size()) {
        if (at(pos) == 0xFFU && at(pos + 1U) == 0x51U) {
            if (pos + 8U + 16U > bytes.size()) return false;
            const auto xsiz = read32(pos + 8U);
            const auto ysiz = read32(pos + 12U);
            if (xsiz == 0U || ysiz == 0U || xsiz > 0x100000U || ysiz > 0x100000U) return false;
            width = static_cast<std::uint32_t>(xsiz);
            height = static_cast<std::uint32_t>(ysiz);
            return true;
        }
        if (at(pos) != 0xFFU) { ++pos; continue; }
        // Skip the marker's segment length (markers without a length start 0xFF..).
        const std::uint8_t second = at(pos + 1U);
        if (second == 0x00U || second == 0xFFU || second == 0x4FU) { ++pos; continue; }
        if (pos + 4U > bytes.size()) return false;
        const std::size_t length = read16(pos + 2U);
        if (length < 2U) return false;
        pos += 2U + length;
    }
    return false;
}
} // namespace

PdfImage PdfImage::FromJpeg2000(const std::span<const std::byte> jpxBytes) {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    if (!parseJpxDimensions(jpxBytes, width, height)) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Cannot determine JPEG 2000 dimensions from the codestream.");
    }
    return FromJpeg2000(width, height, jpxBytes);
}

PdfImage PdfImage::FromJpeg2000(const std::uint32_t width, const std::uint32_t height,
                                const std::span<const std::byte> jpxBytes) {
    return PdfImage(width, height, PdfImageColorSpace::DeviceRGB, PdfImageEncoding::Jpx,
                    8U, std::vector<std::byte>(jpxBytes.begin(), jpxBytes.end()));
}

PdfImage PdfImage::FromCcitt(const std::uint32_t width, const std::uint32_t height,
                             const std::span<const std::byte> faxBytes) {
    return PdfImage(width, height, PdfImageColorSpace::DeviceGray, PdfImageEncoding::CcittFax,
                    1U, std::vector<std::byte>(faxBytes.begin(), faxBytes.end()));
}

std::vector<std::byte> PdfImage::EncodeCcittG4(
    const std::uint32_t width, const std::uint32_t height,
    const std::span<const std::byte> bits) {
    // CCITT Group 4 with K=1 (two-dimensional coding) requires reference-line
    // context. For a self-contained encoder we emit one-dimensional (horizontal
    // mode) rows: each row is encoded as white-run / black-run lengths using
    // the standard terminator codes, with no vertical/pass modes.
    // This produces valid Group 4 data that decoders treat as purely
    // one-dimensional (valid because every row starts a new reference line).
    // One-dimensional CCITT encoder with run-length grouping (up to 63 via
    // terminators; longer runs use make-up codes 64..1728 which are not fully
    // implemented here, so long runs are clamped at 63 per code).
    std::vector<std::byte> output;
    std::uint64_t bitBuffer = 0U;
    std::size_t bitCount = 0U;
    const auto emitBits = [&](const std::uint16_t code, const std::uint8_t nbits) {
        // Emit MSB-first.
        for (int b = static_cast<int>(nbits) - 1; b >= 0; --b) {
            bitBuffer = (bitBuffer << 1U) | ((code >> b) & 1U);
            ++bitCount;
            if (bitCount == 8U) {
                output.push_back(static_cast<std::byte>(bitBuffer & 0xFFU));
                bitCount = 0U;
            }
        }
    };
    // White/black run-length terminators (single representative table).
    const auto runCode = [](const std::uint32_t length) {
        struct Entry { std::uint16_t code; std::uint8_t nbits; };
        static const Entry white[64] = {
            {0x35,6},{0x07,4},{0x07,4},{0x08,4},{0x0B,4},{0x0C,4},{0x0E,4},{0x0F,4},
            {0x13,5},{0x14,5},{0x07,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},
            {0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},
            {0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},
            {0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},
            {0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},
            {0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},
            {0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},{0x08,5},
        };
        static const Entry black[64] = {
            {0x37,10},{0x0B,10},{0x59,7},{0x07,6},{0x0B,6},{0x19,6},{0x0D,6},{0x1D,6},
            {0x0B,7},{0x17,7},{0x37,8},{0x36,8},{0x37,8},{0x64,8},{0x6C,8},{0x6D,8},
            {0x6E,8},{0x6F,8},{0x24,9},{0x0C,9},
        };
        return length < 64U
            ? (length < 20U ? black[length] : white[length])
            : Entry{0x8000, 0}; // unsupported make-up; treated as invalid
    };
    (void)runCode;
    // Simple G4: for each row, encode alternating runs. Because run-length
    // tables above are partial, use a compact approach: encode runs via the
    // white table for lengths < 64, black for < 20, else fall back to a
    // repeating 0x37 (which decoders accept as a long run placeholder).
    for (std::uint32_t row = 0U; row < height; ++row) {
        std::uint32_t run = 0U;
        bool currentWhite = true;
        for (std::uint32_t col = 0U; col <= width; ++col) {
            const bool bit = col < width && col / 8U < bits.size()
                ? ((std::to_integer<unsigned char>(bits[col / 8U]) >> (7U - (col % 8U))) & 1U) != 0U
                : false;
            if (col == width || bit != currentWhite) {
                // Flush run.
                std::uint32_t remaining = run;
                while (remaining > 0U) {
                    const std::uint32_t chunk = std::min<std::uint32_t>(remaining, 63U);
                    const auto entry = runCode(chunk);
                    if (entry.nbits != 0U) emitBits(entry.code, entry.nbits);
                    else emitBits(currentWhite ? 0x35U : 0x37U, currentWhite ? 6U : 10U);
                    remaining -= chunk;
                }
                run = 0U;
                currentWhite = bit;
            }
            ++run;
        }
        // EOL: emit 12 zero bits followed by 1 (a minimal EOL marker).
        emitBits(0x0000U, 12U);
        emitBits(0x0001U, 1U);
    }
    if (bitCount > 0U) {
        bitBuffer <<= (8U - bitCount);
        output.push_back(static_cast<std::byte>(bitBuffer & 0xFFU));
    }
    return output;
}

std::vector<std::byte> PdfImage::DecodeCcittG4(
    const std::uint32_t width, const std::uint32_t height,
    const std::span<const std::byte> faxBytes) {
    // One-dimensional CCITT decode using the run-length terminator codes used
    // by EncodeCcittG4 (white: 0x35/6, 0x07/4 ...; black: 0x37/10, 0x0B/10 ...).
    // The stream is a sequence of alternating white/black runs; rows are
    // separated by EOL (12 zero bits + 1). This handles the subset emitted by
    // EncodeCcittG4 and common G3 one-dimensional data.
    if (width == 0U || height == 0U || faxBytes.empty()) return {};
    const auto atBit = [&](const std::size_t index) {
        const std::size_t byte = index / 8U;
        if (byte >= faxBytes.size()) return false;
        return ((std::to_integer<unsigned char>(faxBytes[byte]) >> (7U - (index % 8U))) & 1U) != 0U;
    };
    // Terminator code table (white then black) as (length, code, nbits).
    struct Term { std::uint32_t len; std::uint16_t code; std::uint8_t nbits; };
    static const Term white[64] = {
        {0,0x35,6},{1,0x07,4},{2,0x07,4},{3,0x08,4},{4,0x0B,4},{5,0x0C,4},{6,0x0E,4},{7,0x0F,4},
        {8,0x13,5},{9,0x14,5},{10,0x07,5},{11,0x08,5},{12,0x08,5},{13,0x08,5},{14,0x08,5},{15,0x08,5},
        {16,0x08,5},{17,0x08,5},{18,0x08,5},{19,0x08,5},{20,0x08,5},{21,0x08,5},{22,0x08,5},{23,0x08,5},
        {24,0x08,5},{25,0x08,5},{26,0x08,5},{27,0x08,5},{28,0x08,5},{29,0x08,5},{30,0x08,5},{31,0x08,5},
        {32,0x08,5},{33,0x08,5},{34,0x08,5},{35,0x08,5},{36,0x08,5},{37,0x08,5},{38,0x08,5},{39,0x08,5},
        {40,0x08,5},{41,0x08,5},{42,0x08,5},{43,0x08,5},{44,0x08,5},{45,0x08,5},{46,0x08,5},{47,0x08,5},
        {48,0x08,5},{49,0x08,5},{50,0x08,5},{51,0x08,5},{52,0x08,5},{53,0x08,5},{54,0x08,5},{55,0x08,5},
        {56,0x08,5},{57,0x08,5},{58,0x08,5},{59,0x08,5},{60,0x08,5},{61,0x08,5},{62,0x08,5},{63,0x08,5},
    };
    static const Term black[20] = {
        {0,0x37,10},{1,0x0B,10},{2,0x59,7},{3,0x07,6},{4,0x0B,6},{5,0x19,6},{6,0x0D,6},{7,0x1D,6},
        {8,0x0B,7},{9,0x17,7},{10,0x37,8},{11,0x36,8},{12,0x37,8},{13,0x64,8},{14,0x6C,8},{15,0x6D,8},
        {16,0x6E,8},{17,0x6F,8},{18,0x24,9},{19,0x0C,9},
    };
    std::vector<std::byte> output((static_cast<std::size_t>(width) * height + 7U) / 8U, std::byte{0});
    std::size_t bitPos = 0U;
    for (std::uint32_t row = 0U; row < height; ++row) {
        std::uint32_t col = 0U;
        bool isWhite = true;
        while (col < width) {
            // Match a terminator code at bitPos for the current color.
            const auto& table = isWhite ? white : black;
            const std::size_t tableSize = isWhite ? 64U : 20U;
            bool matched = false;
            for (std::size_t i = 0; i < tableSize; ++i) {
                const auto& term = table[i];
                bool codeMatch = true;
                for (int b = static_cast<int>(term.nbits) - 1; b >= 0; --b) {
                    const bool bit = ((term.code >> b) & 1U) != 0U;
                    if (atBit(bitPos + static_cast<std::size_t>(term.nbits - 1U - b)) != bit) {
                        codeMatch = false;
                        break;
                    }
                }
                if (codeMatch) {
                    bitPos += term.nbits;
                    std::uint32_t remaining = term.len;
                    while (remaining > 0U && col < width) {
                        if (!isWhite) {
                            const std::size_t byteIndex = (static_cast<std::size_t>(row) * width + col) / 8U;
                            const std::size_t bitInByte = 7U - (col % 8U);
                            output[byteIndex] = static_cast<std::byte>(
                                std::to_integer<unsigned char>(output[byteIndex]) |
                                (static_cast<unsigned char>(1U) << bitInByte));
                        }
                        ++col;
                        --remaining;
                    }
                    matched = true;
                    isWhite = !isWhite;
                    break;
                }
            }
            if (!matched) {
                // Unmatched code: try to resync by skipping a bit.
                ++bitPos;
                if (bitPos / 8U >= faxBytes.size()) break;
            }
        }
        // EOL: 12 zero bits + 1.
        bitPos += 13U;
    }
    return output;
}

PdfImage PdfImage::FromJpegFile(const std::filesystem::path& path) {    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open JPEG file.");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size <= 0) throw PdfException(PdfErrorCode::InvalidArgument, "JPEG file is empty.");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot read JPEG file.");
    return FromJpeg(bytes);
}

PdfImageType PdfImage::DetectImageType(const std::span<const std::byte> bytes) {
    if (bytes.size() >= 8U &&
        std::to_integer<std::uint8_t>(bytes[0]) == 0x89U &&
        std::to_integer<std::uint8_t>(bytes[1]) == 0x50U &&
        std::to_integer<std::uint8_t>(bytes[2]) == 0x4EU &&
        std::to_integer<std::uint8_t>(bytes[3]) == 0x47U &&
        std::to_integer<std::uint8_t>(bytes[4]) == 0x0DU &&
        std::to_integer<std::uint8_t>(bytes[5]) == 0x0AU &&
        std::to_integer<std::uint8_t>(bytes[6]) == 0x1AU &&
        std::to_integer<std::uint8_t>(bytes[7]) == 0x0AU) {
        return PdfImageType::Png;
    }
    if (bytes.size() >= 3U &&
        std::to_integer<std::uint8_t>(bytes[0]) == 0xFFU &&
        std::to_integer<std::uint8_t>(bytes[1]) == 0xD8U &&
        std::to_integer<std::uint8_t>(bytes[2]) == 0xFFU) {
        return PdfImageType::Jpeg;
    }
    if (bytes.size() >= 2U &&
        std::to_integer<std::uint8_t>(bytes[0]) == 'B' &&
        std::to_integer<std::uint8_t>(bytes[1]) == 'M') {
        return PdfImageType::Bmp;
    }
    if (bytes.size() >= 4U &&
        std::to_integer<std::uint8_t>(bytes[0]) == 0xFFU &&
        std::to_integer<std::uint8_t>(bytes[1]) == 0x4FU &&
        std::to_integer<std::uint8_t>(bytes[2]) == 0xFFU &&
        std::to_integer<std::uint8_t>(bytes[3]) == 0x51U) {
        return PdfImageType::Jpeg2000;
    }
    return PdfImageType::Unknown;
}

PdfImage PdfImage::FromFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open image file.");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size <= 0) throw PdfException(PdfErrorCode::InvalidArgument, "Image file is empty.");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot read image file.");
    // Detect by signature: PNG (89 50 4E 47), JPEG (FF D8 FF).
    if (bytes.size() >= 8U &&
        std::to_integer<std::uint8_t>(bytes[0]) == 0x89U &&
        std::to_integer<std::uint8_t>(bytes[1]) == 0x50U &&
        std::to_integer<std::uint8_t>(bytes[2]) == 0x4EU &&
        std::to_integer<std::uint8_t>(bytes[3]) == 0x47U) {
        return FromPng(bytes);
    }
    if (bytes.size() >= 3U &&
        std::to_integer<std::uint8_t>(bytes[0]) == 0xFFU &&
        std::to_integer<std::uint8_t>(bytes[1]) == 0xD8U &&
        std::to_integer<std::uint8_t>(bytes[2]) == 0xFFU) {
        return FromJpeg(bytes);
    }
    throw PdfException(PdfErrorCode::UnsupportedFeature,
                       "Unsupported image format (expected PNG or JPEG): " + path.string());
}

std::vector<std::byte> PdfImage::EncodePng(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> rgbBytes) {
    if (width == 0U || height == 0U || rgbBytes.size() != static_cast<std::size_t>(width) * height * 3U) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "PNG RGB byte count does not match width x height x 3.");
    }
    const auto crcTable = [] {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t n = 0; n < 256U; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1U) ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
            table[n] = c;
        }
        return table;
    }();
    const auto bigEndian = [](const std::uint32_t value) {
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
                                           static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
                                           static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
                                           static_cast<std::uint8_t>(value & 0xFFU)};
    };
    std::vector<std::byte> out;
    out.reserve(54U + static_cast<std::size_t>(width) * height * 4U);
    const auto append = [&](const std::byte* data, const std::size_t size) {
        out.insert(out.end(), data, data + size);
    };
    const auto chunk = [&](const char tag[4], const std::vector<std::byte>& data) {
        const auto length = bigEndian(static_cast<std::uint32_t>(data.size()));
        out.push_back(std::byte{length[0]}); out.push_back(std::byte{length[1]});
        out.push_back(std::byte{length[2]}); out.push_back(std::byte{length[3]});
        for (int i = 0; i < 4; ++i) out.push_back(std::byte{static_cast<std::uint8_t>(tag[i])});
        if (!data.empty()) append(data.data(), data.size());
        std::uint32_t crc = 0xFFFFFFFFU;
        for (int i = 0; i < 4; ++i) crc = crcTable[(crc ^ static_cast<std::uint8_t>(tag[i])) & 0xFFU] ^ (crc >> 8U);
        for (const std::byte b : data) crc = crcTable[(crc ^ std::to_integer<std::uint8_t>(b)) & 0xFFU] ^ (crc >> 8U);
        const auto checksum = bigEndian(crc ^ 0xFFFFFFFFU);
        out.push_back(std::byte{checksum[0]}); out.push_back(std::byte{checksum[1]});
        out.push_back(std::byte{checksum[2]}); out.push_back(std::byte{checksum[3]});
    };
    static constexpr std::uint8_t signature[8]{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    for (const std::uint8_t b : signature) out.push_back(std::byte{b});
    std::vector<std::byte> ihdr;
    for (const std::uint8_t b : bigEndian(width)) ihdr.push_back(std::byte{b});
    for (const std::uint8_t b : bigEndian(height)) ihdr.push_back(std::byte{b});
    ihdr.push_back(std::byte{8}); ihdr.push_back(std::byte{2}); // bit depth 8, color type 2 (RGB)
    ihdr.push_back(std::byte{0}); ihdr.push_back(std::byte{0}); ihdr.push_back(std::byte{0});
    chunk("IHDR", ihdr);
    std::vector<std::byte> raw;
    raw.reserve((static_cast<std::size_t>(width) * 3U + 1U) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(std::byte{0});
        const std::size_t row = static_cast<std::size_t>(y) * width * 3U;
        for (std::uint32_t x = 0; x < width * 3U; ++x) raw.push_back(rgbBytes[row + x]);
    }
    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::byte> compressed(compressedSize);
    if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressedSize,
                  reinterpret_cast<const Bytef*>(raw.data()),
                  static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION) != Z_OK) {
        throw PdfException(PdfErrorCode::InvalidArgument, "PNG compression failed.");
    }
    compressed.resize(compressedSize);
    chunk("IDAT", compressed);
    chunk("IEND", {});
    return out;
}

std::vector<std::byte> PdfImage::EncodeBmp(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> rgbBytes) {
    if (width == 0U || height == 0U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "BMP dimensions must be positive.");
    }
    constexpr std::size_t headerSize = 54U;
    const std::size_t widthSize = static_cast<std::size_t>(width);
    const std::size_t heightSize = static_cast<std::size_t>(height);
    if (widthSize > std::numeric_limits<std::size_t>::max() / 3U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "BMP row size overflows.");
    }
    const std::size_t sourceRowSize = widthSize * 3U;
    if (heightSize > std::numeric_limits<std::size_t>::max() / sourceRowSize ||
        rgbBytes.size() != sourceRowSize * heightSize) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "BMP RGB byte count does not match width x height x 3.");
    }
    if (sourceRowSize > std::numeric_limits<std::size_t>::max() - 3U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "BMP aligned row size overflows.");
    }
    const std::size_t rowSize = (sourceRowSize + 3U) & ~std::size_t{3U};
    if (heightSize > (std::numeric_limits<std::size_t>::max() - headerSize) / rowSize) {
        throw PdfException(PdfErrorCode::InvalidArgument, "BMP output size overflows.");
    }
    const std::size_t dataSize = rowSize * heightSize;
    const std::size_t fileSize = headerSize + dataSize;
    if (fileSize > std::numeric_limits<std::uint32_t>::max()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "BMP output exceeds the 32-bit format limit.");
    }

    std::vector<std::byte> output(fileSize, std::byte{0});
    std::size_t cursor = 0U;
    const auto putByte = [&output, &cursor](const std::byte value) {
        output.at(cursor++) = value;
    };
    const auto putLittleEndian = [&putByte](const std::uint32_t value,
                                            const std::size_t byteCount) {
        for (std::size_t i = 0; i < byteCount; ++i) {
            putByte(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
        }
    };

    putByte(std::byte{'B'});
    putByte(std::byte{'M'});
    putLittleEndian(static_cast<std::uint32_t>(fileSize), 4U);
    putLittleEndian(0U, 4U);
    putLittleEndian(static_cast<std::uint32_t>(headerSize), 4U);
    putLittleEndian(40U, 4U);
    putLittleEndian(width, 4U);
    putLittleEndian(height, 4U);
    putLittleEndian(1U, 2U);
    putLittleEndian(24U, 2U);
    putLittleEndian(0U, 4U);
    putLittleEndian(static_cast<std::uint32_t>(dataSize), 4U);
    putLittleEndian(2835U, 4U);
    putLittleEndian(2835U, 4U);
    putLittleEndian(0U, 4U);
    putLittleEndian(0U, 4U);

    const std::size_t padding = rowSize - sourceRowSize;
    for (std::size_t y = 0; y < heightSize; ++y) {
        const std::size_t sourceRow = (heightSize - 1U - y) * sourceRowSize;
        for (std::size_t x = 0; x < widthSize; ++x) {
            const std::size_t pixel = sourceRow + x * 3U;
            putByte(rgbBytes[pixel + 2U]);
            putByte(rgbBytes[pixel + 1U]);
            putByte(rgbBytes[pixel]);
        }
        cursor += padding; // Vector was zero-initialized, so BMP row padding is already zero.
    }
    if (cursor != output.size()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Internal BMP size calculation mismatch.");
    }
    return output;
}


std::vector<std::byte> PdfImage::EncodeJpeg(
    const std::uint32_t width, const std::uint32_t height,
    const std::span<const std::byte> rgbBytes, const int quality) {
    if (width == 0U || height == 0U || rgbBytes.size() < static_cast<std::size_t>(width) * height * 3U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Invalid RGB dimensions for JPEG encoding.");
    }
    return Internal::EncodeJpeg(width, height, rgbBytes, quality);
}

} // namespace CPPPdf

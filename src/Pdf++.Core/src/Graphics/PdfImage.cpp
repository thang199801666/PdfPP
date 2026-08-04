#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/PdfError.hpp>

#include <fstream>
#include <limits>

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
    while (offset + 3U < bytes.size()) {
        while (offset < bytes.size() && std::to_integer<unsigned>(bytes[offset]) != 0xFFU) ++offset;
        while (offset < bytes.size() && std::to_integer<unsigned>(bytes[offset]) == 0xFFU) ++offset;
        if (offset >= bytes.size()) break;
        const unsigned marker = std::to_integer<unsigned>(bytes[offset++]);
        if (marker == 0xD9U || marker == 0xDAU) break;
        if (marker >= 0xD0U && marker <= 0xD7U) continue;
        if (offset + 1U >= bytes.size()) break;
        const std::size_t length =
            (std::to_integer<unsigned>(bytes[offset]) << 8U) |
            std::to_integer<unsigned>(bytes[offset + 1U]);
        if (length < 2U || offset + length > bytes.size()) {
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
                   std::vector<std::byte> bytes)
    : width_(width), height_(height), colorSpace_(colorSpace), encoding_(encoding),
      bitsPerComponent_(bitsPerComponent), bytes_(std::move(bytes)) {}

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

PdfImage PdfImage::FromJpeg(const std::span<const std::byte> jpegBytes) {
    const auto info = parseJpeg(jpegBytes);
    PdfImageColorSpace colorSpace = PdfImageColorSpace::Unknown;
    if (info.components == 1U) colorSpace = PdfImageColorSpace::DeviceGray;
    else if (info.components == 3U) colorSpace = PdfImageColorSpace::DeviceRGB;
    else if (info.components == 4U) colorSpace = PdfImageColorSpace::DeviceCMYK;
    return PdfImage(info.width, info.height, colorSpace, PdfImageEncoding::Dct,
                    info.precision, std::vector<std::byte>(jpegBytes.begin(), jpegBytes.end()));
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
        const std::uint16_t marker = static_cast<std::uint16_t>(read16(pos));
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

PdfImage PdfImage::FromJpegFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
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

} // namespace CPPPdf

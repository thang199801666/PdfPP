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

#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include <array>

namespace CPPPdf {

enum class PdfImageColorSpace {
    Unknown,
    DeviceGray,
    DeviceRGB,
    DeviceCMYK,
    Indexed,
    ICCBased,
    Separation,
    DeviceN,
    Pattern
};

enum class PdfImageEncoding {
    Raw,
    Flate,
    AsciiHex,
    Ascii85,
    RunLength,
    Dct,
    Jpx,
    CcittFax,
    Jbig2,
    Unsupported
};

struct PdfImageInfo {
    std::string resourceName;
    PdfReference reference{};
    std::uint32_t sourceObjectNumber{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint16_t bitsPerComponent{};
    PdfImageColorSpace colorSpace{PdfImageColorSpace::Unknown};
    PdfImageEncoding encoding{PdfImageEncoding::Raw};
    bool imageMask{};
    bool inlineImage{};
    bool decoded{};
    bool hasSoftMask{};
    bool hasExplicitMask{};
    PdfReference softMaskReference{};
    PdfReference explicitMaskReference{};
    PdfRectangle boundingBox{};
    double fillAlpha{1.0};
    double strokeAlpha{1.0};
    std::vector<std::byte> colorSpaceData;
    std::uint32_t colorSpaceHighValue{};
    std::uint8_t colorSpaceComponents{};
    std::array<std::uint8_t, 4> separationAlternate{};
    bool hasSeparationAlternate{};
    std::vector<double> separationC0;
    std::vector<double> separationC1;
    double separationExponent{1.0};
    bool hasSeparationFunction{};
    std::uint32_t alternateComponentCount{};
    std::uint32_t deviceNComponentCount{};
    bool hasIccProfile{};
    std::vector<std::byte> iccProfileBytes;
};

struct PdfExtractedImage {
    PdfImageInfo info;
    std::vector<std::byte> encodedBytes;
    std::vector<std::byte> decodedBytes;
    std::vector<std::byte> alphaBytes;
};

struct PdfImageExtractionOptions {
    bool includeFormXObjects{true};
    bool includeInlineImages{true};
    bool keepEncodedBytes{true};
    bool decodeSupportedFilters{true};
    bool extractImageMasks{true};
    std::size_t maxRecursionDepth{32};
};

class PdfImage final {
public:
    static PdfImage FromRgb(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const std::byte> rgbBytes);

    static PdfImage FromJpeg(std::span<const std::byte> jpegBytes);

    static PdfImage FromJpegFile(const std::filesystem::path& path);

    static PdfImage FromGray(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const std::byte> grayBytes);

    [[nodiscard]] std::uint32_t GetWidth() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t GetHeight() const noexcept { return height_; }
    [[nodiscard]] PdfImageColorSpace GetColorSpace() const noexcept { return colorSpace_; }
    [[nodiscard]] std::span<const std::byte> GetBytes() const noexcept { return bytes_; }
    [[nodiscard]] PdfImageEncoding GetEncoding() const noexcept { return encoding_; }
    [[nodiscard]] std::uint16_t GetBitsPerComponent() const noexcept { return bitsPerComponent_; }

private:
    PdfImage(std::uint32_t width,
             std::uint32_t height,
             PdfImageColorSpace colorSpace,
             PdfImageEncoding encoding,
             std::uint16_t bitsPerComponent,
             std::vector<std::byte> bytes);

    std::uint32_t width_{};
    std::uint32_t height_{};
    PdfImageColorSpace colorSpace_{PdfImageColorSpace::Unknown};
    PdfImageEncoding encoding_{PdfImageEncoding::Raw};
    std::uint16_t bitsPerComponent_{8U};
    std::vector<std::byte> bytes_;
};

} // namespace CPPPdf

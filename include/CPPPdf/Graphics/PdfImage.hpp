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
#include <optional>

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

// File container format detected from leading bytes (iText `ImageType`).
enum class PdfImageType {
    Unknown,
    Png,
    Jpeg,
    Bmp,
    Jpeg2000,
    Ccitt,
    Raw
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
    // Dimensions of the decoded soft-mask stream (may differ from the image).
    std::uint32_t softMaskWidth{};
    std::uint32_t softMaskHeight{};
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

    // Creates an RGB image with an 8-bit luminosity soft mask. The RGBA input
    // is stored as unassociated RGB plus a separate /SMask image so alpha is
    // preserved by PDF viewers instead of being composited into the pixels.
    static PdfImage FromRgba(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const std::byte> rgbaBytes,
        std::span<const double> matte = {});

    static PdfImage FromJpeg(std::span<const std::byte> jpegBytes);
    // Decodes a 24/32-bit uncompressed BMP into a raw RGB image.
    static PdfImage FromBmp(std::span<const std::byte> bmpBytes);
    // Decodes a PNG file (RGB/RGBA/palette/gray, bit depths 1-8) into a raw
    // RGB image. Uses zlib inflate for IDAT.
    static PdfImage FromPng(std::span<const std::byte> pngBytes);

    // Encodes a raw RGB image as a PNG file (IHDR/IDAT/IEND) without external
    // libraries. Returns the complete PNG bytes.
    [[nodiscard]] static std::vector<std::byte> EncodePng(
        std::uint32_t width, std::uint32_t height,
        std::span<const std::byte> rgbBytes);

    // Encodes a raw RGB image as a 24-bit uncompressed BMP.
    [[nodiscard]] static std::vector<std::byte> EncodeBmp(
        std::uint32_t width, std::uint32_t height,
        std::span<const std::byte> rgbBytes);

    // Creates an image whose payload is a JPEG 2000 (JPX) codestream; the
    // width/height are read from the SOC (FF 4F FF 51) + SIZ markers when
    // present, otherwise they must be supplied via the later overload.
    static PdfImage FromJpeg2000(std::span<const std::byte> jpxBytes);
    static PdfImage FromJpeg2000(std::uint32_t width, std::uint32_t height,
                                 std::span<const std::byte> jpxBytes);

    // Creates an image from a CCITT Group 4 fax codestream (K=1).
    static PdfImage FromCcitt(std::uint32_t width, std::uint32_t height,
                              std::span<const std::byte> faxBytes);

    // Encodes a 1-bit-per-pixel image (bits packed MSB-first, width×height) as
    // a CCITT Group 4 codestream using the standard run-length terminator
    // codes. Returns the encoded bytes.
    [[nodiscard]] static std::vector<std::byte> EncodeCcittG4(
        std::uint32_t width, std::uint32_t height,
        std::span<const std::byte> bits);

    // Decodes a CCITT Group 3/4 one-dimensional codestream back to packed
    // 1-bit rows (MSB-first, width bits per row, height rows). Returns an empty
    // vector when the stream is malformed.
    [[nodiscard]] static std::vector<std::byte> DecodeCcittG4(
        std::uint32_t width, std::uint32_t height,
        std::span<const std::byte> faxBytes);

    // Encodes an RGB image as a baseline JPEG (DCT, 4:4:4, quality 1-100)
    // without external libraries. Returns a complete JPEG file.
    [[nodiscard]] static std::vector<std::byte> EncodeJpeg(
        std::uint32_t width, std::uint32_t height,
        std::span<const std::byte> rgbBytes,
        int quality = 85);

    static PdfImage FromJpegFile(const std::filesystem::path& path);

    // Loads an image file, auto-detecting the format (PNG, JPEG, or raw RGB
    // data with a .rgb extension).
    static PdfImage FromFile(const std::filesystem::path& path);

    // Detects the container format from the leading bytes of an image file.
    // Matches iText `ImageDataFactory::GetImageType`.
    [[nodiscard]] static PdfImageType DetectImageType(std::span<const std::byte> bytes);

    static PdfImage FromGray(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const std::byte> grayBytes);

    static PdfImage FromGrayAlpha(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const std::byte> grayBytes,
        std::span<const std::byte> alphaBytes,
        std::span<const double> matte = {});

    [[nodiscard]] std::uint32_t GetWidth() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t GetHeight() const noexcept { return height_; }
    [[nodiscard]] PdfImageColorSpace GetColorSpace() const noexcept { return colorSpace_; }
    [[nodiscard]] std::span<const std::byte> GetBytes() const noexcept { return bytes_; }
    [[nodiscard]] PdfImageEncoding GetEncoding() const noexcept { return encoding_; }
    [[nodiscard]] std::uint16_t GetBitsPerComponent() const noexcept { return bitsPerComponent_; }
    [[nodiscard]] bool HasSoftMask() const noexcept { return !softMaskBytes_.empty(); }
    [[nodiscard]] std::span<const std::byte> GetSoftMaskBytes() const noexcept { return softMaskBytes_; }
    [[nodiscard]] std::span<const double> GetMatte() const noexcept { return matte_; }

    // Returns a copy with an 8-bit grayscale soft mask. `matte` is optional and
    // must contain one value per base color-space component when present.
    [[nodiscard]] PdfImage WithSoftMask(std::span<const std::byte> alphaBytes,
                                        std::span<const double> matte = {}) const;
    [[nodiscard]] PdfImage WithoutSoftMask() const;

    // Returns a DeviceRGB copy, converting Gray/CMYK/Indexed samples. Other
    // encodings (JPEG/JPX/CCITT) are returned unchanged.
    [[nodiscard]] PdfImage ConvertToRgb() const;

private:
    PdfImage(std::uint32_t width,
             std::uint32_t height,
             PdfImageColorSpace colorSpace,
             PdfImageEncoding encoding,
             std::uint16_t bitsPerComponent,
             std::vector<std::byte> bytes,
             std::vector<std::byte> softMaskBytes = {},
             std::vector<double> matte = {});

    std::uint32_t width_{};
    std::uint32_t height_{};
    PdfImageColorSpace colorSpace_{PdfImageColorSpace::Unknown};
    PdfImageEncoding encoding_{PdfImageEncoding::Raw};
    std::uint16_t bitsPerComponent_{8U};
    std::vector<std::byte> bytes_;
    std::vector<std::byte> softMaskBytes_;
    std::vector<double> matte_;
};

} // namespace CPPPdf

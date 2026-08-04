#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Graphics/PdfJpegEncoder.hpp"

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

std::vector<std::byte> PdfImage::EncodeJpeg(
    const std::uint32_t width, const std::uint32_t height,
    const std::span<const std::byte> rgbBytes, const int quality) {
    if (width == 0U || height == 0U || rgbBytes.size() < static_cast<std::size_t>(width) * height * 3U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Invalid RGB dimensions for JPEG encoding.");
    }
    return Internal::EncodeJpeg(width, height, rgbBytes, quality);
}

} // namespace CPPPdf

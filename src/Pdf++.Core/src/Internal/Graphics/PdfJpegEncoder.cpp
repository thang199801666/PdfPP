#include "Internal/Graphics/PdfJpegEncoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace CPPPdf::Internal {
namespace {

constexpr std::array<std::uint8_t, 64> kLuminanceQuantization = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99};

constexpr std::array<std::uint8_t, 64> kChrominanceQuantization = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99};

// Index in natural 8x8 order for each position in JPEG zig-zag order.
constexpr std::array<std::uint8_t, 64> kZigZag = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63};

constexpr std::array<std::uint8_t, 16> kDcLuminanceBits =
    {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr std::array<std::uint8_t, 12> kDcLuminanceValues =
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::array<std::uint8_t, 16> kDcChrominanceBits =
    {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr std::array<std::uint8_t, 12> kDcChrominanceValues =
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

constexpr std::array<std::uint8_t, 16> kAcLuminanceBits =
    {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D};
constexpr std::array<std::uint8_t, 162> kAcLuminanceValues = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
    0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
    0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA};

constexpr std::array<std::uint8_t, 16> kAcChrominanceBits =
    {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr std::array<std::uint8_t, 162> kAcChrominanceValues = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
    0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
    0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
    0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA};

struct HuffmanCode final {
    std::uint16_t code{};
    std::uint8_t length{};
};

using HuffmanTable = std::array<HuffmanCode, 256>;

HuffmanTable BuildHuffmanTable(const std::span<const std::uint8_t, 16> counts,
                               const std::span<const std::uint8_t> values) {
    HuffmanTable table{};
    std::uint16_t code = 0U;
    std::size_t valueIndex = 0U;
    for (std::uint8_t length = 1U; length <= 16U; ++length) {
        const auto count = counts[length - 1U];
        for (std::uint8_t i = 0U; i < count; ++i) {
            if (valueIndex >= values.size()) {
                throw std::runtime_error("Invalid JPEG Huffman table.");
            }
            table[values[valueIndex++]] = HuffmanCode{code, length};
            ++code;
        }
        code = static_cast<std::uint16_t>(code << 1U);
    }
    if (valueIndex != values.size()) {
        throw std::runtime_error("Invalid JPEG Huffman table value count.");
    }
    return table;
}

class BitWriter final {
public:
    void Write(const std::uint16_t value, const std::uint8_t bitCount) {
        if (bitCount == 0U) return;
        accumulator_ = (accumulator_ << bitCount) |
            (static_cast<std::uint64_t>(value) & ((std::uint64_t{1} << bitCount) - 1U));
        bits_ += bitCount;
        while (bits_ >= 8U) {
            const auto shift = static_cast<std::uint8_t>(bits_ - 8U);
            const auto byteValue = static_cast<std::uint8_t>((accumulator_ >> shift) & 0xFFU);
            bytes_.push_back(static_cast<std::byte>(byteValue));
            if (byteValue == 0xFFU) bytes_.push_back(std::byte{0x00});
            bits_ = shift;
            if (bits_ == 0U) accumulator_ = 0U;
            else accumulator_ &= ((std::uint64_t{1} << bits_) - 1U);
        }
    }

    void Flush() {
        if (bits_ == 0U) return;
        const auto padding = static_cast<std::uint8_t>(8U - bits_);
        // JPEG requires unused scan bits to be padded with one bits.
        const auto value = static_cast<std::uint16_t>(
            (accumulator_ << padding) | ((std::uint64_t{1} << padding) - 1U));
        const auto byteValue = static_cast<std::uint8_t>(value & 0xFFU);
        bytes_.push_back(static_cast<std::byte>(byteValue));
        if (byteValue == 0xFFU) bytes_.push_back(std::byte{0x00});
        accumulator_ = 0U;
        bits_ = 0U;
    }

    [[nodiscard]] const std::vector<std::byte>& Bytes() const noexcept { return bytes_; }

private:
    std::vector<std::byte> bytes_;
    std::uint64_t accumulator_{};
    std::uint8_t bits_{};
};

void AppendU16(std::vector<std::byte>& output, const std::uint16_t value) {
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::byte>(value & 0xFFU));
}

void AppendMarker(std::vector<std::byte>& output, const std::uint8_t marker) {
    output.push_back(std::byte{0xFF});
    output.push_back(static_cast<std::byte>(marker));
}

std::array<std::uint8_t, 64> ScaleQuantization(
    const std::array<std::uint8_t, 64>& base, const int quality) {
    const int clamped = std::clamp(quality, 1, 100);
    const int scale = clamped < 50 ? 5000 / clamped : 200 - 2 * clamped;
    std::array<std::uint8_t, 64> table{};
    for (std::size_t index = 0U; index < table.size(); ++index) {
        const int value = (static_cast<int>(base[index]) * scale + 50) / 100;
        table[index] = static_cast<std::uint8_t>(std::clamp(value, 1, 255));
    }
    return table;
}

const std::array<std::array<double, 8>, 8>& CosineTable() {
    static const auto table = [] {
        std::array<std::array<double, 8>, 8> value{};
        constexpr double pi = 3.1415926535897932384626433832795;
        for (std::size_t frequency = 0U; frequency < 8U; ++frequency) {
            for (std::size_t position = 0U; position < 8U; ++position) {
                value[frequency][position] =
                    std::cos((2.0 * static_cast<double>(position) + 1.0) *
                             static_cast<double>(frequency) * pi / 16.0);
            }
        }
        return value;
    }();
    return table;
}

std::array<std::int16_t, 64> TransformAndQuantize(
    const std::array<double, 64>& samples,
    const std::array<std::uint8_t, 64>& quantization) {
    const auto& cosine = CosineTable();
    std::array<std::int16_t, 64> coefficients{};
    for (std::size_t v = 0U; v < 8U; ++v) {
        for (std::size_t u = 0U; u < 8U; ++u) {
            double sum = 0.0;
            for (std::size_t y = 0U; y < 8U; ++y) {
                for (std::size_t x = 0U; x < 8U; ++x) {
                    sum += samples[y * 8U + x] * cosine[u][x] * cosine[v][y];
                }
            }
            const double cu = u == 0U ? 0.7071067811865475244 : 1.0;
            const double cv = v == 0U ? 0.7071067811865475244 : 1.0;
            const double transformed = 0.25 * cu * cv * sum;
            const auto quantized = std::lround(transformed /
                static_cast<double>(quantization[v * 8U + u]));
            coefficients[v * 8U + u] = static_cast<std::int16_t>(
                std::clamp<long>(quantized,
                                 std::numeric_limits<std::int16_t>::min(),
                                 std::numeric_limits<std::int16_t>::max()));
        }
    }
    return coefficients;
}

std::uint8_t MagnitudeCategory(const int value) {
    unsigned magnitude = static_cast<unsigned>(value < 0 ? -value : value);
    std::uint8_t category = 0U;
    while (magnitude != 0U) {
        ++category;
        magnitude >>= 1U;
    }
    return category;
}

std::uint16_t MagnitudeBits(const int value, const std::uint8_t category) {
    if (category == 0U) return 0U;
    if (value >= 0) return static_cast<std::uint16_t>(value);
    return static_cast<std::uint16_t>(value + ((1 << category) - 1));
}

void WriteHuffman(BitWriter& writer, const HuffmanTable& table, const std::uint8_t symbol) {
    const auto entry = table[symbol];
    if (entry.length == 0U) throw std::runtime_error("Missing JPEG Huffman symbol.");
    writer.Write(entry.code, entry.length);
}

void EncodeBlock(BitWriter& writer,
                 const std::array<std::int16_t, 64>& coefficients,
                 int& previousDc,
                 const HuffmanTable& dcTable,
                 const HuffmanTable& acTable) {
    const int dc = coefficients[0];
    const int difference = dc - previousDc;
    previousDc = dc;
    const auto dcCategory = MagnitudeCategory(difference);
    WriteHuffman(writer, dcTable, dcCategory);
    writer.Write(MagnitudeBits(difference, dcCategory), dcCategory);

    std::uint8_t zeroRun = 0U;
    for (std::size_t zig = 1U; zig < 64U; ++zig) {
        const int value = coefficients[kZigZag[zig]];
        if (value == 0) {
            ++zeroRun;
            continue;
        }
        while (zeroRun >= 16U) {
            WriteHuffman(writer, acTable, 0xF0U);
            zeroRun = static_cast<std::uint8_t>(zeroRun - 16U);
        }
        const auto category = MagnitudeCategory(value);
        if (category > 10U) {
            throw std::runtime_error("JPEG AC coefficient exceeds baseline range.");
        }
        const auto symbol = static_cast<std::uint8_t>((zeroRun << 4U) | category);
        WriteHuffman(writer, acTable, symbol);
        writer.Write(MagnitudeBits(value, category), category);
        zeroRun = 0U;
    }
    if (zeroRun != 0U) WriteHuffman(writer, acTable, 0x00U);
}

template<std::size_t N>
void AppendHuffmanDefinition(std::vector<std::byte>& output,
                             const std::uint8_t tableClassAndId,
                             const std::array<std::uint8_t, 16>& counts,
                             const std::array<std::uint8_t, N>& values) {
    output.push_back(static_cast<std::byte>(tableClassAndId));
    for (const auto count : counts) output.push_back(static_cast<std::byte>(count));
    for (const auto value : values) output.push_back(static_cast<std::byte>(value));
}

} // namespace

std::vector<std::byte> EncodeJpeg(const std::uint32_t width,
                                  const std::uint32_t height,
                                  const std::span<const std::byte> rgbBytes,
                                  const int quality) {
    if (width == 0U || height == 0U || width > 65535U || height > 65535U) {
        throw std::invalid_argument("JPEG dimensions must be between 1 and 65535.");
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 3U ||
        rgbBytes.size() != pixelCount * 3U) {
        throw std::invalid_argument("JPEG RGB byte count does not match width x height x 3.");
    }

    const auto luminanceQuantization = ScaleQuantization(kLuminanceQuantization, quality);
    const auto chrominanceQuantization = ScaleQuantization(kChrominanceQuantization, quality);
    const auto dcLuminance = BuildHuffmanTable(kDcLuminanceBits, kDcLuminanceValues);
    const auto dcChrominance = BuildHuffmanTable(kDcChrominanceBits, kDcChrominanceValues);
    const auto acLuminance = BuildHuffmanTable(kAcLuminanceBits, kAcLuminanceValues);
    const auto acChrominance = BuildHuffmanTable(kAcChrominanceBits, kAcChrominanceValues);

    std::vector<std::byte> output;
    output.reserve(pixelCount + 1024U);
    AppendMarker(output, 0xD8U); // SOI

    AppendMarker(output, 0xE0U); // APP0 / JFIF
    AppendU16(output, 16U);
    constexpr std::array<std::uint8_t, 14> jfif = {
        'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    for (const auto value : jfif) output.push_back(static_cast<std::byte>(value));

    AppendMarker(output, 0xDBU); // DQT
    AppendU16(output, 132U);
    output.push_back(std::byte{0x00});
    for (const auto index : kZigZag) {
        output.push_back(static_cast<std::byte>(luminanceQuantization[index]));
    }
    output.push_back(std::byte{0x01});
    for (const auto index : kZigZag) {
        output.push_back(static_cast<std::byte>(chrominanceQuantization[index]));
    }

    AppendMarker(output, 0xC0U); // SOF0 baseline DCT
    AppendU16(output, 17U);
    output.push_back(std::byte{8});
    AppendU16(output, static_cast<std::uint16_t>(height));
    AppendU16(output, static_cast<std::uint16_t>(width));
    output.push_back(std::byte{3});
    output.push_back(std::byte{1}); output.push_back(std::byte{0x11}); output.push_back(std::byte{0});
    output.push_back(std::byte{2}); output.push_back(std::byte{0x11}); output.push_back(std::byte{1});
    output.push_back(std::byte{3}); output.push_back(std::byte{0x11}); output.push_back(std::byte{1});

    AppendMarker(output, 0xC4U); // DHT: all four baseline tables
    constexpr std::uint16_t dhtLength = static_cast<std::uint16_t>(
        2U + (1U + 16U + kDcLuminanceValues.size()) +
        (1U + 16U + kAcLuminanceValues.size()) +
        (1U + 16U + kDcChrominanceValues.size()) +
        (1U + 16U + kAcChrominanceValues.size()));
    AppendU16(output, dhtLength);
    AppendHuffmanDefinition(output, 0x00U, kDcLuminanceBits, kDcLuminanceValues);
    AppendHuffmanDefinition(output, 0x10U, kAcLuminanceBits, kAcLuminanceValues);
    AppendHuffmanDefinition(output, 0x01U, kDcChrominanceBits, kDcChrominanceValues);
    AppendHuffmanDefinition(output, 0x11U, kAcChrominanceBits, kAcChrominanceValues);

    AppendMarker(output, 0xDAU); // SOS
    AppendU16(output, 12U);
    output.push_back(std::byte{3});
    output.push_back(std::byte{1}); output.push_back(std::byte{0x00});
    output.push_back(std::byte{2}); output.push_back(std::byte{0x11});
    output.push_back(std::byte{3}); output.push_back(std::byte{0x11});
    output.push_back(std::byte{0});
    output.push_back(std::byte{63});
    output.push_back(std::byte{0});

    BitWriter bitWriter;
    int previousY = 0;
    int previousCb = 0;
    int previousCr = 0;
    const auto readChannel = [&](const std::uint32_t x,
                                 const std::uint32_t y,
                                 const std::size_t channel) -> double {
        const auto clampedX = std::min(x, width - 1U);
        const auto clampedY = std::min(y, height - 1U);
        const auto offset = (static_cast<std::size_t>(clampedY) * width + clampedX) * 3U + channel;
        return static_cast<double>(std::to_integer<std::uint8_t>(rgbBytes[offset]));
    };

    for (std::uint32_t blockY = 0U; blockY < height; blockY += 8U) {
        for (std::uint32_t blockX = 0U; blockX < width; blockX += 8U) {
            std::array<double, 64> ySamples{};
            std::array<double, 64> cbSamples{};
            std::array<double, 64> crSamples{};
            for (std::uint32_t y = 0U; y < 8U; ++y) {
                for (std::uint32_t x = 0U; x < 8U; ++x) {
                    const double red = readChannel(blockX + x, blockY + y, 0U);
                    const double green = readChannel(blockX + x, blockY + y, 1U);
                    const double blue = readChannel(blockX + x, blockY + y, 2U);
                    const auto index = static_cast<std::size_t>(y * 8U + x);
                    ySamples[index] = 0.299 * red + 0.587 * green + 0.114 * blue - 128.0;
                    cbSamples[index] = -0.168736 * red - 0.331264 * green + 0.5 * blue;
                    crSamples[index] = 0.5 * red - 0.418688 * green - 0.081312 * blue;
                }
            }
            EncodeBlock(bitWriter,
                        TransformAndQuantize(ySamples, luminanceQuantization),
                        previousY, dcLuminance, acLuminance);
            EncodeBlock(bitWriter,
                        TransformAndQuantize(cbSamples, chrominanceQuantization),
                        previousCb, dcChrominance, acChrominance);
            EncodeBlock(bitWriter,
                        TransformAndQuantize(crSamples, chrominanceQuantization),
                        previousCr, dcChrominance, acChrominance);
        }
    }
    bitWriter.Flush();
    output.insert(output.end(), bitWriter.Bytes().begin(), bitWriter.Bytes().end());
    AppendMarker(output, 0xD9U); // EOI
    return output;
}

} // namespace CPPPdf::Internal

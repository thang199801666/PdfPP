#pragma once
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace CPPPdf {

struct PdfFilterSpec {
    std::string name;
    std::string decodeParameters;
};

class PdfFilterPipeline final {
public:
    explicit PdfFilterPipeline(std::size_t maxDecodedSize = 512ULL * 1024ULL * 1024ULL)
        : maxDecodedSize_(maxDecodedSize) {}

    [[nodiscard]] std::vector<std::byte> Decode(
        std::span<const std::byte> input,
        const std::vector<PdfFilterSpec>& filters) const;

    // Encodes `input` through the named filters (applied in order). Only
    // self-contained filters can be encoded: FlateDecode, RunLengthDecode,
    // ASCIIHexDecode, ASCII85Decode, and LZWDecode. A predictor is not applied
    // Predictor parameters are read from each filter's decodeParameters.
    [[nodiscard]] std::vector<std::byte> Encode(
        std::span<const std::byte> input,
        const std::vector<PdfFilterSpec>& filters) const;

    [[nodiscard]] static std::vector<std::byte> DecodeFlate(
        std::span<const std::byte> input,
        std::size_t maxDecodedSize);
    [[nodiscard]] static std::vector<std::byte> DecodeAsciiHex(
        std::span<const std::byte> input,
        std::size_t maxDecodedSize = std::numeric_limits<std::size_t>::max());
    [[nodiscard]] static std::vector<std::byte> DecodeAscii85(
        std::span<const std::byte> input,
        std::size_t maxDecodedSize = std::numeric_limits<std::size_t>::max());
    [[nodiscard]] static std::vector<std::byte> DecodeRunLength(
        std::span<const std::byte> input,
        std::size_t maxDecodedSize = std::numeric_limits<std::size_t>::max());
    [[nodiscard]] static std::vector<std::byte> DecodeLzw(
        std::span<const std::byte> input,
        bool earlyChange = true,
        std::size_t maxDecodedSize = std::numeric_limits<std::size_t>::max());

    // Encode counterparts. Each returns bytes that `Decode*` reads back to the
    // exact input.
    [[nodiscard]] static std::vector<std::byte> EncodeFlate(
        std::span<const std::byte> input);
    [[nodiscard]] static std::vector<std::byte> EncodeAsciiHex(
        std::span<const std::byte> input);
    [[nodiscard]] static std::vector<std::byte> EncodeAscii85(
        std::span<const std::byte> input);
    [[nodiscard]] static std::vector<std::byte> EncodeRunLength(
        std::span<const std::byte> input);
    [[nodiscard]] static std::vector<std::byte> EncodeLzw(
        std::span<const std::byte> input,
        bool earlyChange = true);
private:
    std::size_t maxDecodedSize_;
};

} // namespace CPPPdf

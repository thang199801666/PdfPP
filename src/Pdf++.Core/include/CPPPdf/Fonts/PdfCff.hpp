#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <string>

namespace CPPPdf {

struct PdfCffIndex final {
    std::vector<std::span<const std::byte>> objects;
};

struct PdfCffDictEntry final {
    std::uint16_t operatorCode{};
    std::vector<double> operands;
};

struct PdfCffTopDict final {
    std::uint32_t charsetOffset{};
    std::uint32_t encodingOffset{};
    std::uint32_t charStringsOffset{};
    std::uint32_t privateOffset{};
    std::uint32_t privateSize{};
    std::string fontName;
};

struct PdfCffFont final {
    std::string name;
    PdfCffTopDict top;
};

class PdfCffParser final {
public:
    static PdfCffIndex ParseIndex(std::span<const std::byte> bytes, std::size_t& offset,
                                  std::size_t maxObjects = 65536U);
    static std::vector<PdfCffDictEntry> ParseDict(std::span<const std::byte> bytes);
    static PdfCffFont ParseFont(std::span<const std::byte> bytes);
};

} // namespace CPPPdf

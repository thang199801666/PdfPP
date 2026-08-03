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

struct PdfCffPrivateDict final {
    double defaultWidthX{0.0};
    double nominalWidthX{0.0};
    std::uint32_t subrsOffset{};
    std::uint32_t subrsSize{};
};

struct PdfCffOutlineSegment final {
    enum class Type { Move, Line, Cubic };
    Type type{Type::Move};
    double x1{};
    double y1{};
    double x2{};
    double y2{};
    double x3{};
    double y3{};
};

struct PdfCffGlyphOutline final {
    std::vector<PdfCffOutlineSegment> segments;
    double width{0.0};
    double xMin{};
    double yMin{};
    double xMax{};
    double yMax{};
    bool empty{true};

    [[nodiscard]] bool IsEmpty() const noexcept { return empty; }
};

struct PdfCffFont final {
    std::string name;
    PdfCffTopDict top;
    PdfCffPrivateDict privateDict;
    std::vector<std::byte> data;
    PdfCffIndex charStrings;
    PdfCffIndex globalSubrs;
    PdfCffIndex localSubrs;
    // Per-glyph character string entry. For simple fonts this is a SID; for
    // CID fonts the value is the CID of the glyph at that index.
    std::vector<std::uint32_t> charset;
    std::uint32_t glyphCount{};
    bool isCID{};
    bool hasLocalSubrs{};
};

class PdfCffParser final {
public:
    static PdfCffIndex ParseIndex(std::span<const std::byte> bytes, std::size_t& offset,
                                  std::size_t maxObjects = 65536U);
    static std::vector<PdfCffDictEntry> ParseDict(std::span<const std::byte> bytes);
    static PdfCffFont ParseFont(std::span<const std::byte> bytes);
    static PdfCffGlyphOutline GetGlyphOutline(const PdfCffFont& font, std::uint32_t glyphId);
};

} // namespace CPPPdf

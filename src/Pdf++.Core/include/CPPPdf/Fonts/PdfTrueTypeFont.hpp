#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <unordered_map>
#include <vector>

namespace CPPPdf {

struct PdfTrueTypeSubset final {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint16_t> glyphIds;
    std::size_t originalByteSize{};
    bool subsetApplied{};

    [[nodiscard]] std::size_t GetByteSize() const noexcept { return bytes.size(); }
    [[nodiscard]] double GetReductionRatio() const noexcept {
        return originalByteSize == 0 ? 0.0 : 1.0 - static_cast<double>(bytes.size()) / static_cast<double>(originalByteSize);
    }
};

struct PdfTrueTypeMetrics final {
    std::uint16_t unitsPerEm{1000};
    std::int16_t ascent{};
    std::int16_t descent{};
    std::int16_t lineGap{};
    std::uint16_t glyphCount{};
};

class PdfTrueTypeFont final {
public:
    static PdfTrueTypeFont Load(const std::filesystem::path& path);
    static PdfTrueTypeFont Parse(std::vector<std::uint8_t> bytes, std::string sourceName = {});

    [[nodiscard]] const std::string& GetSourceName() const noexcept;
    [[nodiscard]] const std::vector<std::uint8_t>& GetBytes() const noexcept;
    [[nodiscard]] const PdfTrueTypeMetrics& GetMetrics() const noexcept;
    [[nodiscard]] std::size_t GetGlyphMappingCount() const noexcept;
    [[nodiscard]] bool Supports(std::uint32_t unicodeCodePoint) const noexcept;
    [[nodiscard]] std::optional<std::uint16_t> GetGlyphId(std::uint32_t unicodeCodePoint) const noexcept;
    [[nodiscard]] std::uint16_t GetAdvanceWidth(std::uint16_t glyphId) const noexcept;
    [[nodiscard]] double GetAdvanceWidth(std::uint16_t glyphId, double fontSize) const;
    [[nodiscard]] double MeasureTextUtf8(std::string_view text, double fontSize) const;
    [[nodiscard]] double GetLineHeight(double fontSize, double lineSpacing = 1.0) const;
    [[nodiscard]] PdfTrueTypeSubset BuildSubset(std::span<const std::uint16_t> glyphIds) const;

private:
    std::string sourceName_;
    std::vector<std::uint8_t> bytes_;
    std::unordered_map<std::uint32_t, std::uint16_t> unicodeToGlyph_;
    PdfTrueTypeMetrics metrics_{};
    std::vector<std::uint16_t> advanceWidths_;
};

} // namespace CPPPdf

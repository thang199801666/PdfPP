#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <list>

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

struct PdfTrueTypePoint final {
    std::int16_t x{};
    std::int16_t y{};
    bool onCurve{};
};

struct PdfTrueTypeGlyphOutline final {
    struct Component final {
        std::uint16_t glyphId{};
        std::int32_t argument1{};
        std::int32_t argument2{};
        bool argumentsAreXY{};
        bool roundToGrid{};
        double xx{1.0};
        double xy{};
        double yx{};
        double yy{1.0};
    };
    std::vector<std::vector<PdfTrueTypePoint>> contours;
    std::vector<Component> components;
    std::int16_t xMin{};
    std::int16_t yMin{};
    std::int16_t xMax{};
    std::int16_t yMax{};
    bool composite{};
};

class PdfTrueTypeFont final {
public:
    static PdfTrueTypeFont Load(const std::filesystem::path& path);
    static PdfTrueTypeFont Parse(std::vector<std::uint8_t> bytes, std::string sourceName = {});

    [[nodiscard]] const std::string& GetSourceName() const noexcept;
    [[nodiscard]] const std::vector<std::uint8_t>& GetBytes() const noexcept;
    [[nodiscard]] bool HasTable(std::string_view tag) const noexcept;
    [[nodiscard]] const PdfTrueTypeMetrics& GetMetrics() const noexcept;
    [[nodiscard]] std::size_t GetGlyphMappingCount() const noexcept;
    [[nodiscard]] bool Supports(std::uint32_t unicodeCodePoint) const noexcept;
    [[nodiscard]] std::optional<std::uint16_t> GetGlyphId(std::uint32_t unicodeCodePoint) const noexcept;
    [[nodiscard]] std::uint16_t GetAdvanceWidth(std::uint16_t glyphId) const noexcept;
    [[nodiscard]] double GetAdvanceWidth(std::uint16_t glyphId, double fontSize) const;
    [[nodiscard]] double GetCachedAdvanceWidth(std::uint16_t glyphId, double fontSize) const;
    [[nodiscard]] double MeasureTextUtf8(std::string_view text, double fontSize) const;
    [[nodiscard]] double GetLineHeight(double fontSize, double lineSpacing = 1.0) const;
    [[nodiscard]] PdfTrueTypeGlyphOutline GetGlyphOutline(std::uint16_t glyphId) const;
    [[nodiscard]] const PdfTrueTypeGlyphOutline& GetGlyphOutlineCached(std::uint16_t glyphId) const;
    [[nodiscard]] std::size_t GetCachedOutlineCount() const noexcept { return outlineCache_.size(); }
    [[nodiscard]] std::size_t GetOutlineCacheHits() const noexcept { return outlineCacheHits_; }
    [[nodiscard]] std::size_t GetOutlineCacheMisses() const noexcept { return outlineCacheMisses_; }
    [[nodiscard]] std::size_t GetAdvanceCacheHits() const noexcept { return advanceCacheHits_; }
    [[nodiscard]] std::size_t GetAdvanceCacheMisses() const noexcept { return advanceCacheMisses_; }
    static constexpr std::size_t kGlyphCacheLimit = 4096U;
    void ClearGlyphCaches() const noexcept;
    [[nodiscard]] PdfTrueTypeSubset BuildSubset(std::span<const std::uint16_t> glyphIds) const;

private:
    std::string sourceName_;
    std::vector<std::uint8_t> bytes_;
    std::unordered_map<std::uint32_t, std::uint16_t> unicodeToGlyph_;
    PdfTrueTypeMetrics metrics_{};
    std::vector<std::uint16_t> advanceWidths_;
    using OutlineCacheEntry = std::pair<const std::uint16_t, PdfTrueTypeGlyphOutline>;
    mutable std::list<OutlineCacheEntry> outlineLru_;
    mutable std::unordered_map<std::uint16_t, std::list<OutlineCacheEntry>::iterator> outlineCache_;
    using AdvanceCacheEntry = std::pair<const std::uint64_t, double>;
    mutable std::list<AdvanceCacheEntry> advanceLru_;
    mutable std::unordered_map<std::uint64_t, std::list<AdvanceCacheEntry>::iterator> advanceCache_;
    mutable std::size_t outlineCacheHits_{};
    mutable std::size_t outlineCacheMisses_{};
    mutable std::size_t advanceCacheHits_{};
    mutable std::size_t advanceCacheMisses_{};

    [[nodiscard]] PdfTrueTypeGlyphOutline ReadGlyphOutline(
        std::uint16_t glyphId, std::unordered_set<std::uint16_t>& active) const;
};

} // namespace CPPPdf

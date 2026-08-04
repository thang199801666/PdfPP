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
    // Font names read from the `name` table (PostScript name = nameID 6,
    // family = nameID 1). Falls back to the source name when absent.
    [[nodiscard]] std::string GetPostScriptName() const;
    [[nodiscard]] std::string GetFontFamily() const;
    // True when the font has an OpenType `fvar` table (variable font).
    [[nodiscard]] bool IsVariable() const;
    // Number of variation axes from the `fvar` table.
    [[nodiscard]] std::size_t GetVariationAxisCount() const;
    // OS/2 usWeightClass (100..900; 400 normal, 700 bold), or 400 when absent.
    [[nodiscard]] std::uint16_t GetWeightClass() const;
    [[nodiscard]] const std::vector<std::uint8_t>& GetBytes() const noexcept;
    [[nodiscard]] bool HasTable(std::string_view tag) const noexcept;
    [[nodiscard]] const PdfTrueTypeMetrics& GetMetrics() const noexcept;
    [[nodiscard]] std::size_t GetGlyphMappingCount() const noexcept;
    [[nodiscard]] bool Supports(std::uint32_t unicodeCodePoint) const noexcept;
    [[nodiscard]] std::optional<std::uint16_t> GetGlyphId(std::uint32_t unicodeCodePoint) const noexcept;
    // Returns a Unicode code point that maps to the glyph (first cmap hit), or
    // std::nullopt when the glyph is unmapped.
    [[nodiscard]] std::optional<std::uint32_t> GetUnicodeForGlyph(std::uint16_t glyphId) const;
    [[nodiscard]] std::uint16_t GetAdvanceWidth(std::uint16_t glyphId) const noexcept;
    [[nodiscard]] double GetAdvanceWidth(std::uint16_t glyphId, double fontSize) const;
    [[nodiscard]] double GetCachedAdvanceWidth(std::uint16_t glyphId, double fontSize) const;
    [[nodiscard]] double MeasureTextUtf8(std::string_view text, double fontSize) const;
    [[nodiscard]] double GetLineHeight(double fontSize, double lineSpacing = 1.0) const;
    // Horizontal kerning between two glyphs (from the `kern` table), in font
    // units scaled to `fontSize`. Returns 0 when no adjustment is defined.
    [[nodiscard]] double GetKerning(std::uint16_t leftGlyph, std::uint16_t rightGlyph,
                                    double fontSize) const;
    [[nodiscard]] double GetCachedKerning(std::uint16_t leftGlyph, std::uint16_t rightGlyph,
                                          double fontSize) const;
    [[nodiscard]] std::size_t GetKerningPairCount() const noexcept { return kerning_.size(); }
    [[nodiscard]] bool HasKerning() const noexcept { return !kerning_.empty(); }

    // OpenType GSUB ligature substitution: applies the first matching
    // LigatureSubst (lookup type 4) substitution to a glyph sequence, replacing
    // matched runs with the ligature glyph. Returns the substituted glyphs.
    [[nodiscard]] std::vector<std::uint16_t> ApplyLigatures(
        std::span<const std::uint16_t> glyphs) const;
    [[nodiscard]] bool HasLigatures() const noexcept { return !ligatures_.empty(); }
    [[nodiscard]] std::size_t GetLigatureCount() const noexcept { return ligatures_.size(); }

    // Returns the anchor attachment (mark anchor + base anchor) for a combining
    // mark glyph placed over a base glyph, when the font has a GPOS
    // MarkBasePos table for the pair.
    struct MarkBaseAttachment final {
        std::int16_t markX{};
        std::int16_t markY{};
        std::int16_t baseX{};
        std::int16_t baseY{};
    };
    [[nodiscard]] std::optional<MarkBaseAttachment> GetMarkBasePosition(
        std::uint16_t markGlyph, std::uint16_t baseGlyph) const;
    [[nodiscard]] bool HasMarkBase() const noexcept { return !markBase_.empty(); }
    [[nodiscard]] std::size_t GetMarkBaseCount() const noexcept { return markBase_.size(); }
    // Returns the anchor attachment for a mark placed over another mark
    // (double diacritics) when the font has a GPOS MarkMarkPos pair.
    [[nodiscard]] std::optional<MarkBaseAttachment> GetMarkMarkPosition(
        std::uint16_t mark1Glyph, std::uint16_t mark2Glyph) const;
    [[nodiscard]] bool HasMarkMark() const noexcept { return !markMark_.empty(); }
    [[nodiscard]] std::size_t GetMarkMarkCount() const noexcept { return markMark_.size(); }
    // Returns the GPOS SinglePos xAdvance adjustment (font units) for a glyph,
    // or 0 when the glyph has none.
    [[nodiscard]] std::int16_t GetGlyphAdvanceAdjustment(std::uint16_t glyph) const noexcept;
    [[nodiscard]] bool HasGlyphAdjustments() const noexcept { return !glyphAdjustments_.empty(); }
    [[nodiscard]] std::size_t GetGlyphAdjustmentCount() const noexcept { return glyphAdjustments_.size(); }
    // GPOS cursive entry/exit anchors used when joining script glyphs.
    struct CursiveAnchor final {
        std::int16_t entryX{};
        std::int16_t entryY{};
        std::int16_t exitX{};
        std::int16_t exitY{};
        bool hasEntry{};
        bool hasExit{};
    };
    // Returns the GPOS cursive entry/exit anchors for a glyph.
    [[nodiscard]] std::optional<CursiveAnchor> GetCursiveAnchor(std::uint16_t glyph) const;
    [[nodiscard]] bool HasCursiveAnchors() const noexcept { return !cursiveAnchors_.empty(); }
    [[nodiscard]] std::size_t GetCursiveAnchorCount() const noexcept { return cursiveAnchors_.size(); }
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
    std::unordered_map<std::uint64_t, std::int16_t> kerning_;
    // OpenType GSUB ligature: sequence of component glyphs -> ligature glyph.
    struct LigatureEntry final {
        std::vector<std::uint16_t> components;
        std::uint16_t ligatureGlyph{};
    };
    std::vector<LigatureEntry> ligatures_;
    // OpenType GPOS mark-to-base anchors: (markGlyph<<32 | baseGlyph) ->
    // (mark anchor x/y, base anchor x/y) in font units.
    struct AnchorPair final {
        std::int16_t markX{};
        std::int16_t markY{};
        std::int16_t baseX{};
        std::int16_t baseY{};
    };
    std::unordered_map<std::uint64_t, AnchorPair> markBase_;
    // OpenType GPOS mark-to-mark anchors: (mark1<<32 | mark2).
    std::unordered_map<std::uint64_t, AnchorPair> markMark_;
    // OpenType GPOS SinglePos xAdvance adjustments per glyph (font units).
    std::unordered_map<std::uint16_t, std::int16_t> glyphAdjustments_;
    // OpenType GPOS cursive entry/exit anchors per glyph (font units).
    std::unordered_map<std::uint16_t, CursiveAnchor> cursiveAnchors_;
    using KerningCacheEntry = std::pair<const std::uint64_t, double>;
    mutable std::list<KerningCacheEntry> kerningLru_;
    mutable std::unordered_map<std::uint64_t, std::list<KerningCacheEntry>::iterator> kerningCache_;
    mutable std::size_t kerningCacheHits_{};
    mutable std::size_t kerningCacheMisses_{};
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

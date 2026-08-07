#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CPPPdf {

// A single glyph program for a PDF Type 3 font. The content is a regular PDF
// content stream expressed in glyph space. Pdf++ prepends the required `d1`
// operator using advanceWidth and boundingBox when the font is written.
struct PdfType3Glyph final {
    std::uint8_t code{};
    std::string name;
    double advanceWidth{500.0};
    PdfRectangle boundingBox{};
    std::string content;
    std::optional<std::uint32_t> unicodeCodePoint;
};

// User-defined PDF Type 3 font. This is useful for symbols, engineering
// notation, barcodes and vector icons without requiring an external font file.
class PdfType3Font final {
public:
    explicit PdfType3Font(std::string fontName = "PdfPPType3",
                          PdfRectangle fontBoundingBox = {0.0, -200.0, 1000.0, 1000.0});

    PdfType3Font& SetFontMatrix(std::array<double, 6> matrix);
    PdfType3Font& AddGlyph(PdfType3Glyph glyph);

    [[nodiscard]] const std::string& GetFontName() const noexcept { return fontName_; }
    [[nodiscard]] const PdfRectangle& GetFontBoundingBox() const noexcept { return fontBoundingBox_; }
    [[nodiscard]] const std::array<double, 6>& GetFontMatrix() const noexcept { return fontMatrix_; }
    [[nodiscard]] const std::vector<PdfType3Glyph>& GetGlyphs() const noexcept { return glyphs_; }
    [[nodiscard]] const PdfType3Glyph* FindGlyphByCode(std::uint8_t code) const noexcept;
    [[nodiscard]] const PdfType3Glyph* FindGlyphByUnicode(std::uint32_t codePoint) const noexcept;
    [[nodiscard]] bool Empty() const noexcept { return glyphs_.empty(); }

private:
    std::string fontName_;
    PdfRectangle fontBoundingBox_{};
    std::array<double, 6> fontMatrix_{0.001, 0.0, 0.0, 0.001, 0.0, 0.0};
    std::vector<PdfType3Glyph> glyphs_;
};

} // namespace CPPPdf

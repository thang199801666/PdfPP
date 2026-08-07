#include <CPPPdf/Fonts/PdfType3Font.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CPPPdf {
namespace {

bool IsSimplePdfName(const std::string& value) {
    if (value.empty()) return false;
    for (const unsigned char ch : value) {
        if (ch <= 0x20U || ch >= 0x7FU || ch == '#' || ch == '/' || ch == '%' ||
            ch == '(' || ch == ')' || ch == '<' || ch == '>' || ch == '[' ||
            ch == ']' || ch == '{' || ch == '}') {
            return false;
        }
    }
    return true;
}

bool FiniteRectangle(const PdfRectangle& rectangle) {
    return std::isfinite(rectangle.left) && std::isfinite(rectangle.bottom) &&
           std::isfinite(rectangle.right) && std::isfinite(rectangle.top);
}

} // namespace

PdfType3Font::PdfType3Font(std::string fontName, const PdfRectangle fontBoundingBox)
    : fontName_(std::move(fontName)), fontBoundingBox_(fontBoundingBox) {
    if (!IsSimplePdfName(fontName_)) {
        throw std::invalid_argument("Type3 font name must be a simple PDF name.");
    }
    if (!FiniteRectangle(fontBoundingBox_) || fontBoundingBox_.empty()) {
        throw std::invalid_argument("Type3 font bounding box must be finite and non-empty.");
    }
}

PdfType3Font& PdfType3Font::SetFontMatrix(const std::array<double, 6> matrix) {
    if (!std::all_of(matrix.begin(), matrix.end(), [](const double value) {
            return std::isfinite(value);
        })) {
        throw std::invalid_argument("Type3 font matrix values must be finite.");
    }
    const double determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];
    if (std::abs(determinant) < 1.0e-15) {
        throw std::invalid_argument("Type3 font matrix must be invertible.");
    }
    fontMatrix_ = matrix;
    return *this;
}

PdfType3Font& PdfType3Font::AddGlyph(PdfType3Glyph glyph) {
    if (!IsSimplePdfName(glyph.name)) {
        throw std::invalid_argument("Type3 glyph name must be a simple PDF name.");
    }
    if (!std::isfinite(glyph.advanceWidth)) {
        throw std::invalid_argument("Type3 glyph advance width must be finite.");
    }
    if (!FiniteRectangle(glyph.boundingBox)) {
        throw std::invalid_argument("Type3 glyph bounding box must be finite.");
    }
    if (glyph.unicodeCodePoint && *glyph.unicodeCodePoint > 0x10FFFFU) {
        throw std::invalid_argument("Type3 glyph Unicode code point is outside the Unicode range.");
    }
    const auto duplicateCode = std::find_if(glyphs_.begin(), glyphs_.end(), [&](const auto& item) {
        return item.code == glyph.code;
    });
    if (duplicateCode != glyphs_.end()) {
        throw std::invalid_argument("Type3 glyph code is already defined.");
    }
    const auto duplicateName = std::find_if(glyphs_.begin(), glyphs_.end(), [&](const auto& item) {
        return item.name == glyph.name;
    });
    if (duplicateName != glyphs_.end()) {
        throw std::invalid_argument("Type3 glyph name is already defined.");
    }
    glyphs_.push_back(std::move(glyph));
    std::sort(glyphs_.begin(), glyphs_.end(), [](const auto& left, const auto& right) {
        return left.code < right.code;
    });
    return *this;
}

const PdfType3Glyph* PdfType3Font::FindGlyphByCode(const std::uint8_t code) const noexcept {
    const auto iterator = std::find_if(glyphs_.begin(), glyphs_.end(), [&](const auto& glyph) {
        return glyph.code == code;
    });
    return iterator == glyphs_.end() ? nullptr : &*iterator;
}

const PdfType3Glyph* PdfType3Font::FindGlyphByUnicode(const std::uint32_t codePoint) const noexcept {
    const auto iterator = std::find_if(glyphs_.begin(), glyphs_.end(), [&](const auto& glyph) {
        return glyph.unicodeCodePoint && *glyph.unicodeCodePoint == codePoint;
    });
    return iterator == glyphs_.end() ? nullptr : &*iterator;
}

} // namespace CPPPdf

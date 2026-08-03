#pragma once

#include <CPPPdf/Fonts/PdfFont.hpp>
#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <CPPPdf/Fonts/PdfCff.hpp>
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CPPPdf {

class PdfFontResource final {
public:
    using Resolver = std::function<const PdfObject&(const PdfReference&)>;

    static PdfFontResource Create(const PdfDictionary& dictionary, const Resolver& resolver = {});

    [[nodiscard]] PdfFontSubtype GetSubtype() const noexcept { return descriptor_.subtype; }
    [[nodiscard]] const PdfFontDescriptor& GetDescriptor() const noexcept { return descriptor_; }
    [[nodiscard]] bool HasToUnicode() const noexcept { return toUnicode_.has_value(); }
    [[nodiscard]] bool IsComposite() const noexcept { return composite_; }
    [[nodiscard]] bool CanRenderEmbeddedGlyphs() const noexcept {
        return embeddedTrueType_ != nullptr || (embeddedCff_ && parsedCff_ != nullptr);
    }
    [[nodiscard]] bool HasUnicodeMapping() const noexcept {
        return toUnicode_.has_value() || !simpleUnicode_.empty();
    }
    [[nodiscard]] bool IsRenderable() const noexcept {
        return embeddedTrueType_ != nullptr || (embeddedCff_ && parsedCff_ != nullptr) ||
               descriptor_.subtype == PdfFontSubtype::Type1;
    }
    [[nodiscard]] bool RequiresExternalShaping() const noexcept {
        return composite_ || descriptor_.subtype == PdfFontSubtype::CIDFontType0 ||
               descriptor_.subtype == PdfFontSubtype::CIDFontType2;
    }
    [[nodiscard]] bool HasEmbeddedCffFont() const noexcept { return embeddedCff_; }
    [[nodiscard]] bool HasEmbeddedType1Font() const noexcept { return embeddedType1_; }
    [[nodiscard]] std::string_view EmbeddedProgramSubtype() const noexcept { return embeddedProgramSubtype_; }
    [[nodiscard]] const PdfCffFont* GetEmbeddedCffFont() const noexcept { return parsedCff_.get(); }
    [[nodiscard]] PdfCffGlyphOutline GetCffGlyphOutline(std::uint32_t glyphId) const;
    [[nodiscard]] std::uint32_t GetGlyphWidth(std::uint32_t characterCode) const noexcept;
    [[nodiscard]] std::size_t GetGlyphCount(std::string_view encodedBytes) const noexcept;
    [[nodiscard]] std::vector<std::uint32_t> GetCharacterCodes(std::string_view encodedBytes) const;
    [[nodiscard]] const PdfTrueTypeFont* GetEmbeddedTrueTypeFont() const noexcept { return embeddedTrueType_.get(); }
    [[nodiscard]] std::optional<std::uint16_t> GetEmbeddedGlyphId(std::uint32_t characterCode) const noexcept;
    [[nodiscard]] double MeasureEncodedText(std::string_view encodedBytes) const noexcept;
    [[nodiscard]] std::string Decode(std::string_view encodedBytes) const;

private:
    static const PdfObject* ResolveObject(const PdfObject* object, const Resolver& resolver);
    static std::string NameValue(const PdfObject* object, const Resolver& resolver);

    PdfFontDescriptor descriptor_;
    std::optional<PdfToUnicodeCMap> toUnicode_;
    std::unordered_map<std::uint32_t, std::string> simpleUnicode_;
    std::unordered_map<std::uint32_t, std::uint32_t> widths_;
    std::uint32_t defaultWidth_{1000};
    bool composite_{false};
    bool identityEncoding_{false};
    std::shared_ptr<const PdfTrueTypeFont> embeddedTrueType_;
    std::shared_ptr<const PdfCffFont> parsedCff_;
    bool embeddedCff_{false};
    bool embeddedType1_{false};
    std::string embeddedProgramSubtype_;
};

} // namespace CPPPdf

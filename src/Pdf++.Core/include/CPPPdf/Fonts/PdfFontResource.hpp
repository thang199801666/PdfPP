#pragma once

#include <CPPPdf/Fonts/PdfFont.hpp>
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>

#include <cstdint>
#include <functional>
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
    [[nodiscard]] std::uint32_t GetDefaultWidth() const noexcept { return defaultWidth_; }
    [[nodiscard]] std::uint32_t GetGlyphWidth(std::uint32_t characterCode) const noexcept;
    [[nodiscard]] std::size_t GetGlyphCount(std::string_view encodedBytes) const noexcept;
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
};

} // namespace CPPPdf

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace CPPPdf {

// Embedded Type1 font support. A PFB (binary Type1 program) can be parsed for
// its FontName and the standard 256 character widths, and the raw program is
// embedded into a PDF as a `/FontFile` stream for a `/Subtype /Type1` font.
class PdfType1Font final {
public:
    // Parses a PFB (Type1 binary) or PFA (Type1 ASCII) program.
    static PdfType1Font Parse(std::vector<std::uint8_t> bytes, std::string sourceName = {});
    static PdfType1Font Load(const std::filesystem::path& path);

    [[nodiscard]] const std::string& GetFontName() const noexcept { return fontName_; }
    [[nodiscard]] const std::string& GetSourceName() const noexcept { return sourceName_; }
    [[nodiscard]] const std::vector<std::uint8_t>& GetBytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t GetByteSize() const noexcept { return bytes_.size(); }
    // Character code (0-255) advance width, in thousandths of an em.
    [[nodiscard]] std::uint32_t GetGlyphWidth(std::uint8_t code) const noexcept;

private:
    std::string fontName_;
    std::string sourceName_;
    std::vector<std::uint8_t> bytes_;
    std::array<std::uint32_t, 256> widths_{};
};

} // namespace CPPPdf

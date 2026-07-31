#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CPPPdf {

enum class PdfFontSubtype { Unknown, Type1, TrueType, Type0, CIDFontType0, CIDFontType2, Type3 };

struct PdfFontDescriptor {
    PdfFontSubtype subtype{PdfFontSubtype::Unknown};
    std::string baseFont;
    std::string encoding;
    bool embedded{false};
};

class PdfToUnicodeCMap final {
public:
    static PdfToUnicodeCMap Parse(std::string_view source);
    [[nodiscard]] std::string Decode(std::string_view encodedBytes) const;
    [[nodiscard]] bool Empty() const noexcept { return mappings_.empty(); }
    [[nodiscard]] std::size_t MappingCount() const noexcept { return mappings_.size(); }
    [[nodiscard]] std::size_t CodeSpaceRangeCount() const noexcept { return codeSpaceRanges_.size(); }

private:
    struct CodeSpaceRange {
        std::uint32_t first{};
        std::uint32_t last{};
        std::uint8_t byteCount{1};
    };

    static std::uint64_t MappingKey(std::uint32_t code, std::uint8_t byteCount) noexcept;
    [[nodiscard]] bool IsInCodeSpace(std::uint32_t code, std::uint8_t byteCount) const noexcept;

    std::unordered_map<std::uint64_t, std::string> mappings_;
    std::vector<CodeSpaceRange> codeSpaceRanges_;
    std::uint8_t minimumCodeBytes_{1};
    std::uint8_t maximumCodeBytes_{1};
};

} // namespace CPPPdf

#include <CPPPdf/Fonts/PdfType1Font.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace CPPPdf {
namespace {

// Standard Type1 (Adobe) character widths in thousandths of an em, indexed by
// character code (WinAnsi-ish layout). Used when the program has no explicit
// /Widths array.
std::array<std::uint32_t, 256> defaultWidths() {
    std::array<std::uint32_t, 256> widths{};
    widths.fill(500);
    // ASCII printable range.
    widths[32] = 278; widths[33] = 278; widths[34] = 355; widths[35] = 556;
    widths[36] = 556; widths[37] = 889; widths[38] = 667; widths[39] = 191;
    widths[40] = 333; widths[41] = 333; widths[42] = 389; widths[43] = 584;
    widths[44] = 278; widths[45] = 333; widths[46] = 278; widths[47] = 278;
    widths[48] = 556; widths[49] = 556; widths[50] = 556; widths[51] = 556;
    widths[52] = 556; widths[53] = 556; widths[54] = 556; widths[55] = 556;
    widths[56] = 556; widths[57] = 556; widths[58] = 333; widths[59] = 333;
    widths[60] = 584; widths[61] = 584; widths[62] = 584; widths[63] = 611;
    widths[64] = 975; widths[65] = 667; widths[66] = 667; widths[67] = 722;
    widths[68] = 722; widths[69] = 667; widths[70] = 611; widths[71] = 778;
    widths[72] = 722; widths[73] = 278; widths[74] = 500; widths[75] = 667;
    widths[76] = 556; widths[77] = 833; widths[78] = 722; widths[79] = 778;
    widths[80] = 667; widths[81] = 778; widths[82] = 722; widths[83] = 667;
    widths[84] = 611; widths[85] = 722; widths[86] = 667; widths[87] = 944;
    widths[88] = 667; widths[89] = 667; widths[90] = 611; widths[91] = 333;
    widths[92] = 278; widths[93] = 333; widths[94] = 584; widths[95] = 556;
    widths[96] = 333; widths[97] = 556; widths[98] = 556; widths[99] = 500;
    widths[100] = 556; widths[101] = 556; widths[102] = 278; widths[103] = 556;
    widths[104] = 556; widths[105] = 222; widths[106] = 222; widths[107] = 500;
    widths[108] = 222; widths[109] = 833; widths[110] = 556; widths[111] = 556;
    widths[112] = 556; widths[113] = 556; widths[114] = 333; widths[115] = 500;
    widths[116] = 278; widths[117] = 556; widths[118] = 500; widths[119] = 722;
    widths[120] = 500; widths[121] = 500; widths[122] = 500; widths[123] = 334;
    widths[124] = 260; widths[125] = 334; widths[126] = 584;
    return widths;
}

// Extracts `/FontName (name)` or `/FontName /name` from a Type1 program.
std::string extractFontName(std::string_view program) {
    const auto pos = program.find("/FontName");
    if (pos == std::string_view::npos) return "Type1Font";
    auto cursor = pos + 9;
    while (cursor < program.size() && std::isspace(static_cast<unsigned char>(program[cursor]))) ++cursor;
    if (cursor >= program.size()) return "Type1Font";
    if (program[cursor] == '(') {
        const auto end = program.find(')', cursor);
        if (end == std::string_view::npos) return "Type1Font";
        return std::string(program.substr(cursor + 1, end - cursor - 1));
    }
    if (program[cursor] == '/') {
        auto end = cursor + 1;
        while (end < program.size() && !std::isspace(static_cast<unsigned char>(program[end]))) ++end;
        return std::string(program.substr(cursor + 1, end - cursor - 1));
    }
    return "Type1Font";
}

} // namespace

PdfType1Font PdfType1Font::Load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open Type1 font: " + path.string());
    return Parse(std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)), {}),
                 path.filename().string());
}

PdfType1Font PdfType1Font::Parse(std::vector<std::uint8_t> bytes, std::string sourceName) {
    PdfType1Font font;
    font.sourceName_ = std::move(sourceName);
    font.widths_ = defaultWidths();

    // Detect PFB (binary) vs PFA (ASCII).
    if (bytes.size() >= 2U && bytes[0] == 0x80U) {
        // PFB: segments prefixed by 0x80 <type> <len2> <len1>.
        std::vector<std::uint8_t> program;
        std::size_t pos = 0U;
        while (pos + 6U <= bytes.size() && bytes[pos] == 0x80U) {
            const std::uint8_t type = bytes[pos + 1U];
            const std::size_t length = static_cast<std::size_t>(bytes[pos + 2U]) |
                (static_cast<std::size_t>(bytes[pos + 3U]) << 8U) |
                (static_cast<std::size_t>(bytes[pos + 4U]) << 16U) |
                (static_cast<std::size_t>(bytes[pos + 5U]) << 24U);
            pos += 6U;
            if (pos + length > bytes.size()) break;
            if (type == 1U || type == 2U) { // ASCII or binary segment
                program.insert(program.end(), bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                               bytes.begin() + static_cast<std::ptrdiff_t>(pos + length));
            }
            pos += length;
        }
        if (!program.empty()) {
            font.fontName_ = extractFontName(std::string_view(
                reinterpret_cast<const char*>(program.data()), program.size()));
            bytes = std::move(program);
        }
    } else {
        font.fontName_ = extractFontName(std::string_view(
            reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    font.bytes_ = std::move(bytes);
    return font;
}

std::uint32_t PdfType1Font::GetGlyphWidth(const std::uint8_t code) const noexcept {
    return widths_[code];
}

} // namespace CPPPdf

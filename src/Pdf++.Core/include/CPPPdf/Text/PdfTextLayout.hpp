#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf {

// Unicode text processing helpers used by the layout primitives.
class PdfTextLayout final {
public:
    // Splits UTF-8 text into grapheme clusters (UAX #29, simplified: base
    // character + combining marks). Each returned string is a single grapheme.
    [[nodiscard]] static std::vector<std::string> GraphemeClusters(std::string_view utf8);

    // Simple Unicode bidirectional (UAX #9) reordering for a paragraph:
    // strong-directional runs (LTR/RTL) are detected and RTL runs are reversed
    // so the visual order is correct when the text is drawn left-to-right.
    // Returns the visual-order characters (UTF-8 code points).
    [[nodiscard]] static std::string ReorderBidi(std::string_view utf8, bool defaultRtl = false);
};

} // namespace CPPPdf

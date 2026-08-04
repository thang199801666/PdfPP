#pragma once

#include <cstdint>
#include <functional>
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

    // Basic Arabic shaping: joins Arabic letters into their contextual
    // presentation forms (isolated/initial/medial/final) using the standard
    // Arabic presentation forms. Letters not joined (at run edges or preceded
    // by a non-joining character) keep their isolated form. The input is
    // assumed to be in logical order with bidi already applied.
    [[nodiscard]] static std::string ShapeArabic(std::string_view utf8);

    // Counts the Unicode code points in a UTF-8 string.
    [[nodiscard]] static std::size_t CountCodePoints(std::string_view utf8);

    // Truncates UTF-8 text to at most `maxCodePoints` code points, cutting at
    // a grapheme boundary and appending `ellipsis` when truncation happened.
    [[nodiscard]] static std::string TruncateUtf8(
        std::string_view utf8,
        std::size_t maxCodePoints,
        std::string_view ellipsis = "...");

    // Removes combining marks and variation selectors, returning base-only
    // text (useful for accent-insensitive matching and normalization).
    [[nodiscard]] static std::string StripCombiningMarks(std::string_view utf8);

    // Normalizes combining sequences to precomposed characters (NFC) for the
    // common Latin block (e.g. "e" + combining acute -> U+00E9).
    [[nodiscard]] static std::string NormalizeNfc(std::string_view utf8);

    // Replaces Latin precomposed letters with their ASCII base (é -> e, ö -> o,
    // ç -> c, ...), keeping non-Latin code points unchanged.
    [[nodiscard]] static std::string RemoveDiacritics(std::string_view utf8);

    // Unicode-aware case conversion for ASCII and Latin-1 letters; other code
    // points are preserved.
    [[nodiscard]] static std::string ToUpper(std::string_view utf8);
    [[nodiscard]] static std::string ToLower(std::string_view utf8);

    // Wraps UTF-8 text into lines of at most maxWidth using the supplied
    // measure callback (returns the advance of a substring). Long words are
    // split at grapheme boundaries.
    [[nodiscard]] static std::vector<std::string> WordWrap(
        std::string_view utf8,
        double maxWidth,
        const std::function<double(std::string_view)>& measure);

    // True when the string consists only of Unicode whitespace.
    [[nodiscard]] static bool IsWhitespace(std::string_view utf8);

    // Removes leading and trailing Unicode whitespace.
    [[nodiscard]] static std::string TrimWhitespace(std::string_view utf8);

    // Splits text into lines on \n and \r\n (keeps empty lines).
    [[nodiscard]] static std::vector<std::string> SplitLines(std::string_view utf8);
};

} // namespace CPPPdf

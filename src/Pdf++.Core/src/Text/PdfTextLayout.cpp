#include <CPPPdf/Text/PdfTextLayout.hpp>

#include <algorithm>
#include <cstddef>

namespace CPPPdf {
namespace {

// UTF-8 decoder used across the text helpers.
void decodeUtf8View(std::string_view text, std::vector<std::uint32_t>& out) {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t n = 0;
        if (c < 0x80U) { cp = c; n = 1; }
        else if ((c & 0xE0U) == 0xC0U) { cp = c & 0x1FU; n = 2; }
        else if ((c & 0xF0U) == 0xE0U) { cp = c & 0x0FU; n = 3; }
        else if ((c & 0xF8U) == 0xF0U) { cp = c & 0x07U; n = 4; }
        else { ++i; continue; }
        if (i + n > text.size()) break;
        for (std::size_t j = 1; j < n; ++j) {
            cp = (cp << 6U) | (static_cast<unsigned char>(text[i + j]) & 0x3FU);
        }
        out.push_back(cp);
        i += n;
    }
}

// Encoding a single code point back to UTF-8.
void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80U) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

// Bidi class approximation (UAX #9 general categories).
enum class BidiClass { L, R, AL, EN, AN, ES, CS, ET, ON, NSM, WS, Other };

BidiClass bidiClassOf(const std::uint32_t cp) {
    // Hebrew.
    if (cp >= 0x0590U && cp <= 0x05FFU) return BidiClass::R;
    // Arabic.
    if ((cp >= 0x0600U && cp <= 0x06FFU) || (cp >= 0x0750U && cp <= 0x077FU) ||
        (cp >= 0x08A0U && cp <= 0x08FFU)) return BidiClass::AL;
    // Combining marks (Mn/Mc/Me) treated as NSM-like.
    if ((cp >= 0x0300U && cp <= 0x036FU) || (cp >= 0x1AB0U && cp <= 0x1AFFU) ||
        (cp >= 0x1DC0U && cp <= 0x1DFFU) || (cp >= 0x20D0U && cp <= 0x20FFU)) return BidiClass::NSM;
    // European digits.
    if (cp >= 0x0030U && cp <= 0x0039U) return BidiClass::EN;
    if (cp == 0x002EU || cp == 0x002CU) return BidiClass::CS;
    if (cp == 0x002DU || cp == 0x002BU) return BidiClass::ES;
    if (cp == 0x0024U || cp == 0x00A3U || cp == 0x00A5U || cp == 0x20ACU) return BidiClass::ET;
    if (cp == 0x0020U || cp == 0x0009U) return BidiClass::WS;
    if (cp == 0x0028U || cp == 0x0029U || cp == 0x0022U || cp == 0x0027U) return BidiClass::ON;
    return BidiClass::L;
}

} // namespace

std::vector<std::string> PdfTextLayout::GraphemeClusters(std::string_view utf8) {
    std::vector<std::uint32_t> cps;
    decodeUtf8View(utf8, cps);
    std::vector<std::string> clusters;
    std::string current;
    std::uint32_t base = 0;
    bool haveBase = false;
    for (const auto cp : cps) {
        const bool combining = (cp >= 0x0300U && cp <= 0x036FU) ||
            (cp >= 0x1AB0U && cp <= 0x1AFFU) ||
            (cp >= 0x1DC0U && cp <= 0x1DFFU) ||
            (cp >= 0x20D0U && cp <= 0x20FFU);
        if (!haveBase) {
            current.clear();
            appendUtf8(current, cp);
            base = cp;
            haveBase = true;
            continue;
        }
        if (combining) {
            appendUtf8(current, cp);
            continue;
        }
        clusters.push_back(current);
        current.clear();
        appendUtf8(current, cp);
        base = cp;
    }
    if (haveBase) clusters.push_back(current);
    return clusters;
}

std::string PdfTextLayout::ReorderBidi(std::string_view utf8, const bool defaultRtl) {
    std::vector<std::uint32_t> cps;
    decodeUtf8View(utf8, cps);
    if (cps.empty()) return {};

    // Determine the paragraph base direction from the first strong character.
    bool baseRtl = defaultRtl;
    for (const auto cp : cps) {
        const auto cls = bidiClassOf(cp);
        if (cls == BidiClass::L) { baseRtl = false; break; }
        if (cls == BidiClass::R || cls == BidiClass::AL) { baseRtl = true; break; }
    }

    // UAX #9 (simplified): split into runs by strong direction, reverse the
    // runs that oppose the base direction. This gives the correct visual order
    // for mixed LTR/RTL paragraphs rendered left-to-right.
    struct Run { std::vector<std::uint32_t> chars; bool rtl; };
    std::vector<Run> runs;
    bool currentRtl = baseRtl;
    for (const auto cp : cps) {
        BidiClass cls = bidiClassOf(cp);
        // Neutral characters take the current run's direction.
        bool isRtl = currentRtl;
        if (cls == BidiClass::L) isRtl = false;
        else if (cls == BidiClass::R || cls == BidiClass::AL) isRtl = true;
        if (runs.empty() || runs.back().rtl != isRtl) {
            runs.push_back({{}, isRtl});
        }
        runs.back().chars.push_back(cp);
        currentRtl = isRtl;
    }

    std::string out;
    for (const auto& run : runs) {
        if (run.rtl == baseRtl) {
            // Same as base: visual order is logical for LTR base, reversed for RTL.
            if (baseRtl) for (auto it = run.chars.rbegin(); it != run.chars.rend(); ++it) appendUtf8(out, *it);
            else for (const auto cp : run.chars) appendUtf8(out, cp);
        } else {
            // Opposite run: reverse it.
            for (auto it = run.chars.rbegin(); it != run.chars.rend(); ++it) appendUtf8(out, *it);
        }
    }
    return out;
}

} // namespace CPPPdf

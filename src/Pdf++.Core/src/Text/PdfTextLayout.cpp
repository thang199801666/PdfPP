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

std::string PdfTextLayout::ShapeArabic(std::string_view utf8) {
    std::vector<std::uint32_t> cps;
    decodeUtf8View(utf8, cps);
    if (cps.empty()) return {};

    // Arabic presentation forms for the basic letters (U+0621..U+064A subset).
    // isJoining returns true for letters that join on both sides.
    const auto hasJoining = [](const std::uint32_t cp) {
        if (cp == 0x0621U) return false; // hamza does not join
        return (cp >= 0x0622U && cp <= 0x062AU) || // alef..ta
               (cp >= 0x062BU && cp <= 0x063AU) ||
               (cp >= 0x0641U && cp <= 0x064AU);
    };
    const auto presentationForm = [](const std::uint32_t cp, const bool initial, const bool medial, const bool final) {
        if (cp == 0x0622U) return final ? 0xFE82U : (initial ? 0xFE81U : (medial ? 0xFE82U : 0xFE81U));
        if (cp == 0x0623U) return final ? 0xFE84U : (initial ? 0xFE83U : (medial ? 0xFE84U : 0xFE83U));
        if (cp == 0x0625U) return final ? 0xFE88U : (initial ? 0xFE87U : (medial ? 0xFE88U : 0xFE87U));
        if (cp == 0x0626U) return final ? 0xFE8AU : (initial ? 0xFE89U : (medial ? 0xFE8AU : 0xFE89U));
        if (cp == 0x0627U) return final ? 0xFE8EU : (initial ? 0xFE8DU : (medial ? 0xFE8EU : 0xFE8DU));
        if (cp == 0x0628U) return final ? 0xFE90U : (initial ? 0xFE91U : (medial ? 0xFE92U : 0xFE8FU));
        if (cp == 0x0629U) return final ? 0xFE94U : (initial ? 0xFE93U : (medial ? 0xFE94U : 0xFE93U));
        if (cp == 0x062AU) return final ? 0xFE96U : (initial ? 0xFE97U : (medial ? 0xFE98U : 0xFE95U));
        if (cp == 0x062BU) return final ? 0xFE9AU : (initial ? 0xFE9BU : (medial ? 0xFE9CU : 0xFE99U));
        if (cp == 0x062CU) return final ? 0xFE9EU : (initial ? 0xFE9FU : (medial ? 0xFEA0U : 0xFE9DU));
        if (cp == 0x062DU) return final ? 0xFEA2U : (initial ? 0xFEA3U : (medial ? 0xFEA4U : 0xFEA1U));
        if (cp == 0x062EU) return final ? 0xFEA6U : (initial ? 0xFEA7U : (medial ? 0xFEA8U : 0xFEA5U));
        if (cp == 0x062FU) return final ? 0xFEAAU : (initial ? 0xFEABU : (medial ? 0xFEACU : 0xFEA9U));
        if (cp == 0x0630U) return final ? 0xFEAEU : (initial ? 0xFEAFU : (medial ? 0xFEB0U : 0xFEADU));
        if (cp == 0x0631U) return final ? 0xFEB2U : (initial ? 0xFEB3U : (medial ? 0xFEB4U : 0xFEB1U));
        if (cp == 0x0632U) return final ? 0xFEB6U : (initial ? 0xFEB7U : (medial ? 0xFEB8U : 0xFEB5U));
        if (cp == 0x0633U) return final ? 0xFEBAU : (initial ? 0xFEBBU : (medial ? 0xFEBCU : 0xFEB9U));
        if (cp == 0x0634U) return final ? 0xFEBEU : (initial ? 0xFEBFU : (medial ? 0xFEC0U : 0xFEBDU));
        if (cp == 0x0635U) return final ? 0xFEC2U : (initial ? 0xFEC3U : (medial ? 0xFEC4U : 0xFEC1U));
        if (cp == 0x0636U) return final ? 0xFEC6U : (initial ? 0xFEC7U : (medial ? 0xFEC8U : 0xFEC5U));
        if (cp == 0x0637U) return final ? 0xFECAU : (initial ? 0xFECBU : (medial ? 0xFECCU : 0xFEC9U));
        if (cp == 0x0638U) return final ? 0xFECEU : (initial ? 0xFECFU : (medial ? 0xFED0U : 0xFECDU));
        if (cp == 0x0639U) return final ? 0xFED2U : (initial ? 0xFED3U : (medial ? 0xFED4U : 0xFED1U));
        if (cp == 0x063AU) return final ? 0xFED6U : (initial ? 0xFED7U : (medial ? 0xFED8U : 0xFED5U));
        if (cp == 0x0641U) return final ? 0xFEDAU : (initial ? 0xFEDBU : (medial ? 0xFEDCU : 0xFED9U));
        if (cp == 0x0642U) return final ? 0xFEDEU : (initial ? 0xFEDFU : (medial ? 0xFEE0U : 0xFEDDU));
        if (cp == 0x0643U) return final ? 0xFEE2U : (initial ? 0xFEE3U : (medial ? 0xFEE4U : 0xFEE1U));
        if (cp == 0x0644U) return final ? 0xFEE6U : (initial ? 0xFEE7U : (medial ? 0xFEE8U : 0xFEE5U));
        if (cp == 0x0645U) return final ? 0xFEEAU : (initial ? 0xFEEBU : (medial ? 0xFEECU : 0xFEE9U));
        if (cp == 0x0646U) return final ? 0xFEEEU : (initial ? 0xFEEFU : (medial ? 0xFEF0U : 0xFEEDU));
        if (cp == 0x0647U) return final ? 0xFEF2U : (initial ? 0xFEF3U : (medial ? 0xFEF4U : 0xFEF1U));
        if (cp == 0x0648U) return final ? 0xFEF6U : (initial ? 0xFEF7U : (medial ? 0xFEF8U : 0xFEF5U));
        if (cp == 0x0649U) return final ? 0xFEFAU : (initial ? 0xFEFBU : (medial ? 0xFEFCU : 0xFEF9U));
        if (cp == 0x064AU) return final ? 0xFEFEU : (initial ? 0xFEFFU : (medial ? 0xFEFEU : 0xFEFDU));
        return cp;
    };

    std::string out;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        const std::uint32_t cp = cps[i];
        if (!hasJoining(cp)) {
            appendUtf8(out, cp);
            continue;
        }
        const bool prevJoins = i > 0U && hasJoining(cps[i - 1U]);
        const bool nextJoins = i + 1U < cps.size() && hasJoining(cps[i + 1U]);
        // Initial form when a joining character follows; final form when one
        // precedes; medial when both.
        const bool initial = nextJoins;
        const bool final = prevJoins;
        const bool medial = prevJoins && nextJoins;
        appendUtf8(out, presentationForm(cp, initial, medial, final));
    }
    return out;
}

} // namespace CPPPdf

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

std::size_t PdfTextLayout::CountCodePoints(const std::string_view utf8) {
    std::vector<std::uint32_t> cps;
    decodeUtf8View(utf8, cps);
    return cps.size();
}

namespace {
// True for combining marks, variation selectors, ZWJ, and zero-width joiners
// that must stay attached to their base code point when truncating.
bool isCombiningMark(const std::uint32_t cp) {
    return (cp >= 0x0300U && cp <= 0x036FU) ||
           (cp >= 0x1AB0U && cp <= 0x1AFFU) ||
           (cp >= 0x1DC0U && cp <= 0x1DFFU) ||
           (cp >= 0x20D0U && cp <= 0x20FFU) ||
           (cp >= 0xFE00U && cp <= 0xFE0FU) ||
           cp == 0x200DU || cp == 0x200CU || cp == 0xFEFFU;
}
} // namespace

std::string PdfTextLayout::TruncateUtf8(
    const std::string_view utf8,
    const std::size_t maxCodePoints,
    const std::string_view ellipsis) {
    if (maxCodePoints == 0U) return std::string(ellipsis);
    std::vector<std::uint32_t> cps;
    decodeUtf8View(utf8, cps);
    if (cps.size() <= maxCodePoints) return std::string(utf8);
    std::size_t take = maxCodePoints;
    // Do not split a base + combining-mark cluster.
    while (take > 0U && take < cps.size() && isCombiningMark(cps[take])) --take;
    std::string out;
    for (std::size_t i = 0; i < take; ++i) appendUtf8(out, cps[i]);
    out += ellipsis;
    return out;
}

std::string PdfTextLayout::StripCombiningMarks(const std::string_view utf8) {
    std::vector<std::uint32_t> cps;
    decodeUtf8View(utf8, cps);
    std::string out;
    for (const std::uint32_t cp : cps) {
        if (isCombiningMark(cp)) continue;
        appendUtf8(out, cp);
    }
    return out;
}

namespace {
constexpr std::uint32_t kDotAbove = 0x0307U;
// Composes a Latin base + combining mark into its precomposed form. Returns 0
// when no precomposed character exists for the pair.
std::uint32_t composeLatin(const std::uint32_t base, const std::uint32_t mark) {
    constexpr std::uint32_t kGrave = 0x0300U, kAcute = 0x0301U, kCircumflex = 0x0302U,
                            kTilde = 0x0303U, kMacron = 0x0304U, kBreve = 0x0306U,
                            kDiaeresis = 0x0308U, kRing = 0x030AU, kCedilla = 0x0327U,
                            kOgonek = 0x0328U;
    switch (base) {
    case 'a':
        if (mark == kGrave) return 0x00E0; if (mark == kAcute) return 0x00E1;
        if (mark == kCircumflex) return 0x00E2; if (mark == kTilde) return 0x00E3;
        if (mark == kDiaeresis) return 0x00E4; if (mark == kRing) return 0x00E5;
        if (mark == kBreve) return 0x0103; if (mark == kOgonek) return 0x0105;
        break;
    case 'A':
        if (mark == kGrave) return 0x00C0; if (mark == kAcute) return 0x00C1;
        if (mark == kCircumflex) return 0x00C2; if (mark == kTilde) return 0x00C3;
        if (mark == kDiaeresis) return 0x00C4; if (mark == kRing) return 0x00C5;
        if (mark == kBreve) return 0x0102; if (mark == kOgonek) return 0x0104;
        break;
    case 'e':
        if (mark == kGrave) return 0x00E8; if (mark == kAcute) return 0x00E9;
        if (mark == kCircumflex) return 0x00EA; if (mark == kDiaeresis) return 0x00EB;
        if (mark == kMacron) return 0x0113; if (mark == kBreve) return 0x0115;
        if (mark == kOgonek) return 0x0119; if (mark == kCedilla) return 0x0229;
        break;
    case 'E':
        if (mark == kGrave) return 0x00C8; if (mark == kAcute) return 0x00C9;
        if (mark == kCircumflex) return 0x00CA; if (mark == kDiaeresis) return 0x00CB;
        if (mark == kMacron) return 0x0112; if (mark == kBreve) return 0x0114;
        if (mark == kOgonek) return 0x0118; if (mark == kCedilla) return 0x0228;
        break;
    case 'i':
        if (mark == kGrave) return 0x00EC; if (mark == kAcute) return 0x00ED;
        if (mark == kCircumflex) return 0x00EE; if (mark == kDiaeresis) return 0x00EF;
        if (mark == kMacron) return 0x012B; if (mark == kBreve) return 0x012D;
        if (mark == kOgonek) return 0x012F;
        break;
    case 'I':
        if (mark == kGrave) return 0x00CC; if (mark == kAcute) return 0x00CD;
        if (mark == kCircumflex) return 0x00CE; if (mark == kDiaeresis) return 0x00CF;
        if (mark == kMacron) return 0x012A; if (mark == kBreve) return 0x012C;
        if (mark == kOgonek) return 0x012E;
        break;
    case 'o':
        if (mark == kGrave) return 0x00F2; if (mark == kAcute) return 0x00F3;
        if (mark == kCircumflex) return 0x00F4; if (mark == kTilde) return 0x00F5;
        if (mark == kDiaeresis) return 0x00F6; if (mark == kMacron) return 0x014D;
        if (mark == kBreve) return 0x014F; if (mark == kOgonek) return 0x01EB;
        break;
    case 'O':
        if (mark == kGrave) return 0x00D2; if (mark == kAcute) return 0x00D3;
        if (mark == kCircumflex) return 0x00D4; if (mark == kTilde) return 0x00D5;
        if (mark == kDiaeresis) return 0x00D6; if (mark == kMacron) return 0x014C;
        if (mark == kBreve) return 0x014E; if (mark == kOgonek) return 0x01EA;
        break;
    case 'u':
        if (mark == kGrave) return 0x00F9; if (mark == kAcute) return 0x00FA;
        if (mark == kCircumflex) return 0x00FB; if (mark == kDiaeresis) return 0x00FC;
        if (mark == kMacron) return 0x016B; if (mark == kBreve) return 0x016D;
        if (mark == kRing) return 0x016F; if (mark == kOgonek) return 0x0173;
        break;
    case 'U':
        if (mark == kGrave) return 0x00D9; if (mark == kAcute) return 0x00DA;
        if (mark == kCircumflex) return 0x00DB; if (mark == kDiaeresis) return 0x00DC;
        if (mark == kMacron) return 0x016A; if (mark == kBreve) return 0x016C;
        if (mark == kRing) return 0x016E; if (mark == kOgonek) return 0x0172;
        break;
    case 'n':
        if (mark == kTilde) return 0x00F1; if (mark == kAcute) return 0x0144;
        if (mark == kGrave) return 0x01F9;
        break;
    case 'N':
        if (mark == kTilde) return 0x00D1; if (mark == kAcute) return 0x0143;
        if (mark == kGrave) return 0x01F8;
        break;
    case 'c':
        if (mark == kCedilla) return 0x00E7; if (mark == kAcute) return 0x0107;
        if (mark == kCircumflex) return 0x0109; if (mark == kBreve) return 0x010D;
        break;
    case 'C':
        if (mark == kCedilla) return 0x00C7; if (mark == kAcute) return 0x0106;
        if (mark == kCircumflex) return 0x0108; if (mark == kBreve) return 0x010C;
        break;
    case 'y':
        if (mark == kAcute) return 0x00FD; if (mark == kDiaeresis) return 0x00FF;
        if (mark == kGrave) return 0x1EF3; if (mark == kCircumflex) return 0x0177;
        break;
    case 'Y':
        if (mark == kAcute) return 0x00DD; if (mark == kGrave) return 0x1EF2;
        if (mark == kCircumflex) return 0x0176; if (mark == kDiaeresis) return 0x0178;
        break;
    case 's':
        if (mark == kAcute) return 0x015B; if (mark == kCedilla) return 0x015F;
        if (mark == kCircumflex) return 0x015D;
        break;
    case 'S':
        if (mark == kAcute) return 0x015A; if (mark == kCedilla) return 0x015E;
        if (mark == kCircumflex) return 0x015C;
        break;
    case 'z':
        if (mark == kAcute) return 0x017A; if (mark == kCircumflex) return 0x017C;
        if (mark == kDotAbove) return 0x017C;
        break;
    default:
        break;
    }
    return 0U;
}
} // namespace

std::string PdfTextLayout::NormalizeNfc(const std::string_view utf8) {
    std::vector<std::uint32_t> cps;
    decodeUtf8View(utf8, cps);
    std::string out;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        if (i + 1U < cps.size() && isCombiningMark(cps[i + 1U])) {
            const std::uint32_t composed = composeLatin(cps[i], cps[i + 1U]);
            if (composed != 0U) {
                appendUtf8(out, composed);
                i += 2U;
                continue;
            }
        }
        appendUtf8(out, cps[i]);
    }
    return out;
}

std::string PdfTextLayout::RemoveDiacritics(const std::string_view utf8) {
    std::vector<std::uint32_t> cps;
    decodeUtf8View(utf8, cps);
    std::string out;
    for (const std::uint32_t cp : cps) {
        if (isCombiningMark(cp)) continue;
        char base = 0;
        switch (cp) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
        case 0x0100: case 0x0102: case 0x0104: case 0x01DE: case 0x01FA: base = 'A'; break;
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
        case 0x0101: case 0x0103: case 0x0105: case 0x01DF: case 0x01FB: base = 'a'; break;
        case 0x00C7: case 0x0106: case 0x0108: case 0x010A: case 0x010C: base = 'C'; break;
        case 0x00E7: case 0x0107: case 0x0109: case 0x010B: case 0x010D: base = 'c'; break;
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
        case 0x0112: case 0x0114: case 0x0116: case 0x0118: case 0x011A:
        case 0x0200: case 0x0202: case 0x0204: base = 'E'; break;
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
        case 0x0113: case 0x0115: case 0x0117: case 0x0119: case 0x011B:
        case 0x0201: case 0x0203: case 0x0205: base = 'e'; break;
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
        case 0x0128: case 0x012A: case 0x012C: case 0x012E: case 0x0130: base = 'I'; break;
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
        case 0x0129: case 0x012B: case 0x012D: case 0x012F: case 0x0131: base = 'i'; break;
        case 0x00D1: case 0x0143: case 0x0145: case 0x0147: case 0x014A: base = 'N'; break;
        case 0x00F1: case 0x0144: case 0x0146: case 0x0148: case 0x014B: base = 'n'; break;
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6:
        case 0x014C: case 0x014E: case 0x0150: case 0x01EA: base = 'O'; break;
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6:
        case 0x014D: case 0x014F: case 0x0151: case 0x01EB: base = 'o'; break;
        case 0x0154: case 0x0156: case 0x0158: base = 'R'; break;
        case 0x0155: case 0x0157: case 0x0159: base = 'r'; break;
        case 0x015A: case 0x015C: case 0x015E: base = 'S'; break;
        case 0x015B: case 0x015D: case 0x015F: base = 's'; break;
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
        case 0x0168: case 0x016A: case 0x016C: case 0x016E: case 0x0170: case 0x0172:
        case 0x01DB: base = 'U'; break;
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
        case 0x0169: case 0x016B: case 0x016D: case 0x016F: case 0x0171: case 0x0173:
        case 0x01DC: base = 'u'; break;
        case 0x00DD: case 0x0176: case 0x0178: base = 'Y'; break;
        case 0x00FD: case 0x00FF: case 0x0177: base = 'y'; break;
        case 0x0179: case 0x017B: case 0x017D: base = 'Z'; break;
        case 0x017A: case 0x017C: case 0x017E: base = 'z'; break;
        case 0x00C6: base = 'A'; break;
        case 0x00E6: base = 'a'; break;
        case 0x00D8: base = 'O'; break;
        case 0x00F8: base = 'o'; break;
        case 0x0110: base = 'D'; break;
        case 0x0111: base = 'd'; break;
        case 0x00DE: base = 'T'; break;
        case 0x00FE: base = 't'; break;
        case 0x00D0: base = 'D'; break;
        case 0x00F0: base = 'd'; break;
        default: break;
        }
        if (base != 0) { out.push_back(base); continue; }
        appendUtf8(out, cp);
    }
    return out;
}

} // namespace CPPPdf

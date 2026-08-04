#include <CPPPdf/Fonts/PdfCff.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace CPPPdf {
namespace {

std::uint32_t readOffset(const std::span<const std::byte> bytes, const std::size_t offset, const std::size_t width) {
    if (width == 0U || width > 4U || offset + width > bytes.size()) throw std::runtime_error("Malformed CFF offset.");
    std::uint32_t value{};
    for (std::size_t i = 0; i < width; ++i) value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[offset + i]);
    return value;
}

std::uint32_t readCard8Or16(const std::span<const std::byte> bytes, std::size_t& offset, const bool cid) {
    if (offset >= bytes.size()) throw std::runtime_error("Malformed CFF charset.");
    if (cid) {
        if (offset + 2U > bytes.size()) throw std::runtime_error("Malformed CFF charset.");
        const std::uint32_t value = (std::to_integer<std::uint8_t>(bytes[offset]) << 8U) |
            std::to_integer<std::uint8_t>(bytes[offset + 1U]);
        offset += 2U;
        return value;
    }
    return std::to_integer<std::uint8_t>(bytes[offset++]);
}

struct Type2Interpreter final {
    const PdfCffFont* font{};
    std::span<const std::byte> program{};
    std::vector<double> stack;
    std::vector<PdfCffOutlineSegment> segments;
    double width{0.0};
    bool widthParsed{false};
    std::size_t hintCount{};
    double x{};
    double y{};
    bool hasPosition{false};
    int callDepth{};
    static constexpr int kMaxCallDepth = 10;
    static constexpr int kMaxStack = 96;

    void Push(const double value) {
        if (stack.size() >= kMaxStack) throw std::runtime_error("CFF charstring stack overflow.");
        stack.push_back(value);
    }

    double Pop() {
        if (stack.empty()) throw std::runtime_error("CFF charstring stack underflow.");
        const double value = stack.back();
        stack.pop_back();
        return value;
    }

    void Move(const double dx, const double dy) {
        x += dx;
        y += dy;
        hasPosition = true;
        segments.push_back({PdfCffOutlineSegment::Type::Move, x, y, 0.0, 0.0, 0.0, 0.0});
    }

    void Line(const double dx, const double dy) {
        x += dx;
        y += dy;
        hasPosition = true;
        segments.push_back({PdfCffOutlineSegment::Type::Line, x, y, 0.0, 0.0, 0.0, 0.0});
    }

    void Cubic(const double dx1, const double dy1, const double dx2, const double dy2,
               const double dx3, const double dy3) {
        const double c1x = x + dx1;
        const double c1y = y + dy1;
        const double c2x = c1x + dx2;
        const double c2y = c1y + dy2;
        x = c2x + dx3;
        y = c2y + dy3;
        hasPosition = true;
        segments.push_back({PdfCffOutlineSegment::Type::Cubic, c1x, c1y, c2x, c2y, x, y});
    }

    // Drains the stack into a front-ordered vector for the variable-arity curve
    // operators whose optional leading control offset lives at the front.
    std::vector<double> Drain() {
        std::vector<double> values = std::move(stack);
        stack.clear();
        return values;
    }

    static std::size_t SubrBias(const std::size_t count) noexcept {
        if (count < 1240U) return 107U;
        if (count < 33900U) return 1131U;
        return 32768U;
    }

    // The first stack-clearing operator may carry a width operand. The width is
    // an extra stack entry when the operator's argument count is odd (hstem,
    // vstem, hintmask, cntrmask) or one more than the fixed arity for move ops.
    void MaybeConsumeWidth(const std::size_t minimum) {
        if (widthParsed) return;
        widthParsed = true;
        if (stack.size() > minimum) {
            const double delta = stack.front();
            stack.erase(stack.begin());
            width = delta + font->privateDict.nominalWidthX;
        } else {
            width = font->privateDict.defaultWidthX;
        }
    }

    // Emits curve groups. Each curve is 4 args (dx2 dy2 dx3 dy3); when the total
    // arg count is odd the first curve additionally carries a leading control
    // offset. `firstHorizontal` selects the axis of the leading control.
    void Curves(std::vector<double> args, const bool leadingHorizontal) {
        const bool hasLeading = (args.size() % 2U) != 0U;
        std::size_t index = 0U;
        double leading = 0.0;
        if (hasLeading) leading = args[index++];
        bool horizontal = leadingHorizontal;
        while (index + 4U <= args.size()) {
            const double dx2 = args[index++];
            const double dy2 = args[index++];
            const double dx3 = args[index++];
            const double dy3 = args[index++];
            const double dx1 = horizontal ? leading : 0.0;
            const double dy1 = horizontal ? 0.0 : leading;
            Cubic(dx1, dy1, dx2, dy2, dx3, dy3);
            leading = 0.0;
            horizontal = !horizontal;
        }
    }

    void Run(const std::span<const std::byte> bytes) {
        program = bytes;
        std::size_t offset{};
        while (offset < program.size()) {
            const auto byte = std::to_integer<std::uint8_t>(program[offset++]);
            if (byte <= 21U) {
                switch (byte) {
                case 1: case 3: case 18: case 23: { // hstem / vstem / hstemhm / vstemhm
                    MaybeConsumeWidth(0);
                    hintCount += stack.size() / 2U;
                    stack.clear();
                    break;
                }
                case 4: { // vmoveto
                    MaybeConsumeWidth(1);
                    const double dy = Pop();
                    stack.clear();
                    Move(0.0, dy);
                    break;
                }
                case 5: { // rlineto
                    while (stack.size() >= 2U) {
                        const double dy = Pop();
                        const double dx = Pop();
                        Line(dx, dy);
                    }
                    break;
                }
                case 6: { // hlineto (alternating dx, dy)
                    bool horizontal = true;
                    while (!stack.empty()) {
                        const double value = Pop();
                        if (horizontal) Line(value, 0.0);
                        else Line(0.0, value);
                        horizontal = !horizontal;
                    }
                    break;
                }
                case 7: { // vlineto (alternating dy, dx)
                    bool vertical = true;
                    while (!stack.empty()) {
                        const double value = Pop();
                        if (vertical) Line(0.0, value);
                        else Line(value, 0.0);
                        vertical = !vertical;
                    }
                    break;
                }
                case 8: { // rrcurveto (groups of six)
                    while (stack.size() >= 6U) {
                        const double dy3 = Pop();
                        const double dx3 = Pop();
                        const double dy2 = Pop();
                        const double dx2 = Pop();
                        const double dy1 = Pop();
                        const double dx1 = Pop();
                        Cubic(dx1, dy1, dx2, dy2, dx3, dy3);
                    }
                    break;
                }
                case 10: { // callsubr
                    const int index = static_cast<int>(Pop());
                    CallSubr(index, true);
                    break;
                }
                case 11: // return
                    return;
                case 14: { // endchar (possibly seac with 4 args or width)
                    MaybeConsumeWidth(0);
                    return;
                }
                case 19: case 20: { // hintmask / cntrmask
                    MaybeConsumeWidth(0);
                    const std::size_t maskBytes = (hintCount + 7U) / 8U;
                    offset = std::min(offset + maskBytes, program.size());
                    stack.clear();
                    break;
                }
                case 21: { // rmoveto
                    MaybeConsumeWidth(2);
                    const double dy = Pop();
                    const double dx = Pop();
                    stack.clear();
                    Move(dx, dy);
                    break;
                }
                case 22: { // hmoveto
                    MaybeConsumeWidth(1);
                    const double dx = Pop();
                    stack.clear();
                    Move(dx, 0.0);
                    break;
                }
                case 24: { // rcurveline
                    while (stack.size() >= 6U) {
                        const double dy3 = Pop();
                        const double dx3 = Pop();
                        const double dy2 = Pop();
                        const double dx2 = Pop();
                        const double dy1 = Pop();
                        const double dx1 = Pop();
                        Cubic(dx1, dy1, dx2, dy2, dx3, dy3);
                    }
                    if (stack.size() == 2U) {
                        const double dy = Pop();
                        const double dx = Pop();
                        Line(dx, dy);
                    }
                    break;
                }
                case 25: { // rlinecurve
                    while (stack.size() > 6U) {
                        const double dy = Pop();
                        const double dx = Pop();
                        Line(dx, dy);
                    }
                    if (stack.size() >= 6U) {
                        const double dy3 = Pop();
                        const double dx3 = Pop();
                        const double dy2 = Pop();
                        const double dx2 = Pop();
                        const double dy1 = Pop();
                        const double dx1 = Pop();
                        Cubic(dx1, dy1, dx2, dy2, dx3, dy3);
                    }
                    break;
                }
                case 26: { // vvcurveto (first control always vertical)
                    Curves(Drain(), false);
                    break;
                }
                case 27: { // hhcurveto (first control always horizontal)
                    Curves(Drain(), true);
                    break;
                }
                case 29: { // callgsubr
                    const int index = static_cast<int>(Pop());
                    CallSubr(index, false);
                    break;
                }
                case 30: { // vhcurveto (alternates, starts vertical)
                    Curves(Drain(), false);
                    break;
                }
                case 31: { // hvcurveto (alternates, starts horizontal)
                    Curves(Drain(), true);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported Type 2 charstring operator.");
                }
                continue;
            }

            if (byte == 12U) {
                // Escape operators: hflex, flex, hflex1, flex1.
                if (offset >= program.size()) throw std::runtime_error("Malformed CFF escape operator.");
                const auto escape = std::to_integer<std::uint8_t>(program[offset++]);
                auto args = Drain();
                std::size_t index = 0U;
                const auto next = [&]() {
                    if (index >= args.size()) throw std::runtime_error("Malformed CFF flex operator.");
                    return args[index++];
                };
                switch (escape) {
                case 34U: { // hflex: dx1 dx2 dy2 dx3 dx4 dx5 dx6
                    const double dx1 = next(), dx2 = next(), dy2 = next(), dx3 = next(),
                                 dx4 = next(), dx5 = next(), dx6 = next();
                    Cubic(dx1, 0.0, dx2, dy2, dx3, 0.0);
                    Cubic(dx4, 0.0, dx5, -dy2, dx6, 0.0);
                    break;
                }
                case 35U: { // flex: dx1 dy1 dx2 dy2 dx3 dy3 dx4 dy4 dx5 dy5 dx6 dy6 fd
                    const double dx1 = next(), dy1 = next(), dx2 = next(), dy2 = next(),
                                 dx3 = next(), dy3 = next(), dx4 = next(), dy4 = next(),
                                 dx5 = next(), dy5 = next(), dx6 = next(), dy6 = next();
                    (void)next(); // flex depth
                    Cubic(dx1, dy1, dx2, dy2, dx3, dy3);
                    Cubic(dx4, dy4, dx5, dy5, dx6, dy6);
                    break;
                }
                case 36U: { // hflex1: dx1 dy1 dx2 dy2 dx3 dx4 dx5 dy5 dx6
                    const double dx1 = next(), dy1 = next(), dx2 = next(), dy2 = next(),
                                 dx3 = next(), dx4 = next(), dx5 = next(), dy5 = next(), dx6 = next();
                    const double dy3 = dy1 + dy2;
                    const double dy4 = -(dy3 + dy5);
                    Cubic(dx1, dy1, dx2, dy2, dx3, dy3);
                    Cubic(dx4, dy4, dx5, dy5, dx6, 0.0);
                    break;
                }
                case 37U: { // flex1: dx1 dy1 dx2 dy2 dx3 dy3 dx4 dy4 dx5 dy5 d6
                    const double dx1 = next(), dy1 = next(), dx2 = next(), dy2 = next(),
                                 dx3 = next(), dy3 = next(), dx4 = next(), dy4 = next(),
                                 dx5 = next(), dy5 = next();
                    const double d6 = next();
                    const double sumDx = dx1 + dx2 + dx3 + dx4 + dx5;
                    const double sumDy = dy1 + dy2 + dy3 + dy4 + dy5;
                    const double endDx = std::abs(sumDx) > std::abs(sumDy) ? d6 : -sumDx;
                    const double endDy = std::abs(sumDx) > std::abs(sumDy) ? -sumDy : d6;
                    Cubic(dx1, dy1, dx2, dy2, dx3, dy3);
                    Cubic(dx4, dy4, dx5, dy5, endDx, endDy);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported Type 2 escape operator.");
                }
                continue;
            }

            // Numeric operands.
            if (byte == 28U) {
                if (offset + 2U > program.size()) throw std::runtime_error("Malformed CFF short integer.");
                const std::int16_t value = static_cast<std::int16_t>(
                    (std::to_integer<std::uint8_t>(program[offset]) << 8U) |
                    std::to_integer<std::uint8_t>(program[offset + 1U]));
                offset += 2U;
                Push(value);
                continue;
            }
            if (byte == 255U) {
                if (offset + 4U > program.size()) throw std::runtime_error("Malformed CFF fixed number.");
                std::int32_t raw{};
                for (int i = 0; i < 4; ++i) raw = (raw << 8) | std::to_integer<std::uint8_t>(program[offset++]);
                Push(static_cast<double>(raw) / 65536.0);
                continue;
            }
            if (byte >= 32U && byte <= 246U) { Push(static_cast<double>(static_cast<int>(byte) - 139)); continue; }
            if (byte >= 247U && byte <= 250U) {
                if (offset >= program.size()) throw std::runtime_error("Malformed CFF number.");
                Push(static_cast<double>((byte - 247U) * 256 + std::to_integer<std::uint8_t>(program[offset++]) + 108));
                continue;
            }
            if (byte >= 251U && byte <= 254U) {
                if (offset >= program.size()) throw std::runtime_error("Malformed CFF number.");
                Push(-static_cast<double>((static_cast<int>(byte) - 251) * 256 + std::to_integer<std::uint8_t>(program[offset++]) + 108));
                continue;
            }
            throw std::runtime_error("Unsupported Type 2 charstring number encoding.");
        }
    }

    void CallSubr(const int index, const bool local) {
        if (++callDepth > kMaxCallDepth) throw std::runtime_error("CFF subr recursion limit exceeded.");
        const auto& subrs = local ? font->localSubrs : font->globalSubrs;
        const auto bias = SubrBias(subrs.objects.size());
        const auto resolved = static_cast<std::int64_t>(index) + static_cast<std::int64_t>(bias);
        if (resolved < 0 || static_cast<std::size_t>(resolved) >= subrs.objects.size()) {
            --callDepth;
            return;
        }
        Run(subrs.objects[static_cast<std::size_t>(resolved)]);
        --callDepth;
    }
};

PdfCffGlyphOutline finalizeOutline(Type2Interpreter& interpreter) {
    PdfCffGlyphOutline outline;
    outline.width = interpreter.width;
    outline.segments = std::move(interpreter.segments);
    outline.empty = outline.segments.empty();
    if (outline.empty) return outline;
    outline.xMin = std::numeric_limits<double>::max();
    outline.yMin = std::numeric_limits<double>::max();
    outline.xMax = std::numeric_limits<double>::lowest();
    outline.yMax = std::numeric_limits<double>::lowest();
    for (const auto& segment : outline.segments) {
        const auto extendX = [&](const double value) {
            outline.xMin = std::min(outline.xMin, value);
            outline.xMax = std::max(outline.xMax, value);
        };
        const auto extendY = [&](const double value) {
            outline.yMin = std::min(outline.yMin, value);
            outline.yMax = std::max(outline.yMax, value);
        };
        extendX(segment.x1);
        extendY(segment.y1);
        if (segment.type == PdfCffOutlineSegment::Type::Cubic) {
            extendX(segment.x2);
            extendY(segment.y2);
        }
        extendX(segment.x3);
        extendY(segment.y3);
    }
    return outline;
}

} // namespace

PdfCffIndex PdfCffParser::ParseIndex(const std::span<const std::byte> bytes, std::size_t& offset,
                                     const std::size_t maxObjects) {
    if (offset + 2U > bytes.size()) throw std::runtime_error("Malformed CFF INDEX.");
    const auto count = static_cast<std::size_t>((std::to_integer<std::uint8_t>(bytes[offset]) << 8U) |
        std::to_integer<std::uint8_t>(bytes[offset + 1U]));
    offset += 2U;
    if (count == 0U) return {};
    if (count > maxObjects || offset >= bytes.size()) throw std::runtime_error("CFF INDEX exceeds limits.");
    const auto offSize = std::to_integer<std::uint8_t>(bytes[offset++]);
    if (offSize == 0U || offSize > 4U) throw std::runtime_error("Invalid CFF INDEX offset size.");
    std::vector<std::uint32_t> offsets;
    offsets.reserve(count + 1U);
    for (std::size_t i = 0; i <= count; ++i) offsets.push_back(readOffset(bytes, offset + i * offSize, offSize));
    offset += (count + 1U) * offSize;
    if (offsets.front() == 0U || offsets.back() < offsets.front() ||
        static_cast<std::size_t>(offsets.back() - 1U) > bytes.size() - offset) throw std::runtime_error("CFF INDEX range is invalid.");
    PdfCffIndex result;
    result.objects.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto begin = offset + offsets[i] - 1U;
        const auto end = offset + offsets[i + 1U] - 1U;
        result.objects.emplace_back(bytes.data() + begin, end - begin);
    }
    offset += offsets.back() - 1U;
    return result;
}

PdfCffFont PdfCffParser::ParseFont(const std::span<const std::byte> bytes) {
    if (bytes.size() < 4U) throw std::runtime_error("Malformed CFF header.");
    const auto headerSize = std::to_integer<std::uint8_t>(bytes[2]);
    if (headerSize < 4U || headerSize > bytes.size()) throw std::runtime_error("Invalid CFF header size.");
    const auto major = std::to_integer<std::uint8_t>(bytes[0]);
    const bool isCID = major >= 2U;

    PdfCffFont result;
    result.data.assign(bytes.begin(), bytes.end());
    result.isCID = isCID;

    std::size_t offset = headerSize;
    const auto names = ParseIndex(bytes, offset);
    const auto tops = ParseIndex(bytes, offset);
    const auto strings = ParseIndex(bytes, offset);
    (void)strings;
    result.globalSubrs = ParseIndex(bytes, offset);

    if (names.objects.empty() || tops.objects.empty()) throw std::runtime_error("CFF font indexes are empty.");
    result.name.assign(reinterpret_cast<const char*>(names.objects.front().data()), names.objects.front().size());
    result.top.fontName = result.name;
    for (const auto& entry : ParseDict(tops.objects.front())) {
        if (entry.operatorCode == 15U && !entry.operands.empty()) result.top.charsetOffset = static_cast<std::uint32_t>(std::max(0.0, entry.operands.front()));
        else if (entry.operatorCode == 16U && !entry.operands.empty()) result.top.encodingOffset = static_cast<std::uint32_t>(std::max(0.0, entry.operands.front()));
        else if (entry.operatorCode == 17U && !entry.operands.empty()) result.top.charStringsOffset = static_cast<std::uint32_t>(std::max(0.0, entry.operands.front()));
        else if (entry.operatorCode == 18U && entry.operands.size() >= 2U) {
            result.top.privateSize = static_cast<std::uint32_t>(std::max(0.0, entry.operands[0]));
            result.top.privateOffset = static_cast<std::uint32_t>(std::max(0.0, entry.operands[1]));
        }
    }
    if (result.top.charStringsOffset == 0U || result.top.charStringsOffset >= bytes.size()) throw std::runtime_error("CFF CharStrings offset is invalid.");

    std::size_t charStringsOffset = result.top.charStringsOffset;
    result.charStrings = ParseIndex(bytes, charStringsOffset);
    result.glyphCount = static_cast<std::uint32_t>(result.charStrings.objects.size());
    if (result.glyphCount == 0U) throw std::runtime_error("CFF CharStrings INDEX is empty.");

    // Private DICT provides default/nominal widths and the local subrs index.
    if (result.top.privateSize > 0U && result.top.privateOffset < bytes.size()) {
        const std::size_t privateEnd = std::min(
            static_cast<std::size_t>(result.top.privateOffset) + result.top.privateSize, bytes.size());
        for (const auto& entry : ParseDict(bytes.subspan(result.top.privateOffset, privateEnd - result.top.privateOffset))) {
            if (entry.operatorCode == 20U && !entry.operands.empty()) result.privateDict.defaultWidthX = entry.operands.front();
            else if (entry.operatorCode == 21U && !entry.operands.empty()) result.privateDict.nominalWidthX = entry.operands.front();
            else if (entry.operatorCode == 19U && entry.operands.size() >= 2U) {
                result.privateDict.subrsSize = static_cast<std::uint32_t>(std::max(0.0, entry.operands[0]));
                result.privateDict.subrsOffset = static_cast<std::uint32_t>(std::max(0.0, entry.operands[1]));
            }
        }
        if (result.privateDict.subrsSize > 0U && result.privateDict.subrsOffset < bytes.size()) {
            const std::size_t subrsEnd = std::min(
                static_cast<std::size_t>(result.privateDict.subrsOffset) + result.privateDict.subrsSize, bytes.size());
            std::size_t subrsOffset = result.privateDict.subrsOffset;
            result.localSubrs = ParseIndex(bytes.subspan(result.privateDict.subrsOffset, subrsEnd - result.privateDict.subrsOffset), subrsOffset);
            result.hasLocalSubrs = !result.localSubrs.objects.empty();
        }
    }

    // Charset: maps glyph index -> SID (simple) or CID (CID fonts). The
    // .notdef glyph at index 0 is not represented in the charset table.
    result.charset.reserve(result.glyphCount);
    result.charset.push_back(0U);
    if (result.top.charsetOffset != 0U && result.top.charsetOffset < bytes.size()) {
        const std::span<const std::byte> charsetBytes = bytes.subspan(result.top.charsetOffset);
        try {
            std::size_t charsetOffset{};
            if (charsetBytes.empty()) throw std::runtime_error("Empty CFF charset.");
            const auto format = std::to_integer<std::uint8_t>(charsetBytes[charsetOffset++]);
            if (format == 0U) {
                for (std::uint32_t i = 1U; i < result.glyphCount; ++i) {
                    result.charset.push_back(readCard8Or16(charsetBytes, charsetOffset, result.isCID));
                }
            } else if (format == 1U || format == 2U) {
                std::uint32_t sid = 0U;
                while (result.charset.size() < result.glyphCount && charsetOffset < charsetBytes.size()) {
                    sid = readCard8Or16(charsetBytes, charsetOffset, result.isCID);
                    const auto count = std::to_integer<std::uint8_t>(charsetBytes[charsetOffset++]) + 1U;
                    for (std::uint32_t i = 0U; i < count && result.charset.size() < result.glyphCount; ++i) {
                        result.charset.push_back(sid + i);
                    }
                }
            }
        } catch (const std::exception&) {
            // Keep a default identity charset so rendering can still proceed.
        }
    }
    while (result.charset.size() < result.glyphCount) {
        result.charset.push_back(static_cast<std::uint32_t>(result.charset.size()));
    }

    return result;
}

std::vector<PdfCffDictEntry> PdfCffParser::ParseDict(const std::span<const std::byte> bytes) {
    std::vector<PdfCffDictEntry> result;
    std::vector<double> operands;
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto code = std::to_integer<std::uint8_t>(bytes[offset++]);
        if (code <= 21U) {
            std::uint16_t op = code;
            if (code == 12U) { if (offset >= bytes.size()) throw std::runtime_error("Malformed CFF DICT escape."); op = static_cast<std::uint16_t>(0x0C00U | std::to_integer<std::uint8_t>(bytes[offset++])); }
            result.push_back({op, std::move(operands)}); operands.clear(); continue;
        }
        if (code == 28U) { if (offset + 2U > bytes.size()) throw std::runtime_error("Malformed CFF integer."); const auto value = static_cast<std::int16_t>((std::to_integer<std::uint8_t>(bytes[offset]) << 8U) | std::to_integer<std::uint8_t>(bytes[offset + 1U])); offset += 2U; operands.push_back(value); }
        else if (code == 29U) { if (offset + 4U > bytes.size()) throw std::runtime_error("Malformed CFF integer."); std::int32_t value{}; for (int i=0;i<4;++i) value=(value<<8)|std::to_integer<std::uint8_t>(bytes[offset++]); operands.push_back(static_cast<double>(value)); }
        else if (code >= 32U && code <= 246U) operands.push_back(static_cast<double>(static_cast<int>(code) - 139));
        else if (code >= 247U && code <= 250U) { if (offset >= bytes.size()) throw std::runtime_error("Malformed CFF number."); operands.push_back(static_cast<double>((code - 247U) * 256 + std::to_integer<std::uint8_t>(bytes[offset++]) + 108)); }
        else if (code >= 251U && code <= 254U) { if (offset >= bytes.size()) throw std::runtime_error("Malformed CFF number."); operands.push_back(-static_cast<double>((static_cast<int>(code) - 251) * 256 + std::to_integer<std::uint8_t>(bytes[offset++]) + 108)); }
        else throw std::runtime_error("Unsupported CFF DICT number encoding.");
    }
    if (!operands.empty()) throw std::runtime_error("CFF DICT ends with operands.");
    return result;
}

PdfCffGlyphOutline PdfCffParser::GetGlyphOutline(const PdfCffFont& font, const std::uint32_t glyphId) {
    if (glyphId >= font.charStrings.objects.size()) return {};
    Type2Interpreter interpreter;
    interpreter.font = &font;
    interpreter.Run(font.charStrings.objects[glyphId]);
    return finalizeOutline(interpreter);
}

double PdfCffParser::GetAdvanceWidth(const PdfCffFont& font, const std::uint32_t glyphId) {
    if (glyphId >= font.charStrings.objects.size()) return font.privateDict.defaultWidthX;
    Type2Interpreter interpreter;
    interpreter.font = &font;
    interpreter.Run(font.charStrings.objects[glyphId]);
    if (interpreter.widthParsed && interpreter.width > 0.0) return interpreter.width;
    return font.privateDict.defaultWidthX;
}

namespace {
// CFF Standard Strings table (SIDs 0..390). Missing rare entries fall back to
// "gidN".
constexpr const char* kCffStandardStrings[] = {
    ".notdef", "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
    "quoteright", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period",
    "slash", "zero", "one", "two", "three", "four", "five", "six",
    "seven", "eight", "nine", "colon", "semicolon", "less", "equal", "greater",
    "question", "at", "A", "B", "C", "D", "E", "F",
    "G", "H", "I", "J", "K", "L", "M", "N",
    "O", "P", "Q", "R", "S", "T", "U", "V",
    "W", "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum",
    "underscore", "quoteleft", "a", "b", "c", "d", "e", "f",
    "g", "h", "i", "j", "k", "l", "m", "n",
    "o", "p", "q", "r", "s", "t", "u", "v",
    "w", "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde",
    "exclamdown", "cent", "sterling", "fraction", "yen", "florin", "section", "currency",
    "quotesingle", "quotedblleft", "guillemotleft", "guilsinglleft", "guilsinglright", "fi", "fl", "endash",
    "dagger", "daggerdbl", "periodcentered", "paragraph", "bullet", "quotesinglbase", "quotedblbase", "quotedblright",
    "guillemotright", "ellipsis", "perthousand", "questiondown", "grave", "acute", "circumflex", "tilde",
    "macron", "breve", "dotaccent", "dieresis", "ring", "cedilla", "hungarumlaut", "ogonek",
    "caron", "emdash", "AE", "ordfeminine", "Lslash", "Oslash", "OE", "ordmasculine",
    "ae", "dotlessi", "lslash", "oslash", "oe", "germandbls", "onesuperior", "logicalnot",
    "mu", "trademark", "Eth", "onehalf", "plusminus", "Thorn", "onequarter", "divide",
    "brokenbar", "degree", "thorn", "threequarters", "twosuperior", "registered", "minus", "eth",
    "multiply", "threesuperior", "copyright", "Aacute", "Acircumflex", "Adieresis", "Agrave", "Aring",
    "Atilde", "Ccedilla", "Eacute", "Ecircumflex", "Edieresis", "Egrave", "Iacute", "Icircumflex",
    "Idieresis", "Igrave", "Ntilde", "Oacute", "Ocircumflex", "Odieresis", "Ograve", "Otilde",
    "Scaron", "Uacute", "Ucircumflex", "Udieresis", "Ugrave", "Yacute", "Ydieresis", "Zcaron",
    "aacute", "acircumflex", "adieresis", "agrave", "aring", "atilde", "ccedilla", "eacute",
    "ecircumflex", "edieresis", "egrave", "iacute", "icircumflex", "idieresis", "igrave", "ntilde",
    "oacute", "ocircumflex", "odieresis", "ograve", "otilde", "scaron", "uacute", "ucircumflex",
    "udieresis", "ugrave", "yacute", "ydieresis", "zcaron", "exclamsmall", "Hungarumlautsmall",
    "dollaroldstyle", "dollarsuperior", "ampersandsmall", "Acutesmall", "parenleftsuperior", "parenrightsuperior",
    "twodotenleader", "onedotenleader", "zerooldstyle", "oneoldstyle", "twooldstyle", "threeoldstyle",
    "fouroldstyle", "fiveoldstyle", "sixoldstyle", "sevenoldstyle", "eightoldstyle", "nineoldstyle",
    "commasuperior", "threequartersemdash", "periodsuperior", "questionsmall", "asuperior", "bsuperior",
    "centsuperior", "dsuperior", "esuperior", "isuperior", "lsuperior", "msuperior", "nsuperior",
    "osuperior", "rsuperior", "ssuperior", "tsuperior", "ff", "ffi", "ffl", "parenleftinferior",
    "parenrightinferior", "Circumflexsmall", "hyphensuperior", "Gravesmall", "Asmall", "Bsmall", "Csmall",
    "Dsmall", "Esmall", "Fsmall", "Gsmall", "Hsmall", "Ismall", "Jsmall", "Ksmall",
    "Lsmall", "Msmall", "Nsmall", "Osmall", "Psmall", "Qsmall", "Rsmall", "Ssmall",
    "Tsmall", "Usmall", "Vsmall", "Wsmall", "Xsmall", "Ysmall", "Zsmall", "colonmonetary",
    "onefitted", "rupiah", "Tildesmall", "exclamdownsmall", "centoldstyle", "Lslashsmall", "Scaronsmall",
    "Zcaronsmall", "Dieresissmall", "Brevesmall", "Caronsmall", "Dotaccentsmall", "Macronsmall", "figuredash",
    "hypheninferior", "Ogoneksmall", "Ringsmall", "Cedillasmall", "questiondownsmall", "oneeighth", "threeeighths",
    "fiveeighths", "seveneighths", "onethird", "twothirds", "zerosuperior", "foursuperior", "fivesuperior",
    "sixsuperior", "sevensuperior", "eightsuperior", "ninesuperior", "zeroinferior", "oneinferior", "twoinferior",
    "threeinferior", "fourinferior", "fiveinferior", "sixinferior", "seveninferior", "eightinferior",
    "nineinferior", "centinferior", "dollarinferior", "periodinferior", "commainferior", "Agravesmall",
    "Aacutesmall", "Acircumflexsmall", "Atildesmall", "Adieresissmall", "Aringsmall", "AEsmall",
    "Ccedillasmall", "Egravesmall", "Eacutesmall", "Ecircumflexsmall", "Edieresissmall", "Igravesmall",
    "Iacutesmall", "Icircumflexsmall", "Idieresissmall", "Ethsmall", "Ntildesmall", "Ogravesmall",
    "Oacutesmall", "Ocircumflexsmall", "Otildesmall", "Odieresissmall", "OEsmall", "Oslashsmall",
    "Ugravesmall", "Uacutesmall", "Ucircumflexsmall", "Udieresissmall", "Yacutesmall", "Thornsmall",
    "Ydieresissmall", "001.000", "001.001", "001.002", "001.003", "Black", "Bold", "Book",
    "Light", "Medium", "Regular", "Roman", "Semibold"
};
constexpr std::size_t kCffStandardStringCount = sizeof(kCffStandardStrings) / sizeof(kCffStandardStrings[0]);
} // namespace

std::string PdfCffParser::GetGlyphName(const PdfCffFont& font, const std::uint32_t glyphId) {
    if (font.isCID || glyphId >= font.charset.size()) return "gid" + std::to_string(glyphId);
    const std::uint32_t sid = font.charset[glyphId];
    if (sid < static_cast<std::uint32_t>(kCffStandardStringCount)) {
        return kCffStandardStrings[sid];
    }
    return "gid" + std::to_string(glyphId);
}

} // namespace CPPPdf

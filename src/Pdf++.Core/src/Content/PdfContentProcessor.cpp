#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace CPPPdf {
namespace {

struct NameOperand { std::string value; };
struct StringOperand { std::string value; };
struct ArrayOperand { std::vector<std::variant<double, NameOperand, StringOperand>> values; };
struct DictOperand { std::string text; };
using Operand = std::variant<double, NameOperand, StringOperand, ArrayOperand, DictOperand>;

bool IsDelimiter(char c) {
    switch (c) {
    case '(': case ')': case '<': case '>': case '[': case ']':
    case '{': case '}': case '/': case '%':
        return true;
    default:
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    }
}

void SkipWhitespaceAndComments(std::string_view source, std::size_t& offset) {
    while (offset < source.size()) {
        if (std::isspace(static_cast<unsigned char>(source[offset]))) {
            ++offset;
            continue;
        }
        if (source[offset] == '%') {
            while (offset < source.size() && source[offset] != '\r' && source[offset] != '\n') ++offset;
            continue;
        }
        break;
    }
}

int OctalValue(char c) { return c >= '0' && c <= '7' ? c - '0' : -1; }

std::string ParseLiteralString(std::string_view source, std::size_t& offset) {
    if (offset >= source.size() || source[offset] != '(') return {};
    ++offset;
    int depth = 1;
    std::string output;
    while (offset < source.size() && depth > 0) {
        char c = source[offset++];
        if (c == '\\') {
            if (offset >= source.size()) break;
            c = source[offset++];
            switch (c) {
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case '\r':
                if (offset < source.size() && source[offset] == '\n') ++offset;
                break;
            case '\n': break;
            default: {
                const int first = OctalValue(c);
                if (first >= 0) {
                    int value = first;
                    int digits = 1;
                    while (digits < 3 && offset < source.size()) {
                        const int next = OctalValue(source[offset]);
                        if (next < 0) break;
                        value = value * 8 + next;
                        ++digits;
                        ++offset;
                    }
                    output.push_back(static_cast<char>(value & 0xFF));
                } else {
                    output.push_back(c);
                }
                break;
            }
            }
            continue;
        }
        if (c == '(') {
            ++depth;
            output.push_back(c);
        } else if (c == ')') {
            --depth;
            if (depth > 0) output.push_back(c);
        } else {
            output.push_back(c);
        }
    }
    if (depth != 0) throw PdfException(PdfErrorCode::MalformedObject, "Unterminated PDF content string.");
    return output;
}

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string ParseHexString(std::string_view source, std::size_t& offset) {
    ++offset;
    std::string output;
    int high = -1;
    while (offset < source.size() && source[offset] != '>') {
        const char c = source[offset++];
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        const int nibble = HexNibble(c);
        if (nibble < 0) throw PdfException(PdfErrorCode::MalformedObject, "Invalid PDF hexadecimal string.");
        if (high < 0) high = nibble;
        else {
            output.push_back(static_cast<char>((high << 4) | nibble));
            high = -1;
        }
    }
    if (offset >= source.size()) throw PdfException(PdfErrorCode::MalformedObject, "Unterminated PDF hexadecimal string.");
    ++offset;
    if (high >= 0) output.push_back(static_cast<char>(high << 4));
    return output;
}

std::string_view ParseWordView(std::string_view source, std::size_t& offset) {
    const auto begin = offset;
    while (offset < source.size() && !IsDelimiter(source[offset])) ++offset;
    return source.substr(begin, offset - begin);
}

bool TryParseNumber(const std::string_view token, double& number) {
    if (token.empty()) return false;
    const auto* begin = token.data();
    const auto* end = token.data() + token.size();
    const auto parsed = std::from_chars(begin, end, number);
    if (parsed.ec == std::errc{} && parsed.ptr == end) return true;
    std::string fallback(token);
    char* parsedEnd{};
    number = std::strtod(fallback.c_str(), &parsedEnd);
    return parsedEnd == fallback.c_str() + fallback.size();
}

NameOperand ParseName(std::string_view source, std::size_t& offset) {
    ++offset;
    std::string value;
    while (offset < source.size() && !IsDelimiter(source[offset])) {
        if (source[offset] == '#' && offset + 2 < source.size()) {
            const int high = HexNibble(source[offset + 1]);
            const int low = HexNibble(source[offset + 2]);
            if (high >= 0 && low >= 0) {
                value.push_back(static_cast<char>((high << 4) | low));
                offset += 3;
                continue;
            }
        }
        value.push_back(source[offset++]);
    }
    return {std::move(value)};
}

ArrayOperand ParseArray(std::string_view source, std::size_t& offset) {
    ++offset;
    ArrayOperand array;
    while (offset < source.size()) {
        SkipWhitespaceAndComments(source, offset);
        if (offset >= source.size()) break;
        if (source[offset] == ']') { ++offset; return array; }
        if (source[offset] == '(') array.values.emplace_back(StringOperand{ParseLiteralString(source, offset)});
        else if (source[offset] == '<' && (offset + 1 >= source.size() || source[offset + 1] != '<'))
            array.values.emplace_back(StringOperand{ParseHexString(source, offset)});
        else if (source[offset] == '/') array.values.emplace_back(ParseName(source, offset));
        else {
            const auto token = ParseWordView(source, offset);
            double value{};
            if (TryParseNumber(token, value)) array.values.emplace_back(value);
        }
    }
    throw PdfException(PdfErrorCode::MalformedObject, "Unterminated PDF content array.");
}

double NumberAt(const std::vector<Operand>& operands, std::size_t index, double fallback = 0.0) {
    if (index >= operands.size()) return fallback;
    if (const auto* value = std::get_if<double>(&operands[index])) return *value;
    return fallback;
}

std::string NameAt(const std::vector<Operand>& operands, std::size_t index) {
    if (index >= operands.size()) return {};
    if (const auto* value = std::get_if<NameOperand>(&operands[index])) return value->value;
    return {};
}

std::string StringAt(const std::vector<Operand>& operands, std::size_t index) {
    if (index >= operands.size()) return {};
    if (const auto* value = std::get_if<StringOperand>(&operands[index])) return value->value;
    return {};
}

std::array<double, 6> MultiplyMatrices(
    const std::array<double, 6>& left,
    const std::array<double, 6>& right) noexcept {
    return {
        left[0] * right[0] + left[2] * right[1],
        left[1] * right[0] + left[3] * right[1],
        left[0] * right[2] + left[2] * right[3],
        left[1] * right[2] + left[3] * right[3],
        left[0] * right[4] + left[2] * right[5] + left[4],
        left[1] * right[4] + left[3] * right[5] + left[5]
    };
}


std::string ParseInlineToken(std::string_view source, std::size_t& offset) {
    SkipWhitespaceAndComments(source, offset);
    if (offset >= source.size()) return {};
    if (source[offset] == '/') return "/" + ParseName(source, offset).value;
    if (source[offset] == '[') {
        const auto begin = offset++;
        int depth = 1;
        while (offset < source.size() && depth > 0) {
            if (source[offset] == '[') ++depth;
            else if (source[offset] == ']') --depth;
            ++offset;
        }
        return std::string(source.substr(begin, offset - begin));
    }
    return std::string(ParseWordView(source, offset));
}

bool IsInlineImageEnd(std::string_view source, std::size_t position) {
    if (position + 2 > source.size() || source.substr(position, 2) != "EI") return false;
    const bool before = position == 0 || std::isspace(static_cast<unsigned char>(source[position - 1]));
    const bool after = position + 2 == source.size() ||
        std::isspace(static_cast<unsigned char>(source[position + 2])) || IsDelimiter(source[position + 2]);
    return before && after;
}

struct ParsedInlineImage {
    std::vector<PdfInlineImageProperty> dictionary;
    std::vector<std::byte> bytes;
};

ParsedInlineImage ParseInlineImage(std::string_view source, std::size_t& offset) {
    ParsedInlineImage result;
    while (offset < source.size()) {
        SkipWhitespaceAndComments(source, offset);
        const auto key = ParseInlineToken(source, offset);
        if (key.empty()) throw PdfException(PdfErrorCode::MalformedObject, "Malformed inline image dictionary.");
        if (key == "ID") break;
        const auto value = ParseInlineToken(source, offset);
        if (value.empty()) throw PdfException(PdfErrorCode::MalformedObject, "Missing inline image dictionary value.");
        std::string normalizedKey = key;
        if (!normalizedKey.empty() && normalizedKey.front() == '/') normalizedKey.erase(normalizedKey.begin());
        result.dictionary.push_back({std::move(normalizedKey), value});
    }
    if (offset < source.size() && source[offset] == '\r') {
        ++offset;
        if (offset < source.size() && source[offset] == '\n') ++offset;
    } else if (offset < source.size() && std::isspace(static_cast<unsigned char>(source[offset]))) {
        ++offset;
    }
    const auto dataStart = offset;
    std::size_t end = std::string_view::npos;
    for (std::size_t candidate = dataStart; candidate + 2 <= source.size(); ++candidate) {
        if (IsInlineImageEnd(source, candidate)) { end = candidate; break; }
    }
    if (end == std::string_view::npos) throw PdfException(PdfErrorCode::MalformedObject, "Inline image is missing EI terminator.");
    std::size_t dataEnd = end;
    while (dataEnd > dataStart && std::isspace(static_cast<unsigned char>(source[dataEnd - 1]))) --dataEnd;
    result.bytes.reserve(dataEnd - dataStart);
    for (std::size_t i = dataStart; i < dataEnd; ++i) result.bytes.push_back(static_cast<std::byte>(source[i]));
    offset = end + 2;
    return result;
}

void Emit(const PdfContentProcessor::Handler& handler,
          PdfContentEventType type,
          std::string operation,
          const PdfTextStateSnapshot& state,
          std::string text = {},
          std::vector<double> numbers = {}) {
    if (handler) {
        PdfContentEvent event;
        event.type = type;
        event.text = std::move(text);
        event.operation = std::move(operation);
        event.numbers = std::move(numbers);
        event.textState = state;
        handler(event);
    }
}

bool dictionaryBooleanLike(const PdfDictionary& dictionary, const char* key) {
    const auto* value = dictionary.Find(PdfName(key));
    if (value == nullptr) return false;
    if (const auto boolean = value->AsBoolean()) return *boolean;
    return false;
}

std::optional<PdfTransparencyGroupProperties> ParseTransparencyGroup(
    std::string_view propertyList) {
    try {
        const auto object = Internal::PdfObjectParser::Parse(propertyList, 64U);
        const auto* dictionary = object.AsDictionary();
        if (dictionary == nullptr) return std::nullopt;
        const auto* groupObject = dictionary->Find(PdfName("Group"));
        if (groupObject == nullptr) return std::nullopt;
        const auto* group = groupObject->AsDictionary();
        if (group == nullptr) return std::nullopt;
        const auto subtype = group->GetAsName(PdfName("S"));
        if (!subtype || subtype->value() != "Transparency") return std::nullopt;
        PdfTransparencyGroupProperties properties;
        properties.isolated = dictionaryBooleanLike(*group, "I");
        properties.knockout = dictionaryBooleanLike(*group, "K");
        if (const auto blend = group->GetAsName(PdfName("BM"))) properties.blendMode = blend->value();
        if (const auto alpha = group->Find(PdfName("CA"))) {
            if (const auto real = alpha->AsReal()) properties.alpha = std::clamp(*real, 0.0, 1.0);
        }
        return properties;
    } catch (const PdfException&) {
        return std::nullopt;
    }
}

} // namespace

void PdfContentProcessor::Process(
    const std::string_view content,
    const PdfTextStateSnapshot& initialState) const {
    if (!handler_) return;

    std::vector<Operand> operands;
    PdfTextStateSnapshot textState = initialState;
    // q/Q save and restore the graphics state only. Text state (font,
    // text matrix, spacing, leading, and rise) is independent in PDF and
    // must survive a graphics-state restore.
    struct GraphicsState final {
        std::array<double, 6> currentTransformationMatrix{};
        std::array<double, 3> strokeColor{};
        std::array<double, 3> fillColor{};
        double lineWidth{};
        int lineCap{};
        int lineJoin{};
        double miterLimit{10.0};
        double strokeAlpha{1.0};
        double fillAlpha{1.0};
    };
    std::vector<GraphicsState> graphicsStack;
    std::vector<bool> markedContentGroupStack;
    std::size_t offset{};

    while (offset < content.size()) {
        SkipWhitespaceAndComments(content, offset);
        if (offset >= content.size()) break;

        const char c = content[offset];
        if (c == '(') { operands.emplace_back(StringOperand{ParseLiteralString(content, offset)}); continue; }
        if (c == '<' && (offset + 1 >= content.size() || content[offset + 1] != '<')) {
            operands.emplace_back(StringOperand{ParseHexString(content, offset)}); continue;
        }
        if (c == '/') { operands.emplace_back(ParseName(content, offset)); continue; }
        if (c == '[') { operands.emplace_back(ParseArray(content, offset)); continue; }
        if (c == '<' && offset + 1 < content.size() && content[offset + 1] == '<') {
            // Dictionaries are legal operands (BDC property lists and inline
            // transparency-group declarations). Capture the raw text so the
            // document layer can resolve /Group metadata.
            const auto dictStart = offset;
            offset += 2;
            int depth = 1;
            while (offset < content.size() && depth > 0) {
                if (offset + 1 < content.size() && content[offset] == '<' && content[offset + 1] == '<') {
                    ++depth;
                    offset += 2;
                } else if (offset + 1 < content.size() && content[offset] == '>' && content[offset + 1] == '>') {
                    --depth;
                    offset += 2;
                } else {
                    ++offset;
                }
            }
            if (depth != 0) throw PdfException(PdfErrorCode::MalformedObject, "Unterminated content dictionary.");
            operands.emplace_back(DictOperand{std::string(content.substr(dictStart, offset - dictStart))});
            continue;
        }

        const std::string_view token = ParseWordView(content, offset);
        if (token.empty()) { ++offset; continue; }
        double number{};
        if (TryParseNumber(token, number)) { operands.emplace_back(number); continue; }

        if (token == "BI") {
            auto inlineImage = ParseInlineImage(content, offset);
            PdfContentEvent event;
            event.type = PdfContentEventType::RenderInlineImage;
            event.operation = "BI";
            event.textState = textState;
            event.inlineImageDictionary = std::move(inlineImage.dictionary);
            event.bytes = std::move(inlineImage.bytes);
            handler_(event);
        }
        else if (token == "BT") Emit(handler_, PdfContentEventType::BeginText, std::string(token), textState);
        else if (token == "ET") Emit(handler_, PdfContentEventType::EndText, std::string(token), textState);
        else if (token == "q") {
            graphicsStack.push_back({textState.currentTransformationMatrix,
                                     textState.strokeColor,
                                      textState.fillColor,
                                      textState.lineWidth,
                                      textState.lineCap,
                                      textState.lineJoin,
                                      textState.miterLimit,
                                      textState.strokeAlpha,
                                      textState.fillAlpha});
            Emit(handler_, PdfContentEventType::SaveState, std::string(token), textState);
        }
        else if (token == "Q") {
            if (!graphicsStack.empty()) {
                const auto state = graphicsStack.back();
                graphicsStack.pop_back();
                textState.currentTransformationMatrix = state.currentTransformationMatrix;
                textState.strokeColor = state.strokeColor;
                textState.fillColor = state.fillColor;
                textState.lineWidth = state.lineWidth;
                textState.lineCap = state.lineCap;
                textState.lineJoin = state.lineJoin;
                textState.miterLimit = state.miterLimit;
                textState.strokeAlpha = state.strokeAlpha;
                textState.fillAlpha = state.fillAlpha;
            }
            Emit(handler_, PdfContentEventType::RestoreState, std::string(token), textState);
        }
        else if (token == "Tf") {
            textState.fontResource = NameAt(operands, 0);
            textState.fontSize = NumberAt(operands, 1);
            Emit(handler_, PdfContentEventType::SetFont, std::string(token), textState, {}, {textState.fontSize});
        }
        else if (token == "Tc") { textState.characterSpacing = NumberAt(operands, 0); Emit(handler_, PdfContentEventType::SetCharacterSpacing, std::string(token), textState, {}, {textState.characterSpacing}); }
        else if (token == "Tw") { textState.wordSpacing = NumberAt(operands, 0); Emit(handler_, PdfContentEventType::SetWordSpacing, std::string(token), textState, {}, {textState.wordSpacing}); }
        else if (token == "Tz") { textState.horizontalScaling = NumberAt(operands, 0, 100.0); Emit(handler_, PdfContentEventType::SetHorizontalScaling, std::string(token), textState, {}, {textState.horizontalScaling}); }
        else if (token == "TL") { textState.leading = NumberAt(operands, 0); Emit(handler_, PdfContentEventType::SetLeading, std::string(token), textState, {}, {textState.leading}); }
        else if (token == "Tr") { textState.renderingMode = static_cast<int>(NumberAt(operands, 0)); Emit(handler_, PdfContentEventType::SetTextRenderingMode, std::string(token), textState, {}, {static_cast<double>(textState.renderingMode)}); }
        else if (token == "Ts") { textState.rise = NumberAt(operands, 0); Emit(handler_, PdfContentEventType::SetTextRise, std::string(token), textState, {}, {textState.rise}); }
        else if (token == "w") {
            textState.lineWidth = std::max(0.0, NumberAt(operands, 0, 1.0));
            Emit(handler_, PdfContentEventType::SetLineWidth, std::string(token), textState, {}, {textState.lineWidth});
        }
        else if (token == "J") {
            textState.lineCap = std::clamp(static_cast<int>(NumberAt(operands, 0)), 0, 2);
        }
        else if (token == "j") {
            textState.lineJoin = std::clamp(static_cast<int>(NumberAt(operands, 0)), 0, 2);
        }
        else if (token == "M") {
            textState.miterLimit = std::max(1.0, NumberAt(operands, 0, 10.0));
        }
        else if (token == "gs") {
            Emit(handler_, PdfContentEventType::UnknownOperator, std::string(token), textState,
                 NameAt(operands, 0));
        }
        else if (token == "RG" || token == "rg") {
            auto& color = token == "RG" ? textState.strokeColor : textState.fillColor;
            for (std::size_t i = 0; i < 3; ++i) color[i] = std::clamp(NumberAt(operands, i), 0.0, 1.0);
            Emit(handler_, token == "RG" ? PdfContentEventType::SetStrokeColor : PdfContentEventType::SetFillColor,
                 std::string(token), textState, {}, {color[0], color[1], color[2]});
        }
        else if (token == "G" || token == "g") {
            const double gray = std::clamp(NumberAt(operands, 0), 0.0, 1.0);
            auto& color = token == "G" ? textState.strokeColor : textState.fillColor;
            color = {gray, gray, gray};
            Emit(handler_, token == "G" ? PdfContentEventType::SetStrokeColor : PdfContentEventType::SetFillColor,
                 std::string(token), textState, {}, {gray, gray, gray});
        }
        else if (token == "Tm") {
            for (std::size_t i = 0; i < 6; ++i) textState.textMatrix[i] = NumberAt(operands, i, i == 0 || i == 3 ? 1.0 : 0.0);
            Emit(handler_, PdfContentEventType::SetTextMatrix, std::string(token), textState,
                 {}, std::vector<double>(textState.textMatrix.begin(), textState.textMatrix.end()));
        }
        else if (token == "Td" || token == "TD") {
            const double tx = NumberAt(operands, 0);
            const double ty = NumberAt(operands, 1);
            if (token == "TD") textState.leading = -ty;
            textState.textMatrix[4] += tx;
            textState.textMatrix[5] += ty;
            Emit(handler_, PdfContentEventType::MoveText, std::string(token), textState, {}, {tx, ty});
        }
        else if (token == "T*") {
            textState.textMatrix[5] -= textState.leading;
            Emit(handler_, PdfContentEventType::MoveText, std::string(token), textState, {}, {0.0, -textState.leading});
        }
        else if (token == "Tj") Emit(handler_, PdfContentEventType::RenderText, std::string(token), textState, StringAt(operands, 0));
        else if (token == "TJ") {
            std::string combined;
            std::vector<double> adjustments;
            std::vector<std::string> eventSegments;
            std::vector<double> eventSegmentAdjustments;
            if (!operands.empty()) {
                if (const auto* array = std::get_if<ArrayOperand>(&operands.front())) {
                    for (const auto& item : array->values) {
                        if (const auto* stringValue = std::get_if<StringOperand>(&item)) {
                            combined += stringValue->value;
                            eventSegments.push_back(stringValue->value);
                            eventSegmentAdjustments.push_back(0.0);
                        } else if (const auto* adjustment = std::get_if<double>(&item)) {
                            adjustments.push_back(*adjustment);
                            if (!eventSegmentAdjustments.empty()) eventSegmentAdjustments.back() += *adjustment;
                        }
                    }
                }
            }
            PdfContentEvent event;
            event.type = PdfContentEventType::RenderText;
            event.operation = std::string(token);
            event.textState = textState;
            event.text = std::move(combined);
            event.numbers = std::move(adjustments);
            event.textSegments = std::move(eventSegments);
            event.textSegmentAdjustments = std::move(eventSegmentAdjustments);
            handler_(event);
        }
        else if (token == "'") {
            textState.textMatrix[5] -= textState.leading;
            Emit(handler_, PdfContentEventType::MoveText, "T*", textState, {}, {0.0, -textState.leading});
            Emit(handler_, PdfContentEventType::RenderText, std::string(token), textState, StringAt(operands, 0));
        }
        else if (token == "\"") {
            textState.wordSpacing = NumberAt(operands, 0);
            textState.characterSpacing = NumberAt(operands, 1);
            textState.textMatrix[5] -= textState.leading;
            Emit(handler_, PdfContentEventType::RenderText, std::string(token), textState, StringAt(operands, 2));
        }
        else if (token == "cm") {
            std::array<double, 6> matrix{};
            for (std::size_t i = 0; i < 6; ++i) {
                matrix[i] = NumberAt(operands, i, i == 0 || i == 3 ? 1.0 : 0.0);
            }
            textState.currentTransformationMatrix =
                MultiplyMatrices(matrix, textState.currentTransformationMatrix);
            Emit(handler_, PdfContentEventType::ConcatenateMatrix, std::string(token), textState, {},
                 std::vector<double>(
                     textState.currentTransformationMatrix.begin(),
                     textState.currentTransformationMatrix.end()));
        }
        else if (token == "Do") Emit(handler_, PdfContentEventType::InvokeXObject, std::string(token), textState, NameAt(operands, 0));
        else if (token == "sh") Emit(handler_, PdfContentEventType::PaintShading, std::string(token), textState, NameAt(operands, 0));
        else if (token == "BDC") {
            bool isGroup = false;
            PdfContentEvent event;
            event.type = PdfContentEventType::BeginMarkedContent;
            event.operation = std::string(token);
            event.textState = textState;
            event.text = NameAt(operands, 0);
            if (operands.size() > 1U) {
                if (const auto* name = std::get_if<NameOperand>(&operands[1])) {
                    event.markedContentProperty = name->value;
                } else if (const auto* dictionary = std::get_if<DictOperand>(&operands[1])) {
                    event.markedContentProperty = dictionary->text;
                    if (auto group = ParseTransparencyGroup(dictionary->text)) {
                        event.type = PdfContentEventType::BeginTransparencyGroup;
                        event.transparencyGroup = std::move(*group);
                        isGroup = true;
                    }
                }
            }
            markedContentGroupStack.push_back(isGroup);
            handler_(event);
        }
        else if (token == "EMC") {
            const bool closesGroup = !markedContentGroupStack.empty() && markedContentGroupStack.back();
            if (!markedContentGroupStack.empty()) markedContentGroupStack.pop_back();
            PdfContentEvent event;
            event.type = closesGroup ? PdfContentEventType::EndTransparencyGroup
                                     : PdfContentEventType::EndMarkedContent;
            event.operation = std::string(token);
            event.textState = textState;
            handler_(event);
        }
        else if (token == "m" || token == "l" || token == "c" || token == "v" || token == "y" ||
                 token == "h" || token == "re" || token == "S" || token == "s" || token == "f" ||
                 token == "F" || token == "f*" || token == "B" || token == "B*" || token == "b" ||
                 token == "b*" || token == "n" || token == "W" || token == "W*") {
            std::vector<double> values;
            for (const auto& operand : operands) if (const auto* value = std::get_if<double>(&operand)) values.push_back(*value);
            Emit(handler_, PdfContentEventType::RenderPath, std::string(token), textState, {}, std::move(values));
        }
        else Emit(handler_, PdfContentEventType::UnknownOperator, std::string(token), textState);

        operands.clear();
    }
}

} // namespace CPPPdf

#include "Internal/Parsing/PdfObjectParser.hpp"
#include <CPPPdf/PdfError.hpp>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <cmath>
#include <system_error>

namespace CPPPdf::Internal {
namespace {

// Returns the position after skipping whitespace and comments.
std::size_t SkipSpaceAndComments(std::string_view s, std::size_t pos) {
    while (pos < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[pos]);
        if (std::isspace(c) || c == 0) { ++pos; continue; }
        if (c == '%') {
            while (pos < s.size() && s[pos] != '\n' && s[pos] != '\r') ++pos;
            continue;
        }
        break;
    }
    return pos;
}

// Attempts to consume a leading indirect-object header ("N G obj") from the
// source. On success the returned view excludes the header; otherwise the
// original source is returned unchanged.
std::string_view StripIndirectHeader(std::string_view source) {
    std::size_t pos = SkipSpaceAndComments(source, 0U);
    const std::size_t firstBegin = pos;
    while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
    if (firstBegin == pos) return source;

    pos = SkipSpaceAndComments(source, pos);
    const std::size_t secondBegin = pos;
    while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
    if (secondBegin == pos) return source;

    const std::size_t afterSecond = SkipSpaceAndComments(source, pos);
    if (source.substr(afterSecond, 3) != "obj") return source;
    return source.substr(afterSecond + 3U);
}

class Parser {
public:
    Parser(std::string_view s, std::size_t maxDepth) : s_(s), maxDepth_(maxDepth) {}

    void Finish() {
        Skip();
        if (Starts("endobj") && (p_ + 6U == s_.size() || Delim(s_[p_ + 6U]))) {
            p_ += 6U;
            Skip();
        }
        if (!End()) {
            throw PdfException(PdfErrorCode::MalformedObject,
                "Unexpected trailing data after PDF object.");
        }
    }

    PdfObject ParseValue(std::size_t depth = 0) {
        if (depth > maxDepth_) throw PdfException(PdfErrorCode::MalformedObject, "PDF object recursion limit exceeded.");
        Skip(); if (End()) return {};
        if (MatchKeyword("null")) return {};
        if (MatchKeyword("true")) return PdfObject(true);
        if (MatchKeyword("false")) return PdfObject(false);
        if (Peek('/')) return PdfObject(ParseName());
        if (Peek('(')) return PdfObject(ParseLiteralString());
        if (Starts("<<")) {
            PdfDictionary dictionary = ParseDictionary(depth + 1);
            if (HasStreamKeyword()) {
                // Resolve the direct /Length before moving the dictionary into
                // the stream object.
                auto data = ParseStreamData(dictionary);
                return PdfObject(PdfStream(std::move(dictionary), std::move(data)));
            }
            return PdfObject(std::move(dictionary));
        }
        if (Peek('[')) return PdfObject(ParseArray(depth + 1));
        if (Peek('<')) return PdfObject(ParseHexString());
        if (Peek('+') || Peek('-') || Peek('.') || std::isdigit(static_cast<unsigned char>(Current()))) return ParseNumberOrReference();
        throw PdfException(PdfErrorCode::MalformedObject, "Unsupported token in typed PDF object parser.");
    }

private:
    void Skip() {
        while (!End()) {
            unsigned char c = static_cast<unsigned char>(s_[p_]);
            if (std::isspace(c) || c == 0) { ++p_; continue; }
            if (c == '%') { while (!End() && s_[p_] != '\n' && s_[p_] != '\r') ++p_; continue; }
            break;
        }
    }
    bool End() const { return p_ >= s_.size(); }
    char Current() const { return End() ? '\0' : s_[p_]; }
    bool Peek(char c) const { return Current() == c; }
    bool Starts(std::string_view t) const { return p_ + t.size() <= s_.size() && s_.substr(p_, t.size()) == t; }
    static bool Delim(char c) { return std::isspace(static_cast<unsigned char>(c)) || c == '/' || c == '<' || c == '>' || c == '[' || c == ']' || c == '(' || c == ')' || c == '%'; }
    bool MatchKeyword(std::string_view t) {
        Skip();
        if (!Starts(t)) return false;
        const std::size_t after = p_ + t.size();
        if (after < s_.size() && !Delim(s_[after])) return false;
        p_ = after;
        return true;
    }

    PdfName ParseName() {
        ++p_; std::string v;
        while (!End() && !Delim(Current())) {
            if (Current() == '#' && p_ + 2 < s_.size()) {
                auto hex = [](char c) {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    return -1;
                };
                const int a = hex(s_[p_ + 1]), b = hex(s_[p_ + 2]);
                if (a >= 0 && b >= 0) {
                    v.push_back(static_cast<char>((a << 4) | b));
                    p_ += 3;
                    continue;
                }
            }
            v.push_back(Current());
            ++p_;
        }
        return PdfName(std::move(v));
    }

    std::string ParseLiteralString() {
        ++p_; std::string out; int depth = 1; bool esc = false;
        while (!End() && depth > 0) {
            char c = s_[p_++];
            if (esc) {
                esc = false;
                switch (c) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '\n': break;
                case '\r': if (!End() && Current() == '\n') ++p_; break;
                default:
                    if (c >= '0' && c <= '7') {
                        int value = c - '0';
                        for (int digits = 1; digits < 3 && !End(); ++digits) {
                            const char next = Current();
                            if (next < '0' || next > '7') break;
                            value = value * 8 + (next - '0');
                            ++p_;
                        }
                        out.push_back(static_cast<char>(value & 0xff));
                    } else {
                        // PDF treats an unknown escape as the escaped byte,
                        // which also covers escaped parentheses and backslash.
                        out += c;
                    }
                }
                continue;
            }
            if (c == '\\') { esc = true; continue; }
            if (c == '(') { ++depth; out += c; }
            else if (c == ')') { if (--depth > 0) out += c; }
            else out += c;
        }
        if (depth) throw PdfException(PdfErrorCode::MalformedObject, "Unterminated PDF string.");
        return out;
    }

    std::string ParseHexString() {
        ++p_; std::string hex;
        while (!End() && Current() != '>') {
            if (!std::isspace(static_cast<unsigned char>(Current()))) hex += Current();
            ++p_;
        }
        if (End()) throw PdfException(PdfErrorCode::MalformedObject, "Unterminated hex string.");
        ++p_;
        if (hex.size() % 2) hex += '0';
        auto h = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        std::string out;
        for (std::size_t i = 0; i < hex.size(); i += 2) {
            const int a = h(hex[i]), b = h(hex[i + 1]);
            if (a < 0 || b < 0) throw PdfException(PdfErrorCode::MalformedObject, "Invalid hex string.");
            out.push_back(static_cast<char>((a << 4) | b));
        }
        return out;
    }

    PdfArray ParseArray(std::size_t depth) {
        ++p_; PdfArray a; a.reserve(8U);
        for (;;) {
            Skip();
            if (End()) throw PdfException(PdfErrorCode::MalformedObject, "Unterminated array.");
            if (Peek(']')) { ++p_; break; }
            a.push_back(ParseValue(depth));
        }
        return a;
    }

    PdfDictionary ParseDictionary(std::size_t depth) {
        p_ += 2; PdfDictionary d; d.reserve(8U);
        for (;;) {
            Skip();
            if (Starts(">>")) { p_ += 2; break; }
            if (!Peek('/')) throw PdfException(PdfErrorCode::MalformedObject, "Dictionary key is not a name.");
            auto key = ParseName();
            auto value = ParseValue(depth);
            d.Put(std::move(key), std::move(value));
        }
        return d;
    }

    PdfObject ParseNumberOrReference() {
        Skip(); const auto start = p_;
        while (!End() && !Delim(Current())) ++p_;
        const auto token = s_.substr(start, p_ - start);
        const bool real = token.find_first_of(".") != std::string_view::npos;
        if (real) {
            double value{};
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value,
                                                std::chars_format::general);
            if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() &&
                std::isfinite(value)) {
                return PdfObject(value);
            }
            // Some older standard libraries have incomplete floating-point
            // from_chars support. Keep a compatible fallback outside the hot path.
            std::string temporary(token);
            char* end{};
            value = std::strtod(temporary.c_str(), &end);
            if (end != temporary.c_str() + temporary.size()) {
                throw PdfException(PdfErrorCode::MalformedObject, "Invalid real number.");
            }
            if (!std::isfinite(value)) {
                throw PdfException(PdfErrorCode::MalformedObject,
                    "PDF real number is not finite.");
            }
            return PdfObject(value);
        }
        std::int64_t first{};
        auto r = std::from_chars(token.data(), token.data() + token.size(), first);
        if (r.ec != std::errc{} || r.ptr != token.data() + token.size()) {
            throw PdfException(PdfErrorCode::MalformedObject, "Invalid PDF integer.");
        }
        const auto save = p_;
        Skip();
        const auto secondStart = p_;
        while (!End() && !Delim(Current())) ++p_;
        const auto secondToken = s_.substr(secondStart, p_ - secondStart);
        std::int64_t second{};
        const auto r2 = std::from_chars(secondToken.data(), secondToken.data() + secondToken.size(), second);
        if (r2.ec == std::errc{} && r2.ptr == secondToken.data() + secondToken.size() &&
            first >= 0 && static_cast<std::uint64_t>(first) <= std::numeric_limits<std::uint32_t>::max() &&
            second >= 0 && static_cast<std::uint64_t>(second) <= std::numeric_limits<std::uint16_t>::max()) {
            Skip();
            if (Peek('R') && (p_ + 1U == s_.size() || Delim(s_[p_ + 1U]))) {
                ++p_;
                return PdfObject::IndirectReference(static_cast<std::uint32_t>(first),
                    static_cast<std::uint16_t>(second));
            }
        }
        p_ = save;
        return PdfObject(first);
    }

    // Returns true when the next token is the "stream" keyword that must
    // directly follow a dictionary. The stream keyword must be followed by an
    // EOL (or the end of the input) per the PDF specification.
    bool HasStreamKeyword() {
        const std::size_t probe = SkipSpaceAndComments(s_, p_);
        if (s_.substr(probe, 6) != "stream") return false;
        const std::size_t after = probe + 6U;
        if (after < s_.size()) {
            const unsigned char ch = static_cast<unsigned char>(s_[after]);
            if (ch != '\r' && ch != '\n' && ch != ' ' && ch != '\t') return false;
        }
        p_ = after;
        return true;
    }

    std::vector<std::byte> ParseStreamData(const PdfDictionary& dictionary) {
        // The EOL immediately following the stream keyword is not stream data.
        if (Peek('\r')) { ++p_; if (Peek('\n')) ++p_; }
        else if (Peek('\n')) ++p_;
        const std::size_t dataStart = p_;

        // Use a direct integer /Length when present; otherwise fall back to the
        // endstream marker. An indirect /Length cannot be resolved here.
        std::size_t dataEnd = std::string_view::npos;
        if (const auto* lengthObject = dictionary.Find(PdfName("Length"))) {
            if (const auto length = lengthObject->AsInteger(); length.has_value()) {
                if (*length < 0) {
                    throw PdfException(PdfErrorCode::MalformedObject,
                        "PDF stream length cannot be negative.");
                }
                const auto requested = static_cast<std::size_t>(*length);
                if (requested > s_.size() - dataStart) {
                    throw PdfException(PdfErrorCode::MalformedObject,
                        "PDF stream is shorter than its declared length.");
                }
                dataEnd = dataStart + requested;
            }
        }
        if (dataEnd == std::string_view::npos) {
            const std::size_t relative = s_.find("endstream", dataStart);
            if (relative == std::string_view::npos) {
                throw PdfException(PdfErrorCode::MalformedObject, "Unterminated PDF stream object.");
            }
            dataEnd = relative;
        }

        std::vector<std::byte> bytes(dataEnd - dataStart);
        if (!bytes.empty()) {
            std::memcpy(bytes.data(), s_.data() + dataStart, bytes.size());
        }
        p_ = dataEnd;
        if (Peek('\r')) { ++p_; if (Peek('\n')) ++p_; }
        else if (Peek('\n')) ++p_;
        if (!Starts("endstream")) {
            throw PdfException(PdfErrorCode::MalformedObject,
                "PDF stream is missing endstream marker.");
        }
        p_ += 9U;
        return bytes;
    }

    std::string_view s_;
    std::size_t p_{};
    std::size_t maxDepth_;
};
} // namespace

PdfObject PdfObjectParser::Parse(std::string_view source, std::size_t maxDepth) {
    const std::string_view body = StripIndirectHeader(source);
    Parser parser(body, maxDepth);
    PdfObject result = parser.ParseValue();
    parser.Finish();
    return result;
}
} // namespace CPPPdf::Internal

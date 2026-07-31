#include <CPPPdf/Fonts/PdfFont.hpp>
#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <regex>
#include <string>
#include <vector>

namespace CPPPdf {
namespace {

std::uint32_t HexToUInt(std::string_view value) {
    if (value.empty() || value.size() > 8) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid CMap source code width.");
    }
    std::uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw PdfException(PdfErrorCode::MalformedObject, "Invalid hexadecimal CMap code.");
    }
    return result;
}

void AppendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
        codePoint = 0xFFFDU;
    }
    if (codePoint <= 0x7FU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

std::string Utf16BeHexToUtf8(std::string_view hex) {
    if ((hex.size() % 4U) != 0U) {
        throw PdfException(PdfErrorCode::MalformedObject, "ToUnicode destination is not UTF-16BE aligned.");
    }
    std::string output;
    for (std::size_t offset = 0; offset < hex.size(); offset += 4U) {
        const auto unit = static_cast<std::uint16_t>(HexToUInt(hex.substr(offset, 4U)));
        if (unit >= 0xD800U && unit <= 0xDBFFU && offset + 8U <= hex.size()) {
            const auto low = static_cast<std::uint16_t>(HexToUInt(hex.substr(offset + 4U, 4U)));
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                const std::uint32_t codePoint = 0x10000U +
                    ((static_cast<std::uint32_t>(unit) - 0xD800U) << 10U) +
                    (static_cast<std::uint32_t>(low) - 0xDC00U);
                AppendUtf8(output, codePoint);
                offset += 4U;
                continue;
            }
        }
        AppendUtf8(output, unit);
    }
    return output;
}

std::string IncrementUtf16Hex(std::string hex, std::uint32_t amount) {
    if (hex.size() < 4U || (hex.size() % 4U) != 0U) return hex;
    std::uint32_t carry = amount;
    for (std::size_t pos = hex.size(); pos >= 4U && carry != 0U; pos -= 4U) {
        const std::size_t start = pos - 4U;
        const std::uint32_t unit = HexToUInt(std::string_view(hex).substr(start, 4U));
        const std::uint32_t sum = unit + (carry & 0xFFFFU);
        static constexpr char digits[] = "0123456789ABCDEF";
        const std::uint16_t written = static_cast<std::uint16_t>(sum & 0xFFFFU);
        for (int nibble = 3; nibble >= 0; --nibble) {
            hex[start + static_cast<std::size_t>(3 - nibble)] = digits[(written >> (nibble * 4)) & 0xFU];
        }
        carry = (carry >> 16U) + (sum >> 16U);
        if (start == 0U) break;
    }
    return hex;
}

std::vector<std::string> ExtractBlocks(const std::string& source,
                                       std::string_view beginMarker,
                                       std::string_view endMarker) {
    std::vector<std::string> blocks;
    std::size_t cursor{};
    while (true) {
        const auto begin = source.find(beginMarker, cursor);
        if (begin == std::string::npos) break;
        const auto body = begin + beginMarker.size();
        const auto end = source.find(endMarker, body);
        if (end == std::string::npos) break;
        blocks.emplace_back(source.substr(body, end - body));
        cursor = end + endMarker.size();
    }
    return blocks;
}

} // namespace

std::uint64_t PdfToUnicodeCMap::MappingKey(std::uint32_t code, std::uint8_t byteCount) noexcept {
    return (static_cast<std::uint64_t>(byteCount) << 32U) | code;
}

bool PdfToUnicodeCMap::IsInCodeSpace(std::uint32_t code, std::uint8_t byteCount) const noexcept {
    if (codeSpaceRanges_.empty()) return byteCount == minimumCodeBytes_;
    return std::any_of(codeSpaceRanges_.begin(), codeSpaceRanges_.end(),
        [code, byteCount](const CodeSpaceRange& range) {
            return range.byteCount == byteCount && code >= range.first && code <= range.last;
        });
}

PdfToUnicodeCMap PdfToUnicodeCMap::Parse(std::string_view source) {
    PdfToUnicodeCMap map;
    const std::string text(source);
    const std::regex pairPattern(R"(<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>)");

    for (const auto& block : ExtractBlocks(text, "begincodespacerange", "endcodespacerange")) {
        for (std::sregex_iterator it(block.begin(), block.end(), pairPattern), end; it != end; ++it) {
            const auto firstHex = (*it)[1].str();
            const auto lastHex = (*it)[2].str();
            if (firstHex.size() != lastHex.size() || (firstHex.size() % 2U) != 0U || firstHex.size() > 8U) continue;
            const auto bytes = static_cast<std::uint8_t>(firstHex.size() / 2U);
            map.codeSpaceRanges_.push_back({HexToUInt(firstHex), HexToUInt(lastHex), bytes});
            map.minimumCodeBytes_ = std::min(map.minimumCodeBytes_, bytes);
            map.maximumCodeBytes_ = std::max(map.maximumCodeBytes_, bytes);
        }
    }
    if (!map.codeSpaceRanges_.empty()) {
        map.minimumCodeBytes_ = std::numeric_limits<std::uint8_t>::max();
        map.maximumCodeBytes_ = 1;
        for (const auto& range : map.codeSpaceRanges_) {
            map.minimumCodeBytes_ = std::min(map.minimumCodeBytes_, range.byteCount);
            map.maximumCodeBytes_ = std::max(map.maximumCodeBytes_, range.byteCount);
        }
    }

    for (const auto& block : ExtractBlocks(text, "beginbfchar", "endbfchar")) {
        for (std::sregex_iterator it(block.begin(), block.end(), pairPattern), end; it != end; ++it) {
            const auto src = (*it)[1].str();
            const auto dst = (*it)[2].str();
            if ((src.size() % 2U) != 0U || src.empty() || src.size() > 8U) continue;
            const auto bytes = static_cast<std::uint8_t>(src.size() / 2U);
            map.mappings_[MappingKey(HexToUInt(src), bytes)] = Utf16BeHexToUtf8(dst);
        }
    }

    const std::regex sequentialRange(
        R"(<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>)");
    const std::regex arrayRange(
        R"(<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*\[([^\]]*)\])");
    const std::regex hexToken(R"(<([0-9A-Fa-f]+)>)");

    for (const auto& block : ExtractBlocks(text, "beginbfrange", "endbfrange")) {
        for (std::sregex_iterator it(block.begin(), block.end(), arrayRange), end; it != end; ++it) {
            const auto firstHex = (*it)[1].str();
            const auto lastHex = (*it)[2].str();
            const auto first = HexToUInt(firstHex);
            const auto last = HexToUInt(lastHex);
            const auto bytes = static_cast<std::uint8_t>(firstHex.size() / 2U);
            std::uint32_t code = first;
            const std::string values = (*it)[3].str();
            for (std::sregex_iterator value(values.begin(), values.end(), hexToken), valueEnd;
                 value != valueEnd && code <= last; ++value, ++code) {
                map.mappings_[MappingKey(code, bytes)] = Utf16BeHexToUtf8((*value)[1].str());
            }
        }

        std::string sequentialBlock = block;
        std::vector<std::pair<std::size_t, std::size_t>> arraySpans;
        for (std::sregex_iterator it(block.begin(), block.end(), arrayRange), end; it != end; ++it) {
            arraySpans.emplace_back(static_cast<std::size_t>(it->position()), static_cast<std::size_t>(it->length()));
        }
        for (const auto& [position, length] : arraySpans) {
            std::fill(sequentialBlock.begin() + static_cast<std::ptrdiff_t>(position),
                      sequentialBlock.begin() + static_cast<std::ptrdiff_t>(position + length), ' ');
        }
        for (std::sregex_iterator it(sequentialBlock.begin(), sequentialBlock.end(), sequentialRange), end; it != end; ++it) {
            const auto firstHex = (*it)[1].str();
            const auto lastHex = (*it)[2].str();
            const auto destination = (*it)[3].str();
            const auto first = HexToUInt(firstHex);
            const auto last = HexToUInt(lastHex);
            const auto bytes = static_cast<std::uint8_t>(firstHex.size() / 2U);
            if (last < first || last - first > 65535U) continue;
            for (std::uint32_t code = first; code <= last; ++code) {
                map.mappings_[MappingKey(code, bytes)] =
                    Utf16BeHexToUtf8(IncrementUtf16Hex(destination, code - first));
            }
        }
    }
    return map;
}

std::string PdfToUnicodeCMap::Decode(std::string_view bytes) const {
    std::string output;
    std::size_t offset{};
    while (offset < bytes.size()) {
        bool consumed = false;
        const auto available = bytes.size() - offset;
        for (std::uint8_t width = static_cast<std::uint8_t>(std::min<std::size_t>(maximumCodeBytes_, available));
             width >= minimumCodeBytes_; --width) {
            std::uint32_t code{};
            for (std::uint8_t i = 0; i < width; ++i) {
                code = (code << 8U) | static_cast<unsigned char>(bytes[offset + i]);
            }
            if (!IsInCodeSpace(code, width)) {
                if (width == minimumCodeBytes_) break;
                continue;
            }
            if (const auto found = mappings_.find(MappingKey(code, width)); found != mappings_.end()) {
                output += found->second;
            } else if (width == 1U && code <= 0x7FU) {
                output.push_back(static_cast<char>(code));
            }
            offset += width;
            consumed = true;
            break;
        }
        if (!consumed) ++offset;
    }
    return output;
}

} // namespace CPPPdf

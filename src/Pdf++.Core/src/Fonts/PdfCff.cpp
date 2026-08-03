#include <CPPPdf/Fonts/PdfCff.hpp>

#include <algorithm>
#include <stdexcept>

namespace CPPPdf {
namespace {
std::uint32_t readOffset(std::span<const std::byte> bytes, std::size_t offset, const std::size_t width) {
    if (width == 0U || width > 4U || offset + width > bytes.size()) throw std::runtime_error("Malformed CFF offset.");
    std::uint32_t value{};
    for (std::size_t i = 0; i < width; ++i) value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[offset + i]);
    return value;
}
}

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
    std::size_t offset = headerSize;
    const auto names = ParseIndex(bytes, offset);
    const auto tops = ParseIndex(bytes, offset);
    if (names.objects.empty() || tops.objects.empty()) throw std::runtime_error("CFF font indexes are empty.");
    PdfCffFont result;
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
} // namespace CPPPdf

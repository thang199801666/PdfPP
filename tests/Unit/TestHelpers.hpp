#pragma once

#include <CPPPdf/Objects/PdfObject.hpp>
#include "TestRunner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Shared helpers for unit tests: deterministic fixtures and codec round trips.
namespace CPPPdfTest {

struct LzwTestCode {
    std::uint32_t code{};
    int width{};
};

// Minimal PDF-style LZW encoder (early change on, codes packed LSB-first)
// used to exercise the decoder against a known-good round trip.
inline std::vector<LzwTestCode> EncodeLzw(const std::string& data) {
    std::unordered_map<std::string, std::uint32_t> table;
    for (std::uint32_t i = 0U; i < 256U; ++i) table[std::string(1, static_cast<char>(i))] = i;
    std::uint32_t nextCode = 258U;
    int width = 9;
    std::vector<LzwTestCode> output;
    const auto push = [&output, &width](const std::uint32_t code) {
        output.push_back(LzwTestCode{code, width});
    };
    push(256U);
    std::string current;
    for (const char ch : data) {
        const std::string candidate = current + ch;
        if (table.count(candidate) != 0U) {
            current = candidate;
            continue;
        }
        push(table[current]);
        if (nextCode < 4096U) table[candidate] = nextCode++;
        if (nextCode == ((1U << width) - 1U) && width < 12) ++width;
        current = std::string(1, ch);
    }
    if (!current.empty()) push(table[current]);
    push(257U);
    return output;
}

inline std::vector<std::byte> PackLzwLsbFirst(const std::vector<LzwTestCode>& codes) {
    std::vector<std::byte> bytes;
    std::size_t bitPosition = 0;
    for (const auto& item : codes) {
        for (int bit = 0; bit < item.width; ++bit) {
            if (bitPosition % 8U == 0U) bytes.push_back(std::byte{0});
            const std::uint32_t value = (item.code >> bit) & 1U;
            const std::size_t byteIndex = bitPosition / 8U;
            bytes[byteIndex] = static_cast<std::byte>(
                std::to_integer<unsigned char>(bytes[byteIndex]) |
                static_cast<unsigned char>(value << (bitPosition % 8U)));
            ++bitPosition;
        }
    }
    return bytes;
}

inline std::vector<std::byte> BuildMinimalCff() {
    const auto buildIndex = [](const std::vector<std::vector<std::byte>>& objects) {
        std::vector<std::byte> out;
        out.push_back(static_cast<std::byte>(objects.size() >> 8U));
        out.push_back(static_cast<std::byte>(objects.size() & 0xFF));
        if (objects.empty()) return out;
        out.push_back(std::byte{1}); // offSize = 1
        std::size_t running = 1U;
        for (const auto& object : objects) {
            out.push_back(static_cast<std::byte>(running & 0xFF));
            running += object.size();
        }
        out.push_back(static_cast<std::byte>(running & 0xFF));
        for (const auto& object : objects) out.insert(out.end(), object.begin(), object.end());
        return out;
    };
    const auto fixedShort = [](const int value) {
        return std::vector<std::byte>{std::byte{28},
            static_cast<std::byte>((value >> 8) & 0xFF), static_cast<std::byte>(value & 0xFF)};
    };
    std::vector<std::byte> font;
    font.push_back(std::byte{1}); font.push_back(std::byte{0});
    font.push_back(std::byte{4}); font.push_back(std::byte{4});
    const auto nameIndex = buildIndex({{std::byte{'T'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}}});
    font.insert(font.end(), nameIndex.begin(), nameIndex.end());
    // Top DICT: CharStrings (17) + Private (18), fixed-short operands.
    const std::size_t topDictDataSize = 3U + 1U + 3U + 3U + 1U;
    font.push_back(std::byte{0}); font.push_back(std::byte{1}); // count = 1
    font.push_back(std::byte{2}); // offSize = 2
    font.push_back(std::byte{0}); font.push_back(std::byte{1}); // INDEX offset[0]
    const auto topDictIndexOffset1 = static_cast<std::uint16_t>(topDictDataSize + 1U);
    font.push_back(static_cast<std::byte>((topDictIndexOffset1 >> 8U) & 0xFF));
    font.push_back(static_cast<std::byte>(topDictIndexOffset1 & 0xFF));
    const std::size_t topDictDataPos = font.size();
    font.resize(topDictDataPos + topDictDataSize, std::byte{0});

    const auto emptyIndex = buildIndex({});
    font.insert(font.end(), emptyIndex.begin(), emptyIndex.end()); // String
    font.insert(font.end(), emptyIndex.begin(), emptyIndex.end()); // GlobalSubr
    const std::size_t charStringsPos = font.size();
    const std::vector<std::byte> notdef{std::byte{14}};
    const std::vector<std::byte> boxGlyph{
        std::byte{139}, std::byte{139}, std::byte{21},
        std::byte{142}, std::byte{139}, std::byte{5},
        std::byte{139}, std::byte{141}, std::byte{5},
        std::byte{136}, std::byte{139}, std::byte{5},
        std::byte{139}, std::byte{137}, std::byte{5},
        std::byte{14}
    };
    const auto charStrings = buildIndex({notdef, boxGlyph});
    font.insert(font.end(), charStrings.begin(), charStrings.end());
    const std::size_t privatePos = font.size();
    const auto privateIndex = buildIndex({});
    font.insert(font.end(), privateIndex.begin(), privateIndex.end());

    auto topDict = fixedShort(static_cast<int>(charStringsPos));
    topDict.push_back(std::byte{17});
    const auto privateEncoded = fixedShort(0);
    topDict.insert(topDict.end(), privateEncoded.begin(), privateEncoded.end());
    const auto privateOffsetEncoded = fixedShort(static_cast<int>(privatePos));
    topDict.insert(topDict.end(), privateOffsetEncoded.begin(), privateOffsetEncoded.end());
    topDict.push_back(std::byte{18});
    for (std::size_t i = 0; i < topDict.size(); ++i) font[topDictDataPos + i] = topDict[i];
    return font;
}

} // namespace CPPPdfTest

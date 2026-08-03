#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <CPPPdf/Filters/PdfFilterPipeline.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <exception>

namespace CPPPdf {
namespace {

std::string Utf8(std::uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

const std::array<std::uint32_t, 128>& MacRomanTable() {
    static const std::array<std::uint32_t, 128> table{
        0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
        0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
        0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
        0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
        0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
        0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
        0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
        0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
        0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
        0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
        0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
        0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
        0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
        0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
        0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
        0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7
    };
    return table;
}

std::string GlyphNameToUnicode(std::string_view name) {
    if (name.size() == 1) return std::string(name);
    static const std::unordered_map<std::string_view, std::uint32_t> names{
        {"space", 0x20}, {"exclam", 0x21}, {"quotedbl", 0x22}, {"numbersign", 0x23},
        {"dollar", 0x24}, {"percent", 0x25}, {"ampersand", 0x26}, {"quotesingle", 0x27},
        {"parenleft", 0x28}, {"parenright", 0x29}, {"asterisk", 0x2A}, {"plus", 0x2B},
        {"comma", 0x2C}, {"hyphen", 0x2D}, {"period", 0x2E}, {"slash", 0x2F},
        {"colon", 0x3A}, {"semicolon", 0x3B}, {"less", 0x3C}, {"equal", 0x3D},
        {"greater", 0x3E}, {"question", 0x3F}, {"at", 0x40}, {"bracketleft", 0x5B},
        {"backslash", 0x5C}, {"bracketright", 0x5D}, {"asciicircum", 0x5E},
        {"underscore", 0x5F}, {"grave", 0x60}, {"braceleft", 0x7B}, {"bar", 0x7C},
        {"braceright", 0x7D}, {"asciitilde", 0x7E}, {"Euro", 0x20AC},
        {"bullet", 0x2022}, {"endash", 0x2013}, {"emdash", 0x2014},
        {"quoteleft", 0x2018}, {"quoteright", 0x2019}, {"quotedblleft", 0x201C},
        {"quotedblright", 0x201D}, {"ellipsis", 0x2026}, {"Omega", 0x03A9}
    };
    const auto it = names.find(name);
    if (it != names.end()) return Utf8(it->second);
    if (name.size() == 7 && name.substr(0, 3) == "uni") {
        try { return Utf8(static_cast<std::uint32_t>(std::stoul(std::string(name.substr(3)), nullptr, 16))); }
        catch (...) { return {}; }
    }
    if (name.size() >= 5 && name.front() == 'u') {
        try { return Utf8(static_cast<std::uint32_t>(std::stoul(std::string(name.substr(1)), nullptr, 16))); }
        catch (...) { return {}; }
    }
    return {};
}

PdfFontSubtype ParseSubtype(std::string_view subtype) {
    if (subtype == "Type1" || subtype == "MMType1") return PdfFontSubtype::Type1;
    if (subtype == "TrueType") return PdfFontSubtype::TrueType;
    if (subtype == "Type0") return PdfFontSubtype::Type0;
    if (subtype == "CIDFontType0") return PdfFontSubtype::CIDFontType0;
    if (subtype == "CIDFontType2") return PdfFontSubtype::CIDFontType2;
    if (subtype == "Type3") return PdfFontSubtype::Type3;
    return PdfFontSubtype::Unknown;
}

} // namespace

const PdfObject* PdfFontResource::ResolveObject(const PdfObject* object, const Resolver& resolver) {
    if (!object) return nullptr;
    const auto ref = object->AsReference();
    if (!ref || !resolver) return object;
    return &resolver(PdfReference{ref->first, ref->second});
}

std::string PdfFontResource::NameValue(const PdfObject* object, const Resolver& resolver) {
    object = ResolveObject(object, resolver);
    if (!object) return {};
    if (const auto* name = object->AsName()) return name->value();
    return {};
}

PdfFontResource PdfFontResource::Create(const PdfDictionary& dictionary, const Resolver& resolver) {
    PdfFontResource result;
    result.descriptor_.subtype = ParseSubtype(NameValue(dictionary.Find(PdfName("Subtype")), resolver));
    result.descriptor_.baseFont = NameValue(dictionary.Find(PdfName("BaseFont")), resolver);
    result.descriptor_.encoding = NameValue(dictionary.Find(PdfName("Encoding")), resolver);
    result.composite_ = result.descriptor_.subtype == PdfFontSubtype::Type0;
    result.identityEncoding_ = result.descriptor_.encoding == "Identity-H" || result.descriptor_.encoding == "Identity-V";

    for (std::uint32_t code = 32; code <= 126; ++code) result.simpleUnicode_[code] = Utf8(code);

    const PdfObject* encodingObject = ResolveObject(dictionary.Find(PdfName("Encoding")), resolver);
    if (encodingObject && encodingObject->AsDictionary()) {
        const auto* encoding = encodingObject->AsDictionary();
        result.descriptor_.encoding = NameValue(encoding->Find(PdfName("BaseEncoding")), resolver);
        const PdfObject* differencesObject = ResolveObject(encoding->Find(PdfName("Differences")), resolver);
        if (differencesObject && differencesObject->AsArray()) {
            std::uint32_t code = 0;
            for (const auto& item : differencesObject->AsArray()->values()) {
                if (const auto integer = item.AsInteger()) code = static_cast<std::uint32_t>(*integer);
                else if (const auto* name = item.AsName()) {
                    const auto unicode = GlyphNameToUnicode(name->value());
                    if (!unicode.empty()) result.simpleUnicode_[code] = unicode;
                    ++code;
                }
            }
        }
    }

    if (result.descriptor_.encoding == "MacRomanEncoding") {
        const auto& table = MacRomanTable();
        for (std::uint32_t code = 0x80; code <= 0xFF; ++code) {
            result.simpleUnicode_[code] = Utf8(table[code - 0x80]);
        }
    }

    const PdfObject* toUnicodeObject = ResolveObject(dictionary.Find(PdfName("ToUnicode")), resolver);
    if (toUnicodeObject && toUnicodeObject->AsStream()) {
        const auto& stream = *toUnicodeObject->AsStream();
        auto bytes = stream.bytes();
        std::vector<std::byte> decoded;
        std::vector<PdfFilterSpec> filters;
        const auto* filterObject = stream.dictionary().Find(PdfName("Filter"));
        filterObject = ResolveObject(filterObject, resolver);
        auto addFilter = [&filters](const PdfObject* object) {
            if (!object) return;
            if (const auto* name = object->AsName()) {
                filters.push_back({name->value(), {}});
            } else if (const auto* array = object->AsArray()) {
                for (const auto& item : array->values()) {
                    if (const auto* name = item.AsName()) filters.push_back({name->value(), {}});
                }
            }
        };
        addFilter(filterObject);
        if (!filters.empty()) {
            try {
                decoded = PdfFilterPipeline().Decode(bytes, filters);
                bytes = decoded;
            } catch (...) {
                // Keep the raw stream as a compatibility fallback for malformed PDFs.
            }
        }
        std::string source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        result.toUnicode_ = PdfToUnicodeCMap::Parse(source);
    }

    const PdfObject* descriptorObject = ResolveObject(dictionary.Find(PdfName("FontDescriptor")), resolver);
    if (descriptorObject && descriptorObject->AsDictionary()) {
        const auto* descriptor = descriptorObject->AsDictionary();
        const PdfObject* fontFileObject = ResolveObject(descriptor->Find(PdfName("FontFile2")), resolver);
        if (fontFileObject && fontFileObject->AsStream()) {
            const auto& stream = *fontFileObject->AsStream();
            std::vector<std::byte> bytes(stream.bytes().begin(), stream.bytes().end());
            const PdfObject* filterObject = ResolveObject(stream.dictionary().Find(PdfName("Filter")), resolver);
            std::vector<PdfFilterSpec> filters;
            if (filterObject) {
                if (const auto* name = filterObject->AsName()) filters.push_back({name->value(), {}});
                else if (const auto* array = filterObject->AsArray()) {
                    for (const auto& item : array->values())
                        if (const auto* name = item.AsName()) filters.push_back({name->value(), {}});
                }
            }
            try {
                if (!filters.empty()) bytes = PdfFilterPipeline().Decode(bytes, filters);
                std::vector<std::uint8_t> fontBytes;
                fontBytes.reserve(bytes.size());
                for (const auto byte : bytes) fontBytes.push_back(std::to_integer<std::uint8_t>(byte));
                result.embeddedTrueType_ = std::make_shared<const PdfTrueTypeFont>(
                    PdfTrueTypeFont::Parse(std::move(fontBytes), result.descriptor_.baseFont));
            } catch (const std::exception&) {
                // Keep malformed embedded fonts from making an otherwise readable
                // PDF unusable; the caller can use metrics/Unicode fallback.
            }
        }
        const PdfObject* cffObject = ResolveObject(descriptor->Find(PdfName("FontFile3")), resolver);
        if (cffObject && cffObject->AsStream()) {
            const auto subtype = cffObject->AsStream()->dictionary().GetAsName(PdfName("Subtype"));
            result.embeddedProgramSubtype_ = subtype ? subtype->value() : std::string{};
            result.embeddedCff_ = result.embeddedProgramSubtype_ == "Type1C" ||
                result.embeddedProgramSubtype_ == "CIDFontType0C" ||
                result.embeddedProgramSubtype_ == "OpenType";
        }
        const PdfObject* type1Object = ResolveObject(descriptor->Find(PdfName("FontFile")), resolver);
        result.embeddedType1_ = type1Object != nullptr && type1Object->AsStream() != nullptr;
    }

    if (!result.composite_) {
        const auto firstCharObject = ResolveObject(dictionary.Find(PdfName("FirstChar")), resolver);
        const std::uint32_t first = firstCharObject && firstCharObject->AsInteger()
            ? static_cast<std::uint32_t>(*firstCharObject->AsInteger()) : 0;
        const PdfObject* widthsObject = ResolveObject(dictionary.Find(PdfName("Widths")), resolver);
        if (widthsObject && widthsObject->AsArray()) {
            std::uint32_t code = first;
            for (const auto& width : widthsObject->AsArray()->values()) {
                if (const auto number = width.AsInteger()) result.widths_[code] = static_cast<std::uint32_t>(std::max<std::int64_t>(0, *number));
                else if (const auto real = width.AsReal()) result.widths_[code] = static_cast<std::uint32_t>(std::max(0.0, *real));
                ++code;
            }
        }
        result.defaultWidth_ = 500;
    } else {
        const PdfObject* descendantsObject = ResolveObject(dictionary.Find(PdfName("DescendantFonts")), resolver);
        if (descendantsObject && descendantsObject->AsArray() && !descendantsObject->AsArray()->empty()) {
            const PdfObject* descendantObject = ResolveObject(&descendantsObject->AsArray()->at(0), resolver);
            if (descendantObject && descendantObject->AsDictionary()) {
                const auto* descendant = descendantObject->AsDictionary();
                const PdfObject* dw = ResolveObject(descendant->Find(PdfName("DW")), resolver);
                if (dw && dw->AsInteger()) result.defaultWidth_ = static_cast<std::uint32_t>(*dw->AsInteger());
                const PdfObject* w = ResolveObject(descendant->Find(PdfName("W")), resolver);
                if (w && w->AsArray()) {
                    const auto& values = w->AsArray()->values();
                    std::size_t i = 0;
                    while (i < values.size()) {
                        const auto first = values[i++].AsInteger();
                        if (!first || i >= values.size()) break;
                        if (const auto* widthArray = values[i].AsArray()) {
                            std::uint32_t cid = static_cast<std::uint32_t>(*first);
                            for (const auto& width : widthArray->values()) {
                                if (const auto n = width.AsInteger()) result.widths_[cid] = static_cast<std::uint32_t>(*n);
                                else if (const auto r = width.AsReal()) result.widths_[cid] = static_cast<std::uint32_t>(*r);
                                ++cid;
                            }
                            ++i;
                        } else if (i + 1 < values.size()) {
                            const auto last = values[i++].AsInteger();
                            const auto width = values[i++].AsInteger();
                            if (last && width) {
                                for (std::int64_t cid = *first; cid <= *last; ++cid) result.widths_[static_cast<std::uint32_t>(cid)] = static_cast<std::uint32_t>(*width);
                            }
                        } else break;
                    }
                }
            }
        }
    }

    return result;
}

std::uint32_t PdfFontResource::GetGlyphWidth(std::uint32_t characterCode) const noexcept {
    const auto it = widths_.find(characterCode);
    return it == widths_.end() ? defaultWidth_ : it->second;
}

std::optional<std::uint16_t> PdfFontResource::GetEmbeddedGlyphId(
    const std::uint32_t characterCode) const noexcept {
    if (!embeddedTrueType_) return std::nullopt;
    if (identityEncoding_ && composite_) return embeddedTrueType_->GetGlyphId(characterCode);
    const auto unicode = simpleUnicode_.find(characterCode);
    if (unicode == simpleUnicode_.end()) return std::nullopt;
    const auto& text = unicode->second;
    if (text.empty()) return std::nullopt;
    const auto first = static_cast<unsigned char>(text[0]);
    std::uint32_t codePoint = first;
    std::size_t length = 1U;
    if ((first & 0xE0U) == 0xC0U) { codePoint = first & 0x1FU; length = 2U; }
    else if ((first & 0xF0U) == 0xE0U) { codePoint = first & 0x0FU; length = 3U; }
    else if ((first & 0xF8U) == 0xF0U) { codePoint = first & 0x07U; length = 4U; }
    if (length > text.size()) return std::nullopt;
    for (std::size_t i = 1U; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if ((byte & 0xC0U) != 0x80U) return std::nullopt;
        codePoint = (codePoint << 6U) | (byte & 0x3FU);
    }
    return embeddedTrueType_->GetGlyphId(codePoint);
}

std::size_t PdfFontResource::GetGlyphCount(const std::string_view encodedBytes) const noexcept {
    if (composite_) return (encodedBytes.size() + 1U) / 2U;
    return encodedBytes.size();
}

std::vector<std::uint32_t> PdfFontResource::GetCharacterCodes(
    const std::string_view encodedBytes) const {
    std::vector<std::uint32_t> result;
    result.reserve(GetGlyphCount(encodedBytes));
    if (composite_) {
        for (std::size_t i = 0; i < encodedBytes.size();) {
            std::uint32_t code = static_cast<unsigned char>(encodedBytes[i++]);
            if (i < encodedBytes.size()) code = (code << 8U) |
                static_cast<unsigned char>(encodedBytes[i++]);
            result.push_back(code);
        }
    } else {
        for (const unsigned char code : encodedBytes) result.push_back(code);
    }
    return result;
}

double PdfFontResource::MeasureEncodedText(const std::string_view encodedBytes) const noexcept {
    double width{};
    if (composite_) {
        for (std::size_t i = 0; i < encodedBytes.size();) {
            std::uint32_t code = static_cast<unsigned char>(encodedBytes[i++]);
            if (i < encodedBytes.size()) {
                code = (code << 8U) | static_cast<unsigned char>(encodedBytes[i++]);
            }
            width += static_cast<double>(GetGlyphWidth(code));
        }
        return width;
    }
    for (const unsigned char code : encodedBytes) width += static_cast<double>(GetGlyphWidth(code));
    return width;
}

std::string PdfFontResource::Decode(std::string_view encodedBytes) const {
    if (toUnicode_) return toUnicode_->Decode(encodedBytes);
    std::string output;
    if (composite_ && identityEncoding_) {
        for (std::size_t i = 0; i + 1 < encodedBytes.size(); i += 2) {
            const auto code = (static_cast<std::uint32_t>(static_cast<unsigned char>(encodedBytes[i])) << 8) |
                              static_cast<std::uint32_t>(static_cast<unsigned char>(encodedBytes[i + 1]));
            output += Utf8(code);
        }
        return output;
    }
    for (const unsigned char code : encodedBytes) {
        const auto it = simpleUnicode_.find(code);
        output += it == simpleUnicode_.end() ? Utf8(code) : it->second;
    }
    return output;
}

} // namespace CPPPdf

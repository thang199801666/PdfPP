#include <CPPPdf/Validation/PdfConformance.hpp>

#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Document/PdfPage.hpp>
#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include "Internal/Security/PdfCrypto.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace CPPPdf {
namespace {

struct PdfARequirements final {
    int part{};
    char level{};
    bool tagged{};
    bool unicode{};
    bool transparencyAllowed{};
    bool arbitraryEmbeddedFiles{};
    bool engineeringVariant{};
    std::string version;
};

struct PdfUaRequirements final {
    int part{};
    int revision{};
    std::string version;
};

[[nodiscard]] std::optional<PdfARequirements> PdfARequirementsFor(
    const PdfConformanceProfile profile) {
    switch (profile) {
    case PdfConformanceProfile::PdfA1A: return PdfARequirements{1, 'A', true, false, false, false, false, "1.4"};
    case PdfConformanceProfile::PdfA1B: return PdfARequirements{1, 'B', false, false, false, false, false, "1.4"};
    case PdfConformanceProfile::PdfA2A: return PdfARequirements{2, 'A', true, false, true, false, false, "1.7"};
    case PdfConformanceProfile::PdfA2B: return PdfARequirements{2, 'B', false, false, true, false, false, "1.7"};
    case PdfConformanceProfile::PdfA2U: return PdfARequirements{2, 'U', false, true, true, false, false, "1.7"};
    case PdfConformanceProfile::PdfA3A: return PdfARequirements{3, 'A', true, false, true, true, false, "1.7"};
    case PdfConformanceProfile::PdfA3B: return PdfARequirements{3, 'B', false, false, true, true, false, "1.7"};
    case PdfConformanceProfile::PdfA3U: return PdfARequirements{3, 'U', false, true, true, true, false, "1.7"};
    case PdfConformanceProfile::PdfA4: return PdfARequirements{4, '\0', false, false, true, false, false, "2.0"};
    case PdfConformanceProfile::PdfA4E: return PdfARequirements{4, 'E', false, false, true, true, true, "2.0"};
    case PdfConformanceProfile::PdfA4F: return PdfARequirements{4, 'F', false, false, true, true, false, "2.0"};
    default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<PdfUaRequirements> PdfUaRequirementsFor(
    const PdfConformanceProfile profile) {
    switch (profile) {
    case PdfConformanceProfile::PdfUA1: return PdfUaRequirements{1, 0, "1.7"};
    case PdfConformanceProfile::PdfUA2: return PdfUaRequirements{2, 2024, "2.0"};
    default: return std::nullopt;
    }
}

[[nodiscard]] std::string ProfileName(const PdfConformanceProfile profile) {
    switch (profile) {
    case PdfConformanceProfile::Pdf17: return "PDF 1.7";
    case PdfConformanceProfile::Pdf20: return "PDF 2.0";
    case PdfConformanceProfile::PdfA1A: return "PDF/A-1A";
    case PdfConformanceProfile::PdfA1B: return "PDF/A-1B";
    case PdfConformanceProfile::PdfA2A: return "PDF/A-2A";
    case PdfConformanceProfile::PdfA2B: return "PDF/A-2B";
    case PdfConformanceProfile::PdfA2U: return "PDF/A-2U";
    case PdfConformanceProfile::PdfA3A: return "PDF/A-3A";
    case PdfConformanceProfile::PdfA3B: return "PDF/A-3B";
    case PdfConformanceProfile::PdfA3U: return "PDF/A-3U";
    case PdfConformanceProfile::PdfA4: return "PDF/A-4";
    case PdfConformanceProfile::PdfA4E: return "PDF/A-4E";
    case PdfConformanceProfile::PdfA4F: return "PDF/A-4F";
    case PdfConformanceProfile::PdfUA1: return "PDF/UA-1";
    case PdfConformanceProfile::PdfUA2: return "PDF/UA-2";
    }
    return "PDF";
}

[[nodiscard]] std::string JsonEscape(const std::string_view value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(character) << std::dec;
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    return output.str();
}

class ValidationContext final {
public:
    ValidationContext(PdfValidationResult& result, const PdfValidationOptions& options)
        : result_(result), options_(options) {}

    void Error(std::string code, std::string message, std::string clause = {},
               std::optional<std::size_t> page = {},
               std::optional<std::uint32_t> object = {}, std::string path = {}) {
        Add({std::move(code), std::move(message), true, PdfValidationSeverity::Error,
             std::move(clause), page, object, std::move(path)});
    }

    void Warning(std::string code, std::string message, std::string clause = {},
                 std::optional<std::size_t> page = {},
                 std::optional<std::uint32_t> object = {}, std::string path = {}) {
        Add({std::move(code), std::move(message), false, PdfValidationSeverity::Warning,
             std::move(clause), page, object, std::move(path)});
    }

    void Info(std::string code, std::string message) {
        PdfValidationIssue issue;
        issue.code = std::move(code);
        issue.message = std::move(message);
        issue.error = false;
        issue.severity = PdfValidationSeverity::Info;
        Add(std::move(issue));
    }

    [[nodiscard]] bool Stop() const noexcept {
        if (options_.maxIssues != 0U && result_.issues.size() >= options_.maxIssues) return true;
        return options_.failFast && std::any_of(result_.issues.begin(), result_.issues.end(),
                                                [](const auto& issue) { return issue.IsError(); });
    }

private:
    void Add(PdfValidationIssue issue) {
        if (options_.maxIssues != 0U && result_.issues.size() >= options_.maxIssues) return;
        result_.issues.push_back(std::move(issue));
    }

    PdfValidationResult& result_;
    const PdfValidationOptions& options_;
};

[[nodiscard]] const PdfObject* ResolveObject(
    const PdfDocument& document, const PdfObject* object) {
    if (!object) return nullptr;
    const auto reference = object->AsReference();
    if (!reference) return object;
    try {
        return &document.GetObject(PdfReference{reference->first, reference->second});
    } catch (...) {
        return nullptr;
    }
}

[[nodiscard]] const PdfDictionary* ResolveDictionary(
    const PdfDocument& document, const PdfObject* object) {
    object = ResolveObject(document, object);
    if (!object) return nullptr;
    if (const auto* dictionary = object->AsDictionary()) return dictionary;
    if (const auto* stream = object->AsStream()) return &stream->dictionary();
    return nullptr;
}

[[nodiscard]] const PdfArray* ResolveArray(
    const PdfDocument& document, const PdfObject* object) {
    object = ResolveObject(document, object);
    return object ? object->AsArray() : nullptr;
}

[[nodiscard]] std::string DictionaryStringOrName(
    const PdfDocument& document, const PdfDictionary* dictionary,
    const std::string_view key) {
    if (!dictionary) return {};
    const auto* object = ResolveObject(document, dictionary->Find(PdfName(std::string(key))));
    if (!object) return {};
    if (const auto* name = object->AsName()) return name->value();
    if (const auto* string = object->AsString()) return *string;
    return {};
}

[[nodiscard]] std::optional<std::int64_t> DictionaryInteger(
    const PdfDocument& document, const PdfDictionary* dictionary,
    const std::string_view key) {
    if (!dictionary) return std::nullopt;
    const auto* object = ResolveObject(document, dictionary->Find(PdfName(std::string(key))));
    return object ? object->AsInteger() : std::nullopt;
}

[[nodiscard]] bool DictionaryBoolean(
    const PdfDocument& document, const PdfDictionary* dictionary,
    const std::string_view key, const bool fallback = false) {
    if (!dictionary) return fallback;
    const auto* object = ResolveObject(document, dictionary->Find(PdfName(std::string(key))));
    return object ? object->AsBoolean().value_or(fallback) : fallback;
}

[[nodiscard]] std::string DecodeStream(
    const PdfDocument& document, const PdfObject* object) {
    object = ResolveObject(document, object);
    const auto* stream = object ? object->AsStream() : nullptr;
    if (!stream) return {};
    std::vector<std::byte> bytes(stream->bytes().begin(), stream->bytes().end());
    const auto* filterObject = ResolveObject(document, stream->dictionary().Find(PdfName("Filter")));
    if (filterObject) {
        std::vector<PdfFilterSpec> filters;
        if (const auto* name = filterObject->AsName()) filters.push_back({name->value(), {}});
        else if (const auto* array = filterObject->AsArray()) {
            for (const auto& item : array->values()) {
                const auto* resolvedItem = ResolveObject(document, &item);
                if (resolvedItem) {
                    if (const auto* name = resolvedItem->AsName()) {
                        filters.push_back({name->value(), {}});
                    }
                }
            }
        }
        try {
            if (!filters.empty()) bytes = PdfFilterPipeline().Decode(bytes, filters);
        } catch (...) {
            return {};
        }
    }
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

[[nodiscard]] std::string XmlUnescape(std::string value) {
    const std::pair<std::string_view, std::string_view> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"}
    };
    for (const auto& [encoded, decoded] : entities) {
        std::size_t position = 0U;
        while ((position = value.find(encoded, position)) != std::string::npos) {
            value.replace(position, encoded.size(), decoded);
            position += decoded.size();
        }
    }
    return value;
}

[[nodiscard]] std::string XmpElement(
    const std::string& metadata, const std::string_view tag) {
    const std::string open = "<" + std::string(tag) + ">";
    const std::string close = "</" + std::string(tag) + ">";
    const auto begin = metadata.find(open);
    if (begin == std::string::npos) return {};
    const auto start = begin + open.size();
    const auto end = metadata.find(close, start);
    if (end == std::string::npos) return {};
    return XmlUnescape(metadata.substr(start, end - start));
}

[[nodiscard]] std::string XmpDcTitle(const std::string& metadata) {
    const auto title = metadata.find("<dc:title");
    if (title == std::string::npos) return {};
    const auto li = metadata.find("<rdf:li", title);
    if (li == std::string::npos) return {};
    const auto begin = metadata.find('>', li);
    const auto end = metadata.find("</rdf:li>", begin == std::string::npos ? li : begin);
    if (begin == std::string::npos || end == std::string::npos) return {};
    return XmlUnescape(metadata.substr(begin + 1U, end - begin - 1U));
}


[[nodiscard]] std::string XmpDcLanguage(const std::string& metadata) {
    const auto language = metadata.find("<dc:language");
    if (language == std::string::npos) return {};
    const auto li = metadata.find("<rdf:li", language);
    if (li == std::string::npos) return {};
    const auto begin = metadata.find('>', li);
    const auto end = metadata.find("</rdf:li>", begin == std::string::npos ? li : begin);
    if (begin == std::string::npos || end == std::string::npos) return {};
    return XmlUnescape(metadata.substr(begin + 1U, end - begin - 1U));
}

[[nodiscard]] bool IsValidLanguageTag(const std::string_view value) {
    if (value.empty() || value.front() == '-' || value.back() == '-') return false;
    std::size_t segmentLength = 0U;
    for (const unsigned char character : value) {
        if (character == '-') {
            if (segmentLength == 0U || segmentLength > 8U) return false;
            segmentLength = 0U;
            continue;
        }
        if (!std::isalnum(character)) return false;
        ++segmentLength;
        if (segmentLength > 8U) return false;
    }
    return segmentLength != 0U;
}

[[nodiscard]] std::vector<std::string> DictionaryStrings(
    const PdfDocument& document, const PdfDictionary* dictionary,
    const std::string_view key) {
    std::vector<std::string> values;
    if (!dictionary) return values;
    const auto append = [&](const PdfObject* object) {
        object = ResolveObject(document, object);
        if (!object) return;
        if (const auto* string = object->AsString()) values.push_back(*string);
        else if (const auto* name = object->AsName()) values.push_back(name->value());
    };
    const auto* raw = dictionary->Find(PdfName(std::string(key)));
    const auto* resolved = ResolveObject(document, raw);
    if (const auto* array = resolved ? resolved->AsArray() : nullptr) {
        for (const auto& item : array->values()) append(&item);
    } else append(raw);
    return values;
}

[[nodiscard]] std::string InheritedFieldStringOrName(
    const PdfDocument& document, const PdfDictionary* field,
    const std::string_view key) {
    std::set<std::pair<std::uint32_t, std::uint16_t>> visited;
    for (std::size_t depth = 0U; field && depth < 64U; ++depth) {
        const std::string value = DictionaryStringOrName(document, field, key);
        if (!value.empty()) return value;
        const auto* parentObject = field->Find(PdfName("Parent"));
        if (!parentObject) break;
        if (const auto reference = parentObject->AsReference()) {
            if (!visited.insert(*reference).second) break;
        }
        field = ResolveDictionary(document, parentObject);
    }
    return {};
}

[[nodiscard]] bool HasNormalAppearance(
    const PdfDocument& document, const PdfDictionary* annotation) {
    const auto* appearance = ResolveDictionary(
        document, annotation ? annotation->Find(PdfName("AP")) : nullptr);
    if (!appearance) return false;
    const auto* normal = ResolveObject(document, appearance->Find(PdfName("N")));
    return normal && !normal->IsNull();
}

[[nodiscard]] bool VersionMatches(const PdfDocument& document, const std::string_view expected) {
    return document.GetVersion() == expected;
}

[[nodiscard]] bool IsPaintingEvent(const PdfContentEvent& event) {
    switch (event.type) {
    case PdfContentEventType::RenderText:
    case PdfContentEventType::InvokeXObject:
    case PdfContentEventType::RenderInlineImage:
    case PdfContentEventType::PaintShading:
        return true;
    case PdfContentEventType::RenderPath:
        return event.operation == "S" || event.operation == "s" || event.operation == "f" ||
               event.operation == "F" || event.operation == "f*" || event.operation == "B" ||
               event.operation == "B*" || event.operation == "b" || event.operation == "b*";
    default: return false;
    }
}

[[nodiscard]] std::optional<std::uint32_t> ParseMcid(const std::string_view property) {
    const auto marker = property.find("/MCID");
    if (marker == std::string_view::npos) return std::nullopt;
    std::size_t position = marker + 5U;
    while (position < property.size() && std::isspace(static_cast<unsigned char>(property[position]))) ++position;
    std::uint64_t value = 0U;
    bool found = false;
    while (position < property.size() && std::isdigit(static_cast<unsigned char>(property[position]))) {
        found = true;
        value = value * 10U + static_cast<unsigned int>(property[position] - '0');
        if (value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
        ++position;
    }
    return found ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(value)) : std::nullopt;
}

struct PageTagInspection final {
    std::set<std::uint32_t> mcids;
    std::vector<std::uint32_t> mcidOrder;
    bool untaggedPainting{};
    bool unbalanced{};
};

[[nodiscard]] PageTagInspection InspectPageTags(
    const PdfDocument& document, const std::size_t pageIndex) {
    PageTagInspection inspection;
    struct State { bool semantic{}; bool artifact{}; };
    std::vector<State> stack;
    try {
        document.ForEachPageContentEvent(pageIndex, [&](const PdfContentEvent& event) {
            if (event.type == PdfContentEventType::BeginMarkedContent) {
                const auto mcid = ParseMcid(event.markedContentProperty);
                if (mcid) {
                    inspection.mcidOrder.push_back(*mcid);
                    if (!inspection.mcids.insert(*mcid).second) inspection.unbalanced = true;
                }
                stack.push_back({mcid.has_value(), event.text == "Artifact"});
                return;
            }
            if (event.type == PdfContentEventType::EndMarkedContent) {
                if (stack.empty()) inspection.unbalanced = true;
                else stack.pop_back();
                return;
            }
            if (!IsPaintingEvent(event)) return;
            const bool covered = std::any_of(stack.begin(), stack.end(), [](const auto& state) {
                return state.semantic || state.artifact;
            });
            if (!covered) inspection.untaggedPainting = true;
        });
    } catch (...) {
        inspection.unbalanced = true;
    }
    if (!stack.empty()) inspection.unbalanced = true;
    return inspection;
}

[[nodiscard]] const PdfObject* FindNumberTreeValue(
    const PdfDocument& document, const PdfDictionary* numberTree, const std::int64_t key) {
    if (!numberTree) return nullptr;
    if (const auto* numbers = ResolveArray(document, numberTree->Find(PdfName("Nums")))) {
        for (std::size_t index = 0U; index + 1U < numbers->size(); index += 2U) {
            const auto* keyObject = ResolveObject(document, &numbers->at(index));
            if (keyObject && keyObject->AsInteger().value_or(std::numeric_limits<std::int64_t>::min()) == key) {
                return &numbers->at(index + 1U);
            }
        }
    }
    if (const auto* kids = ResolveArray(document, numberTree->Find(PdfName("Kids")))) {
        for (const auto& kid : kids->values()) {
            if (const auto* value = FindNumberTreeValue(document, ResolveDictionary(document, &kid), key)) return value;
        }
    }
    return nullptr;
}

struct EmbeddedFileNameTreeInfo final {
    std::map<std::string, std::pair<std::uint32_t, std::uint16_t>> entries;
    bool malformed{};
};

void CollectEmbeddedFileNameTree(
    const PdfDocument& document, const PdfDictionary* tree,
    EmbeddedFileNameTreeInfo& result, const std::size_t depth = 0U) {
    if (!tree || depth > 64U) {
        if (depth > 64U) result.malformed = true;
        return;
    }
    if (const auto* names = ResolveArray(document, tree->Find(PdfName("Names")))) {
        if ((names->size() % 2U) != 0U) result.malformed = true;
        std::string previous;
        for (std::size_t index = 0U; index + 1U < names->size(); index += 2U) {
            const auto* key = ResolveObject(document, &names->at(index));
            const auto* value = &names->at(index + 1U);
            const auto* string = key ? key->AsString() : nullptr;
            const auto reference = value->AsReference();
            if (!string || !reference || (!previous.empty() && *string <= previous)) {
                result.malformed = true;
                continue;
            }
            previous = *string;
            if (!result.entries.emplace(*string, *reference).second) result.malformed = true;
        }
    }
    if (const auto* kids = ResolveArray(document, tree->Find(PdfName("Kids")))) {
        for (const auto& kid : kids->values()) {
            CollectEmbeddedFileNameTree(document, ResolveDictionary(document, &kid), result, depth + 1U);
        }
    }
}

[[nodiscard]] std::set<std::pair<std::uint32_t, std::uint16_t>> CollectAssociatedFileReferences(
    const PdfDocument& document) {
    std::set<std::pair<std::uint32_t, std::uint16_t>> references;
    for (const auto objectNumber : document.objectNumbers()) {
        const auto xref = document.GetXrefEntry(objectNumber);
        if (!xref || !xref->inUse) continue;
        const PdfObject* object = nullptr;
        try { object = &document.GetObject(PdfReference{objectNumber, xref->generation}); }
        catch (...) { continue; }
        const auto* dictionary = ResolveDictionary(document, object);
        if (!dictionary) continue;
        const auto* associated = ResolveArray(document, dictionary->Find(PdfName("AF")));
        if (!associated) continue;
        for (const auto& item : associated->values()) {
            if (const auto reference = item.AsReference()) references.insert(*reference);
        }
    }
    return references;
}

[[nodiscard]] bool EmbeddedFileChecksumMatches(
    const PdfDocument& document, const PdfObject* streamObject, const PdfDictionary* params) {
    if (!params) return false;
    streamObject = ResolveObject(document, streamObject);
    if (!streamObject || !streamObject->AsStream()) return false;
    const auto* checksumObject = ResolveObject(document, params->Find(PdfName("CheckSum")));
    const auto* checksum = checksumObject ? checksumObject->AsString() : nullptr;
    if (!checksum || checksum->size() != 16U) return false;
    const std::string decoded = DecodeStream(document, streamObject);
    const auto digest = Internal::Md5(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(decoded.data()), decoded.size()));
    return std::equal(digest.begin(), digest.end(),
                      reinterpret_cast<const std::uint8_t*>(checksum->data()));
}

[[nodiscard]] bool IsKnownRole(const std::string_view role, const int uaPart) {
    static const std::unordered_set<std::string> common = {
        "Document", "Part", "Art", "Sect", "Div", "BlockQuote", "Caption", "TOC", "TOCI",
        "Index", "NonStruct", "Private", "H", "H1", "H2", "H3", "H4", "H5", "H6", "P",
        "L", "LI", "Lbl", "LBody", "Table", "TR", "TH", "TD", "THead", "TBody", "TFoot",
        "Span", "Quote", "Note", "Reference", "BibEntry", "Code", "Link", "Annot", "Ruby",
        "RB", "RT", "RP", "Warichu", "WT", "WP", "Figure", "Formula", "Form"
    };
    if (common.contains(std::string(role))) return true;
    if (uaPart == 2) {
        static const std::unordered_set<std::string> pdf20 = {
            "DocumentFragment", "Aside", "Title", "FENote", "Sub", "Em", "Strong", "Artifact"
        };
        return pdf20.contains(std::string(role));
    }
    return false;
}

[[nodiscard]] bool HasValidOutputIntent(
    const PdfDocument& document, ValidationContext& context) {
    const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    const auto* intents = ResolveArray(document, catalog ? catalog->Find(PdfName("OutputIntents")) : nullptr);
    if (!intents || intents->empty()) return false;
    std::size_t pdfaIntentCount = 0U;
    for (const auto& item : intents->values()) {
        const auto* dictionary = ResolveDictionary(document, &item);
        if (!dictionary || DictionaryStringOrName(document, dictionary, "S") != "GTS_PDFA1") continue;
        ++pdfaIntentCount;
        if (DictionaryStringOrName(document, dictionary, "OutputConditionIdentifier").empty()) {
            context.Error("PDFA-OUTPUT-008", "PDF/A output intent requires /OutputConditionIdentifier.",
                          "ISO 19005 output intent", {}, {}, "/Root/OutputIntents/OutputConditionIdentifier");
        }
        const auto* profileObject = ResolveObject(document, dictionary->Find(PdfName("DestOutputProfile")));
        const auto* stream = profileObject ? profileObject->AsStream() : nullptr;
        if (!stream) {
            context.Error("PDFA-OUTPUT-003", "Output intent is missing an ICC profile stream.",
                          "ISO 19005 output intent", {}, {}, "/Root/OutputIntents/DestOutputProfile");
            continue;
        }
        const auto components = DictionaryInteger(document, &stream->dictionary(), "N");
        if (!components || (*components != 1 && *components != 3 && *components != 4)) {
            context.Error("PDFA-OUTPUT-004", "ICC output profile /N must be 1, 3, or 4.",
                          "ISO 19005 output intent", {}, {}, "/Root/OutputIntents/DestOutputProfile/N");
        }
        const auto bytes = stream->bytes();
        if (bytes.size() < 128U || bytes[36] != static_cast<std::byte>('a') ||
            bytes[37] != static_cast<std::byte>('c') || bytes[38] != static_cast<std::byte>('s') ||
            bytes[39] != static_cast<std::byte>('p')) {
            context.Error("PDFA-OUTPUT-005", "Embedded output profile has no valid ICC header signature.",
                          "ISO 19005 output intent", {}, {}, "/Root/OutputIntents/DestOutputProfile");
            continue;
        }
        const auto byte = [&](const std::size_t index) {
            return static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[index]));
        };
        const std::uint32_t declaredSize = (byte(0U) << 24U) | (byte(1U) << 16U) |
                                           (byte(2U) << 8U) | byte(3U);
        if (declaredSize != bytes.size()) {
            context.Error("PDFA-OUTPUT-006", "ICC profile header size does not match the embedded stream length.",
                          "ICC profile integrity", {}, {}, "/Root/OutputIntents/DestOutputProfile");
        }
        if (components) {
            const std::string colorSignature{
                static_cast<char>(std::to_integer<unsigned char>(bytes[16])),
                static_cast<char>(std::to_integer<unsigned char>(bytes[17])),
                static_cast<char>(std::to_integer<unsigned char>(bytes[18])),
                static_cast<char>(std::to_integer<unsigned char>(bytes[19]))};
            const std::string expected = *components == 1 ? "GRAY" : (*components == 3 ? "RGB " : "CMYK");
            if (colorSignature != expected) {
                context.Error("PDFA-OUTPUT-007", "ICC profile color-space signature does not match /N.",
                              "ICC profile integrity", {}, {}, "/Root/OutputIntents/DestOutputProfile/N");
            }
        }
    }
    if (pdfaIntentCount > 1U) {
        context.Error("PDFA-OUTPUT-009", "A PDF/A document must not contain multiple PDF/A output intents.",
                      "ISO 19005 output intent", {}, {}, "/Root/OutputIntents");
    }
    return pdfaIntentCount != 0U;
}

[[nodiscard]] bool ObjrMatchesAnnotation(
    const PdfDocument& document, const PdfDictionary* structureElement,
    const std::optional<std::pair<std::uint32_t, std::uint16_t>>& annotationReference) {
    if (!structureElement || !annotationReference) return false;
    const auto* kids = ResolveObject(document, structureElement->Find(PdfName("K")));
    const auto inspect = [&](const PdfObject* object) {
        const auto* dictionary = ResolveDictionary(document, object);
        if (!dictionary || DictionaryStringOrName(document, dictionary, "Type") != "OBJR") return false;
        const auto* target = dictionary->Find(PdfName("Obj"));
        const auto reference = target ? target->AsReference() : std::nullopt;
        return reference && *reference == *annotationReference;
    };
    if (!kids) return false;
    if (inspect(kids)) return true;
    if (const auto* array = kids->AsArray()) {
        return std::any_of(array->values().begin(), array->values().end(),
                           [&](const auto& item) { return inspect(&item); });
    }
    return false;
}

void ValidateMetadata(const PdfDocument& document,
                      const std::optional<PdfARequirements>& pdfA,
                      const std::optional<PdfUaRequirements>& pdfUa,
                      ValidationContext& context) {
    const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    const auto* metadataObject = catalog ? catalog->Find(PdfName("Metadata")) : nullptr;
    const auto* metadataStreamObject = ResolveObject(document, metadataObject);
    const auto* metadataStream = metadataStreamObject ? metadataStreamObject->AsStream() : nullptr;
    if (!metadataStream) {
        context.Error("META-XMP-001", "Conforming PDF requires a document XMP metadata stream.",
                      "ISO 19005/14289 metadata", {}, {}, "/Root/Metadata");
        return;
    }
    if (DictionaryStringOrName(document, &metadataStream->dictionary(), "Subtype") != "XML") {
        context.Error("META-XMP-002", "Metadata stream must have /Subtype /XML.",
                      "ISO 19005/14289 metadata", {}, {}, "/Root/Metadata/Subtype");
    }
    const std::string metadata = DecodeStream(document, metadataObject);
    if (metadata.find("<x:xmpmeta") == std::string::npos || metadata.find("<rdf:RDF") == std::string::npos) {
        context.Error("META-XMP-003", "Metadata stream is not a recognizable XMP packet.",
                      "ISO 19005/14289 metadata", {}, {}, "/Root/Metadata");
    }
    if (pdfA) {
        if (XmpElement(metadata, "pdfaid:part") != std::to_string(pdfA->part)) {
            context.Error("PDFA-METADATA-001", "pdfaid:part does not match the requested profile.",
                          "ISO 19005 identification", {}, {}, "/Root/Metadata/pdfaid:part");
        }
        if (pdfA->level != '\0' && XmpElement(metadata, "pdfaid:conformance") != std::string(1U, pdfA->level)) {
            context.Error("PDFA-METADATA-002", "pdfaid:conformance does not match the requested profile.",
                          "ISO 19005 identification", {}, {}, "/Root/Metadata/pdfaid:conformance");
        }
        if (metadata.find("<pdfuaid:part>") != std::string::npos) {
            const bool declared = metadata.find("<pdfaExtension:schemas>") != std::string::npos &&
                metadata.find("<pdfaSchema:namespaceURI>http://www.aiim.org/pdfua/ns/id/</pdfaSchema:namespaceURI>") != std::string::npos &&
                metadata.find("<pdfaSchema:prefix>pdfuaid</pdfaSchema:prefix>") != std::string::npos &&
                metadata.find("<pdfaProperty:name>part</pdfaProperty:name>") != std::string::npos;
            if (!declared) {
                context.Error("PDFA-XMP-EXT-001",
                              "PDF/A metadata using pdfuaid properties must declare the PDF/UA extension schema.",
                              "ISO 19005 XMP extension schemas", {}, {}, "/Root/Metadata/pdfaExtension:schemas");
            }
            if (metadata.find("<pdfuaid:rev>") != std::string::npos &&
                metadata.find("<pdfaProperty:name>rev</pdfaProperty:name>") == std::string::npos) {
                context.Error("PDFA-XMP-EXT-002",
                              "The PDF/UA revision property is not declared in the PDF/A extension schema.",
                              "ISO 19005 XMP extension schemas", {}, {}, "/Root/Metadata/pdfaExtension:schemas");
            }
        }
    }
    if (pdfUa) {
        if (XmpElement(metadata, "pdfuaid:part") != std::to_string(pdfUa->part)) {
            context.Error("PDFUA-METADATA-001", "pdfuaid:part does not match the requested profile.",
                          "ISO 14289 identification", {}, {}, "/Root/Metadata/pdfuaid:part");
        }
        if (pdfUa->part == 2 && XmpElement(metadata, "pdfuaid:rev") != "2024") {
            context.Error("PDFUA2-METADATA-002", "PDF/UA-2 requires pdfuaid:rev=2024.",
                          "ISO 14289-2 identification", {}, {}, "/Root/Metadata/pdfuaid:rev");
        }
        const std::string infoTitle = document.GetDocumentInfo().title;
        const std::string xmpTitle = XmpDcTitle(metadata);
        if (infoTitle.empty() || xmpTitle.empty()) {
            context.Error("PDFUA-TITLE-001", "PDF/UA requires a title in document information and XMP metadata.");
        } else if (infoTitle != xmpTitle) {
            context.Error("PDFUA-TITLE-002", "Document title and XMP dc:title are inconsistent.");
        }
        const std::string catalogLanguage = DictionaryStringOrName(document, catalog, "Lang");
        const std::string xmpLanguage = XmpDcLanguage(metadata);
        if (xmpLanguage.empty()) {
            context.Error("PDFUA-LANG-002", "PDF/UA XMP metadata requires dc:language.",
                          "ISO 14289 metadata", {}, {}, "/Root/Metadata/dc:language");
        } else if (catalogLanguage != xmpLanguage) {
            context.Error("PDFUA-LANG-003", "Catalog /Lang and XMP dc:language are inconsistent.",
                          "ISO 14289 metadata", {}, {}, "/Root/Metadata/dc:language");
        }
        if (!catalogLanguage.empty() && !IsValidLanguageTag(catalogLanguage)) {
            context.Error("PDFUA-LANG-004", "Document language is not a valid BCP 47-style tag.",
                          "ISO 14289 natural language", {}, {}, "/Root/Lang");
        }
    }
}

void ValidateFonts(const PdfDocument& document, const bool requireUnicode,
                   ValidationContext& context) {
    std::set<std::pair<std::size_t, std::string>> visited;
    for (std::size_t page = 0U; page < document.GetPageCount(); ++page) {
        try {
            document.ForEachPageContentEvent(page, [&](const PdfContentEvent& event) {
                if (event.type != PdfContentEventType::SetFont || event.textState.fontResource.empty()) return;
                if (!visited.insert({page, event.textState.fontResource}).second) return;
                try {
                    const auto font = document.ResolveFont(page, event.resourceObjectNumber, event.textState.fontResource);
                    if (!font) {
                        context.Error("FONT-RESOLVE-001", "Font resource cannot be resolved.",
                                      "ISO 19005/14289 fonts", page, {}, event.textState.fontResource);
                        return;
                    }
                    const bool type3 = font->GetSubtype() == PdfFontSubtype::Type3;
                    const bool embedded = type3 || font->GetEmbeddedTrueTypeFont() != nullptr ||
                        font->HasEmbeddedCffFont() || font->HasEmbeddedType1Font();
                    if (!embedded) {
                        context.Error("PDFA-FONT-002", "Conforming PDF requires embedded fonts.",
                                      "ISO 19005/14289 fonts", page, {}, event.textState.fontResource);
                    }
                    if (requireUnicode && !font->HasUnicodeMapping()) {
                        context.Error("FONT-UNICODE-001", "Text font requires a ToUnicode mapping.",
                                      "ISO 19005/14289 text", page, {}, event.textState.fontResource + "/ToUnicode");
                    }
                } catch (const std::exception& error) {
                    context.Error("FONT-RESOLVE-002", std::string("Font inspection failed: ") + error.what(),
                                  "ISO 19005/14289 fonts", page, {}, event.textState.fontResource);
                }
            });
        } catch (const std::exception& error) {
            context.Error("FONT-CONTENT-001", std::string("Page content font scan failed: ") + error.what(),
                          "ISO 19005/14289 fonts", page);
        }
    }
}

void ValidatePdfAObjects(const PdfDocument& document, const PdfARequirements& requirements,
                         ValidationContext& context, const PdfValidationOptions& options) {
    static const std::unordered_set<std::string> forbiddenActions = {
        "JavaScript", "Launch", "ImportData", "Hide", "Rendition", "Trans", "GoTo3DView",
        "RichMediaExecute", "Movie", "Sound"
    };
    static const std::unordered_set<std::string> pdfA1ForbiddenAnnotations = {
        "Sound", "Movie", "Screen", "FileAttachment"
    };

    const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    EmbeddedFileNameTreeInfo embeddedNameTree;
    if (const auto* names = ResolveDictionary(document, catalog ? catalog->Find(PdfName("Names")) : nullptr)) {
        CollectEmbeddedFileNameTree(
            document, ResolveDictionary(document, names->Find(PdfName("EmbeddedFiles"))), embeddedNameTree);
    }
    if (embeddedNameTree.malformed) {
        context.Error("PDFA-AF-010", "EmbeddedFiles name tree is malformed, unsorted, or contains duplicate names.",
                      "ISO 19005 associated files", {}, {}, "/Root/Names/EmbeddedFiles");
    }
    std::set<std::pair<std::uint32_t, std::uint16_t>> nameTreeReferences;
    for (const auto& [ignored, reference] : embeddedNameTree.entries) {
        (void)ignored;
        nameTreeReferences.insert(reference);
    }
    const auto associatedFileReferences = CollectAssociatedFileReferences(document);

    if (options.inspectFormAccessibility) {
        const auto* acroForm = ResolveDictionary(document, catalog ? catalog->Find(PdfName("AcroForm")) : nullptr);
        if (acroForm) {
            if (DictionaryBoolean(document, acroForm, "NeedAppearances")) {
                context.Error("PDFA-FORM-001", "PDF/A AcroForm dictionaries must not set /NeedAppearances true.",
                              "ISO 19005 interactive forms", {}, {}, "/Root/AcroForm/NeedAppearances");
            }
            if (acroForm->Find(PdfName("XFA"))) {
                context.Error("PDFA-FORM-002", "PDF/A forbids XFA form resources.",
                              "ISO 19005 interactive forms", {}, {}, "/Root/AcroForm/XFA");
            }
        }
    }

    bool hasAssociatedFile = false;
    std::size_t validAssociatedFileCount = 0U;
    for (const auto objectNumber : document.objectNumbers()) {
        if (context.Stop()) return;
        const auto xref = document.GetXrefEntry(objectNumber);
        if (!xref || !xref->inUse) continue;
        const PdfObject* object = nullptr;
        try {
            object = &document.GetObject(PdfReference{objectNumber, xref->generation});
        } catch (...) {
            continue;
        }
        const auto* dictionary = ResolveDictionary(document, object);
        if (!dictionary) continue;
        const std::string type = DictionaryStringOrName(document, dictionary, "Type");
        const std::string subtype = DictionaryStringOrName(document, dictionary, "Subtype");
        if (options.inspectAnnotationsAndActions && type == "Annot") {
            if (requirements.part == 1 && pdfA1ForbiddenAnnotations.contains(subtype)) {
                context.Error("PDFA-ANNOT-001", "PDF/A-1 forbids /" + subtype + " annotations.",
                              "ISO 19005-1 annotations", {}, objectNumber, "/Subtype");
            }
            const auto flags = DictionaryInteger(document, dictionary, "F").value_or(0);
            if ((flags & 4) == 0) {
                context.Error("PDFA-ANNOT-002", "PDF/A annotations must set the Print flag.",
                              "ISO 19005 annotations", {}, objectNumber, "/F");
            }
            if ((flags & (1 | 2 | 32)) != 0) {
                context.Error("PDFA-ANNOT-003", "PDF/A annotations must not be Invisible, Hidden, or NoView.",
                              "ISO 19005 annotations", {}, objectNumber, "/F");
            }
            if (subtype == "Widget" && !HasNormalAppearance(document, dictionary)) {
                context.Error("PDFA-ANNOT-004", "PDF/A widget annotations require a normal appearance stream.",
                              "ISO 19005 interactive forms", {}, objectNumber, "/AP/N");
            }
        }
        if (options.inspectAnnotationsAndActions) {
            const std::string action = DictionaryStringOrName(document, dictionary, "S");
            if (forbiddenActions.contains(action)) {
                context.Error("PDFA-ACTION-001", "PDF/A forbids the /" + action + " action.",
                              "ISO 19005 actions", {}, objectNumber, "/S");
            }
            if (dictionary->Find(PdfName("AA"))) {
                context.Error("PDFA-ACTION-002", "PDF/A forbids additional-actions dictionaries.",
                              "ISO 19005 actions", {}, objectNumber, "/AA");
            }
        }
        if (options.inspectEmbeddedFiles && type == "Filespec" && dictionary->Find(PdfName("EF"))) {
            hasAssociatedFile = true;
            bool fileValid = true;
            const auto fileReference = std::pair<std::uint32_t, std::uint16_t>{objectNumber, xref->generation};
            const std::string relationship = DictionaryStringOrName(document, dictionary, "AFRelationship");
            if (relationship.empty() || relationship == "Unspecified") {
                context.Error("PDFA-AF-002", "Associated files require a meaningful /AFRelationship.",
                              "ISO 19005 associated files", {}, objectNumber, "/AFRelationship");
                fileValid = false;
            }
            if (!nameTreeReferences.contains(fileReference)) {
                context.Error("PDFA-AF-003", "Embedded file specification is absent from the EmbeddedFiles name tree.",
                              "ISO 19005 associated files", {}, objectNumber, "/Root/Names/EmbeddedFiles");
                fileValid = false;
            }
            if (requirements.arbitraryEmbeddedFiles && !associatedFileReferences.contains(fileReference)) {
                context.Error("PDFA-AF-004", "Embedded file specification is not associated through an /AF array.",
                              "ISO 19005 associated files", {}, objectNumber, "/AF");
                fileValid = false;
            }
            const std::string fileName = DictionaryStringOrName(document, dictionary, "F");
            const std::string unicodeName = DictionaryStringOrName(document, dictionary, "UF");
            if (fileName.empty() || unicodeName.empty()) {
                context.Error("PDFA-AF-005", "Embedded file specifications require both /F and /UF file names.",
                              "ISO 19005 associated files", {}, objectNumber);
                fileValid = false;
            }
            const auto* ef = ResolveDictionary(document, dictionary->Find(PdfName("EF")));
            const PdfObject* streamObject = ef ? ef->Find(PdfName("UF")) : nullptr;
            if (!streamObject && ef) streamObject = ef->Find(PdfName("F"));
            const auto* resolvedStreamObject = ResolveObject(document, streamObject);
            const auto* stream = resolvedStreamObject ? resolvedStreamObject->AsStream() : nullptr;
            if (!stream) {
                context.Error("PDFA-AF-006", "Embedded file specification does not reference an embedded-file stream.",
                              "ISO 19005 associated files", {}, objectNumber, "/EF");
                fileValid = false;
            } else {
                if (DictionaryStringOrName(document, &stream->dictionary(), "Subtype").empty()) {
                    context.Error("PDFA-AF-007", "Embedded-file streams require a MIME /Subtype.",
                                  "ISO 19005 associated files", {}, objectNumber, "/EF/Subtype");
                    fileValid = false;
                }
                const auto* params = ResolveDictionary(document, stream->dictionary().Find(PdfName("Params")));
                if (!params) {
                    context.Error("PDFA-AF-008", "Embedded-file streams require a /Params dictionary.",
                                  "ISO 19005 associated files", {}, objectNumber, "/EF/Params");
                    fileValid = false;
                } else {
                    const std::string decoded = DecodeStream(document, resolvedStreamObject);
                    const auto size = DictionaryInteger(document, params, "Size");
                    if (!size || *size < 0 || static_cast<std::size_t>(*size) != decoded.size()) {
                        context.Error("PDFA-AF-009", "Embedded-file /Params /Size does not match decoded data.",
                                      "ISO 19005 associated files", {}, objectNumber, "/EF/Params/Size");
                        fileValid = false;
                    }
                    if (!EmbeddedFileChecksumMatches(document, resolvedStreamObject, params)) {
                        context.Error("PDFA-AF-011", "Embedded-file /CheckSum is absent or does not match its data.",
                                      "ISO 19005 associated files", {}, objectNumber, "/EF/Params/CheckSum");
                        fileValid = false;
                    }
                    if (DictionaryStringOrName(document, params, "ModDate").empty()) {
                        context.Error("PDFA-AF-012", "Embedded-file /Params requires /ModDate.",
                                      "ISO 19005 associated files", {}, objectNumber, "/EF/Params/ModDate");
                        fileValid = false;
                    }
                }
            }
            if (fileValid) ++validAssociatedFileCount;
        }
        if (!requirements.transparencyAllowed && type == "ExtGState") {
            const auto ca = ResolveObject(document, dictionary->Find(PdfName("ca")));
            const auto CA = ResolveObject(document, dictionary->Find(PdfName("CA")));
            const double fill = ca ? ca->AsReal().value_or(static_cast<double>(ca->AsInteger().value_or(1))) : 1.0;
            const double stroke = CA ? CA->AsReal().value_or(static_cast<double>(CA->AsInteger().value_or(1))) : 1.0;
            const std::string blend = DictionaryStringOrName(document, dictionary, "BM");
            if (fill != 1.0 || stroke != 1.0 || (!blend.empty() && blend != "Normal")) {
                context.Error("PDFA1-TRANSPARENCY-001", "PDF/A-1 forbids transparency and non-Normal blend modes.",
                              "ISO 19005-1 transparency", {}, objectNumber);
            }
        }
        if (!requirements.transparencyAllowed && subtype == "Image" && dictionary->Find(PdfName("SMask"))) {
            context.Error("PDFA1-TRANSPARENCY-002", "PDF/A-1 forbids image soft masks.",
                          "ISO 19005-1 transparency", {}, objectNumber, "/SMask");
        }
    }
    if (hasAssociatedFile && !requirements.arbitraryEmbeddedFiles) {
        context.Error("PDFA-AF-001", "This PDF/A profile does not permit arbitrary associated files.");
    }
    if (requirements.part == 4 && requirements.level == 'F' && validAssociatedFileCount == 0U) {
        context.Error("PDFA4F-AF-003", "PDF/A-4F requires at least one valid associated embedded file.");
    }
    if (requirements.engineeringVariant && !hasAssociatedFile) {
        context.Warning("PDFA4E-CONTENT-001", "PDF/A-4e normally carries engineering content or associated data.");
    }
}

void ValidateStructureTree(const PdfDocument& document, const PdfUaRequirements& requirements,
                           ValidationContext& context, const PdfValidationOptions& options) {
    const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    const auto* rootObject = catalog ? catalog->Find(PdfName("StructTreeRoot")) : nullptr;
    const auto rootReference = rootObject ? rootObject->AsReference() : std::nullopt;
    const auto* root = ResolveDictionary(document, rootObject);
    if (!root) return;
    const auto* parentTree = ResolveDictionary(document, root->Find(PdfName("ParentTree")));
    if (!parentTree) context.Error("PDFUA-PARENTTREE-001", "PDF/UA requires a ParentTree.");

    std::set<std::int64_t> numberTreeKeys;
    std::int64_t maximumNumberTreeKey = -1;
    std::function<void(const PdfDictionary*, std::size_t)> collectNumberTree;
    collectNumberTree = [&](const PdfDictionary* tree, const std::size_t depth) {
        if (!tree || depth > 64U) {
            if (depth > 64U) {
                context.Error("PDFUA-PARENTTREE-010", "ParentTree exceeds the supported nesting depth.");
            }
            return;
        }
        if (const auto* numbers = ResolveArray(document, tree->Find(PdfName("Nums")))) {
            if ((numbers->size() % 2U) != 0U) {
                context.Error("PDFUA-PARENTTREE-011", "ParentTree /Nums must contain key-value pairs.");
            }
            std::optional<std::int64_t> previous;
            for (std::size_t index = 0U; index + 1U < numbers->size(); index += 2U) {
                const auto* keyObject = ResolveObject(document, &numbers->at(index));
                const auto key = keyObject ? keyObject->AsInteger() : std::nullopt;
                if (!key || *key < 0) {
                    context.Error("PDFUA-PARENTTREE-012", "ParentTree keys must be non-negative integers.");
                    continue;
                }
                if (previous && *key <= *previous) {
                    context.Error("PDFUA-PARENTTREE-013", "ParentTree /Nums keys must be strictly increasing.");
                }
                previous = *key;
                if (!numberTreeKeys.insert(*key).second) {
                    context.Error("PDFUA-PARENTTREE-014", "ParentTree contains a duplicate key.");
                }
                maximumNumberTreeKey = std::max(maximumNumberTreeKey, *key);
            }
        }
        if (const auto* kids = ResolveArray(document, tree->Find(PdfName("Kids")))) {
            for (const auto& kid : kids->values()) {
                collectNumberTree(ResolveDictionary(document, &kid), depth + 1U);
            }
        }
    };
    collectNumberTree(parentTree, 0U);

    const auto nextKey = DictionaryInteger(document, root, "ParentTreeNextKey");
    if (!nextKey) {
        context.Error("PDFUA-PARENTTREE-002", "Structure tree is missing /ParentTreeNextKey.");
    } else if (*nextKey < 0 || *nextKey <= maximumNumberTreeKey) {
        context.Error("PDFUA-PARENTTREE-015",
                      "/ParentTreeNextKey must be greater than every key in the ParentTree.");
    }

    std::set<std::pair<std::uint32_t, std::uint16_t>> standardNamespaceReferences;
    if (requirements.part == 2) {
        bool foundNamespace = false;
        if (const auto* namespaces = ResolveArray(document, root->Find(PdfName("Namespaces")))) {
            for (const auto& item : namespaces->values()) {
                const auto* dictionary = ResolveDictionary(document, &item);
                if (!dictionary) continue;
                const bool standard = DictionaryStringOrName(document, dictionary, "Type") == "Namespace" &&
                    DictionaryStringOrName(document, dictionary, "NS") == "http://iso.org/pdf2/ssn";
                if (!standard) continue;
                foundNamespace = true;
                if (const auto reference = item.AsReference()) standardNamespaceReferences.insert(*reference);
            }
        }
        if (!foundNamespace) {
            context.Error("PDFUA2-NS-001", "PDF/UA-2 requires the PDF 2.0 standard structure namespace.");
        }
    }

    const auto* roleMap = ResolveDictionary(document, root->Find(PdfName("RoleMap")));
    const auto resolveRole = [&](std::string role, const std::optional<std::uint32_t> objectNumber) {
        std::unordered_set<std::string> visited;
        for (std::size_t depth = 0U; depth < 32U; ++depth) {
            if (IsKnownRole(role, requirements.part)) return role;
            if (!visited.insert(role).second) {
                context.Error("PDFUA-ROLE-002", "RoleMap contains a cycle involving /" + role + '.',
                              {}, {}, objectNumber, "/RoleMap");
                return role;
            }
            const auto* mapped = roleMap ? ResolveObject(document, roleMap->Find(PdfName(role))) : nullptr;
            const auto* mappedName = mapped ? mapped->AsName() : nullptr;
            if (!mappedName) {
                context.Error("PDFUA-ROLE-001", "Custom role /" + role + " is not role-mapped.",
                              {}, {}, objectNumber, "/S");
                return role;
            }
            role = mappedName->value();
        }
        context.Error("PDFUA-ROLE-003", "RoleMap chain is too deep.", {}, {}, objectNumber, "/RoleMap");
        return role;
    };

    if (roleMap) {
        for (const auto& [customRole, ignored] : roleMap->values()) {
            (void)ignored;
            (void)resolveRole(customRole.value(), {});
        }
    }

    struct StructElementRecord final {
        struct OrderedKid final {
            enum class Kind { Mcid, Child };
            Kind kind{Kind::Mcid};
            std::int64_t value{};
        };
        std::uint32_t objectNumber{};
        const PdfDictionary* dictionary{};
        std::string rawRole;
        std::string role;
        std::optional<std::uint32_t> parentObject;
        std::vector<std::uint32_t> childObjects;
        std::vector<std::int64_t> mcids;
        std::vector<OrderedKid> orderedKids;
        std::optional<std::pair<std::uint32_t, std::uint16_t>> pageReference;
    };

    std::vector<StructElementRecord> elements;
    std::map<std::uint32_t, std::size_t> elementIndex;
    std::map<std::string, std::string> identifiers;
    for (const auto objectNumber : document.objectNumbers()) {
        const auto xref = document.GetXrefEntry(objectNumber);
        if (!xref || !xref->inUse) continue;
        const PdfObject* object = nullptr;
        try { object = &document.GetObject(PdfReference{objectNumber, xref->generation}); }
        catch (...) { continue; }
        const auto* dictionary = ResolveDictionary(document, object);
        if (!dictionary || DictionaryStringOrName(document, dictionary, "Type") != "StructElem") continue;

        StructElementRecord record;
        record.objectNumber = objectNumber;
        record.dictionary = dictionary;
        record.rawRole = DictionaryStringOrName(document, dictionary, "S");
        if (record.rawRole.empty()) {
            context.Error("PDFUA-STRUCT-001", "Structure element is missing /S.", {}, {}, objectNumber);
        }
        record.role = resolveRole(record.rawRole, objectNumber);
        const auto* parentObject = dictionary->Find(PdfName("P"));
        if (!parentObject) {
            context.Error("PDFUA-STRUCT-002", "Structure element is missing /P.", {}, {}, objectNumber);
        } else if (const auto parentReference = parentObject->AsReference()) {
            record.parentObject = parentReference->first;
        }
        if (const auto* pageObject = dictionary->Find(PdfName("Pg"))) {
            record.pageReference = pageObject->AsReference();
        }

        if (requirements.part == 2) {
            const auto* namespaceObject = dictionary->Find(PdfName("NS"));
            std::optional<std::pair<std::uint32_t, std::uint16_t>> namespaceReference;
            if (namespaceObject) namespaceReference = namespaceObject->AsReference();
            const auto* namespaceDictionary = ResolveDictionary(document, namespaceObject);
            const bool standard = namespaceDictionary &&
                DictionaryStringOrName(document, namespaceDictionary, "Type") == "Namespace" &&
                DictionaryStringOrName(document, namespaceDictionary, "NS") == "http://iso.org/pdf2/ssn";
            bool registeredNamespace = true;
            if (namespaceReference.has_value()) {
                registeredNamespace = standardNamespaceReferences.contains(namespaceReference.value());
            }
            if (!standard || !registeredNamespace) {
                context.Error("PDFUA2-NS-003",
                              "PDF/UA-2 structure elements must use the standard PDF 2.0 namespace.",
                              {}, {}, objectNumber, "/NS");
            }
        }

        const std::string language = DictionaryStringOrName(document, dictionary, "Lang");
        if (!language.empty() && !IsValidLanguageTag(language)) {
            context.Error("PDFUA-LANG-005", "Structure element /Lang is not a valid BCP 47-style tag.",
                          {}, {}, objectNumber, "/Lang");
        }
        if ((record.role == "Figure" || record.role == "Formula") &&
            DictionaryStringOrName(document, dictionary, "Alt").empty() &&
            DictionaryStringOrName(document, dictionary, "ActualText").empty()) {
            context.Error("PDFUA-ALT-001", "/" + record.role + " requires /Alt or /ActualText.",
                          {}, {}, objectNumber);
        }

        const std::string identifier = DictionaryStringOrName(document, dictionary, "ID");
        if (!identifier.empty()) {
            const auto [position, inserted] = identifiers.emplace(identifier, record.role);
            if (!inserted) {
                context.Error("PDFUA-ID-001", "Structure element IDs must be unique.",
                              {}, {}, objectNumber, "/ID");
            }
            (void)position;
        }

        const auto consumeKid = [&](const PdfObject* kid) {
            if (!kid) return;
            if (const auto integer = kid->AsInteger()) {
                record.mcids.push_back(*integer);
                record.orderedKids.push_back({StructElementRecord::OrderedKid::Kind::Mcid, *integer});
                return;
            }
            if (const auto reference = kid->AsReference()) {
                const auto* childDictionary = ResolveDictionary(document, kid);
                if (childDictionary && DictionaryStringOrName(document, childDictionary, "Type") == "StructElem") {
                    record.childObjects.push_back(reference->first);
                    record.orderedKids.push_back({StructElementRecord::OrderedKid::Kind::Child,
                                                  static_cast<std::int64_t>(reference->first)});
                    return;
                }
            }
            const auto* kidDictionary = ResolveDictionary(document, kid);
            if (kidDictionary && DictionaryStringOrName(document, kidDictionary, "Type") == "MCR") {
                if (const auto mcid = DictionaryInteger(document, kidDictionary, "MCID")) {
                    record.mcids.push_back(*mcid);
                    record.orderedKids.push_back({StructElementRecord::OrderedKid::Kind::Mcid, *mcid});
                }
            }
        };
        const auto* kidsObject = ResolveObject(document, dictionary->Find(PdfName("K")));
        if (const auto* kids = kidsObject ? kidsObject->AsArray() : nullptr) {
            for (const auto& kid : kids->values()) consumeKid(&kid);
        } else {
            consumeKid(dictionary->Find(PdfName("K")));
        }

        elementIndex.emplace(objectNumber, elements.size());
        elements.push_back(std::move(record));
    }

    const auto allowedChild = [](const std::string_view parent, const std::string_view child) {
        if (parent == "L") return child == "LI";
        if (parent == "LI") return child == "Lbl" || child == "LBody";
        if (parent == "Table") {
            return child == "Caption" || child == "TR" || child == "THead" ||
                   child == "TBody" || child == "TFoot";
        }
        if (parent == "THead" || parent == "TBody" || parent == "TFoot") return child == "TR";
        if (parent == "TR") return child == "TH" || child == "TD";
        if (parent == "TOC") return child == "TOCI";
        if (parent == "Ruby") return child == "RB" || child == "RT" || child == "RP";
        if (parent == "Warichu") return child == "WT" || child == "WP";
        return true;
    };

    std::size_t topLevelDocuments = 0U;
    std::vector<std::uint32_t> rootElementObjects;
    const auto inspectRootKid = [&](const PdfObject* kid) {
        const auto reference = kid ? kid->AsReference() : std::nullopt;
        if (!reference) return;
        const auto found = elementIndex.find(reference->first);
        if (found == elementIndex.end()) return;
        rootElementObjects.push_back(reference->first);
        const auto& child = elements[found->second];
        if (child.role == "Document" || (requirements.part == 2 && child.role == "DocumentFragment")) {
            ++topLevelDocuments;
        }
    };
    const auto* rootKidsObject = ResolveObject(document, root->Find(PdfName("K")));
    if (const auto* rootKids = rootKidsObject ? rootKidsObject->AsArray() : nullptr) {
        for (const auto& kid : rootKids->values()) inspectRootKid(&kid);
    } else {
        inspectRootKid(root->Find(PdfName("K")));
    }
    if (topLevelDocuments == 0U) {
        context.Error("PDFUA-STRUCT-003",
                      "The structure tree root must contain a /Document or permitted /DocumentFragment element.");
    }

    std::map<std::pair<std::uint32_t, std::uint16_t>, std::map<std::int64_t, std::size_t>> structuralMcids;
    for (const auto& element : elements) {
        if (element.pageReference) {
            auto& counts = structuralMcids[*element.pageReference];
            for (const auto mcid : element.mcids) {
                if (mcid < 0) {
                    context.Error("PDFUA-MCID-002", "Structure MCIDs must be non-negative integers.",
                                  {}, {}, element.objectNumber, "/K");
                } else {
                    ++counts[mcid];
                }
            }
        } else if (!element.mcids.empty()) {
            context.Error("PDFUA-MCID-003", "A structure element containing an MCID requires /Pg.",
                          {}, {}, element.objectNumber, "/Pg");
        }

        std::string parentRole = "StructTreeRoot";
        if (element.parentObject) {
            const auto parent = elementIndex.find(*element.parentObject);
            if (parent != elementIndex.end()) parentRole = elements[parent->second].role;
            else if (!rootReference || rootReference->first != *element.parentObject) {
                context.Error("PDFUA-STRUCT-005", "Structure element /P does not reference a structure parent.",
                              {}, {}, element.objectNumber, "/P");
            }
        }

        std::unordered_map<std::string, std::size_t> childCounts;
        for (const auto childObject : element.childObjects) {
            const auto child = elementIndex.find(childObject);
            if (child == elementIndex.end()) {
                context.Error("PDFUA-STRUCT-006", "Structure element references an invalid child.",
                              {}, {}, element.objectNumber, "/K");
                continue;
            }
            const auto& childElement = elements[child->second];
            if (!childElement.parentObject || *childElement.parentObject != element.objectNumber) {
                context.Error("PDFUA-STRUCT-004", "Child structure element /P does not point back to its parent.",
                              {}, {}, childElement.objectNumber, "/P");
            }
            ++childCounts[childElement.role];
            if (options.inspectSemanticStructure && !allowedChild(element.role, childElement.role)) {
                context.Error("PDFUA-SEMANTIC-001", "/" + childElement.role +
                              " is not permitted beneath /" + element.role + '.',
                              "PDF/UA semantic structure", {}, childElement.objectNumber, "/P");
            }
        }

        if (options.inspectSemanticStructure) {
            if ((element.role == "LI" && parentRole != "L") ||
                ((element.role == "Lbl" || element.role == "LBody") && parentRole != "LI") ||
                ((element.role == "THead" || element.role == "TBody" || element.role == "TFoot") && parentRole != "Table") ||
                (element.role == "TR" && parentRole != "Table" && parentRole != "THead" &&
                 parentRole != "TBody" && parentRole != "TFoot") ||
                ((element.role == "TH" || element.role == "TD") && parentRole != "TR") ||
                (element.role == "TOCI" && parentRole != "TOC") ||
                ((element.role == "RB" || element.role == "RT" || element.role == "RP") && parentRole != "Ruby") ||
                ((element.role == "WT" || element.role == "WP") && parentRole != "Warichu")) {
                context.Error("PDFUA-SEMANTIC-002", "/" + element.role +
                              " is not permitted beneath /" + parentRole + '.',
                              "PDF/UA semantic structure", {}, element.objectNumber, "/P");
            }
            if (element.role == "L" && childCounts["LI"] == 0U) {
                context.Error("PDFUA-LIST-001", "List elements require at least one /LI child.",
                              {}, {}, element.objectNumber, "/K");
            }
            if (element.role == "LI" && (childCounts["LBody"] != 1U || childCounts["Lbl"] > 1U)) {
                context.Error("PDFUA-LIST-002", "/LI requires exactly one /LBody and no more than one /Lbl.",
                              {}, {}, element.objectNumber, "/K");
            }
            if (element.role == "Table" && element.childObjects.empty()) {
                context.Error("PDFUA-TABLE-002", "Table elements require rows or table row groups.",
                              {}, {}, element.objectNumber, "/K");
            }
            if (element.role == "TR" && childCounts["TH"] + childCounts["TD"] == 0U) {
                context.Error("PDFUA-TABLE-003", "Table rows require at least one /TH or /TD child.",
                              {}, {}, element.objectNumber, "/K");
            }
            if (element.role == "Ruby" && (childCounts["RB"] == 0U || childCounts["RT"] == 0U)) {
                context.Error("PDFUA-RUBY-001", "Ruby elements require /RB and /RT children.",
                              {}, {}, element.objectNumber, "/K");
            }
            if (element.role == "Warichu" && (childCounts["WT"] == 0U || childCounts["WP"] == 0U)) {
                context.Error("PDFUA-WARICHU-001", "Warichu elements require /WT and /WP children.",
                              {}, {}, element.objectNumber, "/K");
            }
        }

        bool tableAssociation = false;
        const auto inspectAttribute = [&](const PdfDictionary* attribute) {
            if (!attribute || DictionaryStringOrName(document, attribute, "O") != "Table") return;
            if (const auto rowSpan = DictionaryInteger(document, attribute, "RowSpan"); rowSpan && *rowSpan <= 0) {
                context.Error("PDFUA-TABLE-004", "/RowSpan must be a positive integer.",
                              {}, {}, element.objectNumber, "/A/RowSpan");
            }
            if (const auto columnSpan = DictionaryInteger(document, attribute, "ColSpan"); columnSpan && *columnSpan <= 0) {
                context.Error("PDFUA-TABLE-005", "/ColSpan must be a positive integer.",
                              {}, {}, element.objectNumber, "/A/ColSpan");
            }
            const std::string scope = DictionaryStringOrName(document, attribute, "Scope");
            if (!scope.empty() && scope != "Row" && scope != "Column" && scope != "Both") {
                context.Error("PDFUA-TABLE-006", "Table /Scope must be /Row, /Column, or /Both.",
                              {}, {}, element.objectNumber, "/A/Scope");
            }
            const auto headers = DictionaryStrings(document, attribute, "Headers");
            if (!scope.empty() || !headers.empty()) tableAssociation = true;
            for (const auto& header : headers) {
                const auto target = identifiers.find(header);
                if (target == identifiers.end() || target->second != "TH") {
                    context.Error("PDFUA-TABLE-007",
                                  "A /Headers entry must reference the ID of a /TH structure element.",
                                  {}, {}, element.objectNumber, "/A/Headers");
                }
            }
        };
        const auto* attributes = ResolveObject(document, element.dictionary->Find(PdfName("A")));
        if (const auto* array = attributes ? attributes->AsArray() : nullptr) {
            for (const auto& item : array->values()) inspectAttribute(ResolveDictionary(document, &item));
        } else {
            inspectAttribute(ResolveDictionary(document, attributes));
        }
        if (element.role == "TH" && !tableAssociation) {
            context.Error("PDFUA-TABLE-001", "Table header cells require /Scope or /Headers.",
                          {}, {}, element.objectNumber, "/A");
        }
    }

    std::map<std::pair<std::uint32_t, std::uint16_t>, std::vector<std::uint32_t>> logicalMcidOrder;
    std::vector<std::pair<int, std::uint32_t>> numberedHeadings;
    bool hasGenericHeading = false;
    std::set<std::uint32_t> recursionStack;
    std::set<std::uint32_t> traversedElements;
    std::function<void(std::uint32_t)> traverseStructure;
    traverseStructure = [&](const std::uint32_t objectNumber) {
        const auto found = elementIndex.find(objectNumber);
        if (found == elementIndex.end()) return;
        if (!recursionStack.insert(objectNumber).second) {
            context.Error("PDFUA-STRUCT-007", "Structure tree contains a cycle.",
                          "PDF/UA logical structure", {}, objectNumber, "/K");
            return;
        }
        if (!traversedElements.insert(objectNumber).second) {
            context.Error("PDFUA-STRUCT-008", "A structure element is referenced more than once in the logical tree.",
                          "PDF/UA logical structure", {}, objectNumber, "/K");
            recursionStack.erase(objectNumber);
            return;
        }
        const auto& element = elements[found->second];
        if (options.inspectHeadingHierarchy) {
            if (element.role == "H") hasGenericHeading = true;
            else if (element.role.size() == 2U && element.role.front() == 'H' &&
                     element.role[1] >= '1' && element.role[1] <= '6') {
                numberedHeadings.emplace_back(element.role[1] - '0', objectNumber);
            }
        }
        for (const auto& kid : element.orderedKids) {
            if (kid.kind == StructElementRecord::OrderedKid::Kind::Mcid) {
                if (element.pageReference && kid.value >= 0 &&
                    kid.value <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                    logicalMcidOrder[*element.pageReference].push_back(static_cast<std::uint32_t>(kid.value));
                }
            } else if (kid.value >= 0 &&
                       kid.value <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                traverseStructure(static_cast<std::uint32_t>(kid.value));
            }
        }
        recursionStack.erase(objectNumber);
    };
    for (const auto objectNumber : rootElementObjects) traverseStructure(objectNumber);

    if (options.inspectHeadingHierarchy) {
        if (hasGenericHeading && !numberedHeadings.empty()) {
            context.Warning("PDFUA-HEADING-001",
                            "The document mixes generic /H headings with numbered /H1-/H6 headings.",
                            "PDF/UA heading hierarchy");
        }
        int previousLevel = 0;
        for (const auto& [level, objectNumber] : numberedHeadings) {
            if (previousLevel == 0 && level > 1) {
                context.Warning("PDFUA-HEADING-002", "The first numbered heading begins below /H1.",
                                "PDF/UA heading hierarchy", {}, objectNumber, "/S");
            } else if (previousLevel != 0 && level > previousLevel + 1) {
                context.Warning("PDFUA-HEADING-003", "Numbered heading levels skip from /H" +
                                std::to_string(previousLevel) + " to /H" + std::to_string(level) + '.',
                                "PDF/UA heading hierarchy", {}, objectNumber, "/S");
            }
            previousLevel = level;
        }
    }

    std::set<std::int64_t> seenParentTreeKeys;
    for (std::size_t pageIndex = 0U; pageIndex < document.GetPageCount(); ++pageIndex) {
        const auto pageReference = document.GetPageReference(pageIndex);
        const auto pageReferencePair = std::pair<std::uint32_t, std::uint16_t>{
            pageReference.objectNumber, pageReference.generation};
        const auto* page = document.GetObject(pageReference).AsDictionary();
        if (!page) continue;
        const auto structParents = DictionaryInteger(document, page, "StructParents");
        if (!structParents) {
            context.Error("PDFUA-PAGE-001", "Tagged pages require /StructParents.", {}, pageIndex);
        } else {
            if (!seenParentTreeKeys.insert(*structParents).second) {
                context.Error("PDFUA-PARENTTREE-005", "StructParent keys must be unique across pages and annotations.",
                              {}, pageIndex, {}, "/StructParents");
            }
            if (!numberTreeKeys.contains(*structParents)) {
                context.Error("PDFUA-PARENTTREE-016", "Page /StructParents key is absent from the ParentTree.",
                              {}, pageIndex, {}, "/StructParents");
            }
        }
        if (DictionaryStringOrName(document, page, "Tabs") != "S") {
            context.Error("PDFUA-PAGE-002", "PDF/UA pages require /Tabs /S.", {}, pageIndex);
        }
        if (options.inspectTaggedContent) {
            const auto inspection = InspectPageTags(document, pageIndex);
            if (inspection.unbalanced) {
                context.Error("PDFUA-MCID-001", "Marked-content sequences or MCIDs are invalid.", {}, pageIndex);
            }
            if (inspection.untaggedPainting) {
                context.Error("PDFUA-CONTENT-001", "Real page content is neither tagged nor an Artifact.", {}, pageIndex);
            }
            if (structParents) {
                const auto* entries = ResolveArray(document, FindNumberTreeValue(document, parentTree, *structParents));
                if (!inspection.mcids.empty() && !entries) {
                    context.Error("PDFUA-PARENTTREE-003", "ParentTree has no MCID array for the page.", {}, pageIndex);
                } else if (entries && !inspection.mcids.empty() && entries->size() <= *inspection.mcids.rbegin()) {
                    context.Error("PDFUA-PARENTTREE-004", "ParentTree array does not cover every page MCID.", {}, pageIndex);
                } else if (entries) {
                    for (const auto mcid : inspection.mcids) {
                        if (mcid >= entries->size()) continue;
                        const auto* element = ResolveDictionary(document, &entries->at(mcid));
                        if (!element || DictionaryStringOrName(document, element, "Type") != "StructElem") {
                            context.Error("PDFUA-PARENTTREE-006", "Each page MCID must map to a structure element.",
                                          {}, pageIndex, {}, "/ParentTree");
                            continue;
                        }
                        const auto* pg = element->Find(PdfName("Pg"));
                        const auto pageTarget = pg ? pg->AsReference() : std::nullopt;
                        if (!pageTarget || *pageTarget != pageReferencePair) {
                            context.Error("PDFUA-PARENTTREE-007", "MCID structure element /Pg does not match its page.",
                                          {}, pageIndex, {}, "/ParentTree");
                        }
                    }
                }
            }
            const auto structural = structuralMcids.find(pageReferencePair);
            if (structural != structuralMcids.end()) {
                for (const auto& [mcid, count] : structural->second) {
                    if (count != 1U) {
                        context.Error("PDFUA-MCID-006", "Each MCID must occur exactly once in the structure tree.",
                                      {}, pageIndex, {}, "/StructTreeRoot");
                    }
                    if (mcid < 0 || !inspection.mcids.contains(static_cast<std::uint32_t>(mcid))) {
                        context.Error("PDFUA-MCID-004", "Structure tree references an MCID absent from page content.",
                                      {}, pageIndex, {}, "/StructTreeRoot");
                    }
                }
                for (const auto mcid : inspection.mcids) {
                    if (!structural->second.contains(static_cast<std::int64_t>(mcid))) {
                        context.Error("PDFUA-MCID-005", "Page content MCID is absent from the structure tree.",
                                      {}, pageIndex, {}, "/StructTreeRoot");
                    }
                }
            } else if (!inspection.mcids.empty()) {
                context.Error("PDFUA-MCID-005", "Page content MCIDs are absent from the structure tree.",
                              {}, pageIndex, {}, "/StructTreeRoot");
            }
            if (options.inspectReadingOrder) {
                const auto logical = logicalMcidOrder.find(pageReferencePair);
                if (logical != logicalMcidOrder.end() &&
                    logical->second.size() == inspection.mcidOrder.size() &&
                    std::set<std::uint32_t>(logical->second.begin(), logical->second.end()) == inspection.mcids &&
                    logical->second != inspection.mcidOrder) {
                    context.Warning("PDFUA-ORDER-001",
                                    "Physical marked-content order differs from logical structure-tree order.",
                                    "PDF/UA reading order", pageIndex, {}, "/StructTreeRoot");
                }
            }
        }
        if (options.inspectAnnotationsAndActions) {
            if (const auto* annotations = ResolveArray(document, page->Find(PdfName("Annots")))) {
                for (const auto& annotationObject : annotations->values()) {
                    const auto* annotation = ResolveDictionary(document, &annotationObject);
                    if (!annotation) continue;
                    const std::string subtype = DictionaryStringOrName(document, annotation, "Subtype");
                    if (subtype == "Popup") continue;
                    const auto flags = DictionaryInteger(document, annotation, "F").value_or(0);
                    if ((flags & (1 | 2 | 32)) != 0) {
                        context.Error("PDFUA-ANNOT-004",
                                      "Accessible annotations must not be Invisible, Hidden, or NoView.",
                                      {}, pageIndex, {}, "/Annots/F");
                    }
                    const auto structParent = DictionaryInteger(document, annotation, "StructParent");
                    if (!structParent) {
                        context.Error("PDFUA-ANNOT-001", "PDF/UA annotations require /StructParent and OBJR.", {}, pageIndex);
                    } else {
                        if (!seenParentTreeKeys.insert(*structParent).second) {
                            context.Error("PDFUA-PARENTTREE-005", "StructParent keys must be unique across pages and annotations.",
                                          {}, pageIndex, {}, "/StructParent");
                        }
                        if (!numberTreeKeys.contains(*structParent)) {
                            context.Error("PDFUA-PARENTTREE-017", "Annotation /StructParent key is absent from the ParentTree.",
                                          {}, pageIndex, {}, "/StructParent");
                        }
                        const auto* treeValue = FindNumberTreeValue(document, parentTree, *structParent);
                        const auto* structure = ResolveDictionary(document, treeValue);
                        if (!ObjrMatchesAnnotation(document, structure, annotationObject.AsReference())) {
                            context.Error("PDFUA-ANNOT-003", "Annotation ParentTree entry does not contain a matching OBJR.", {}, pageIndex);
                        }
                    }
                    const std::string contents = DictionaryStringOrName(document, annotation, "Contents");
                    if (contents.empty() && subtype != "Widget") {
                        context.Error("PDFUA-ANNOT-002", "Annotation requires an accessible /Contents description.", {}, pageIndex);
                    }
                    if (subtype == "Link" && !annotation->Find(PdfName("A")) && !annotation->Find(PdfName("Dest"))) {
                        context.Error("PDFUA-LINK-001", "Link annotations require an action or destination.",
                                      {}, pageIndex, {}, "/Annots");
                    }
                    if (subtype == "Widget" && options.inspectFormAccessibility) {
                        if (InheritedFieldStringOrName(document, annotation, "FT").empty()) {
                            context.Error("PDFUA-FORM-001", "Widget annotations require an inherited field type.",
                                          {}, pageIndex, {}, "/Annots/FT");
                        }
                        if (InheritedFieldStringOrName(document, annotation, "T").empty()) {
                            context.Error("PDFUA-FORM-002", "Form fields require a non-empty field name.",
                                          {}, pageIndex, {}, "/Annots/T");
                        }
                        if (InheritedFieldStringOrName(document, annotation, "TU").empty()) {
                            context.Error("PDFUA-FORM-003", "Form fields require an accessible alternate name (/TU).",
                                          {}, pageIndex, {}, "/Annots/TU");
                        }
                        if (!HasNormalAppearance(document, annotation)) {
                            context.Error("PDFUA-FORM-004", "Widget annotations require a normal appearance (/AP /N).",
                                          {}, pageIndex, {}, "/Annots/AP/N");
                        }
                    }
                }
            }
        }
    }
}

} // namespace

bool PdfValidationResult::IsValid() const noexcept { return ErrorCount() == 0U; }
std::size_t PdfValidationResult::ErrorCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
        [](const auto& issue) { return issue.IsError(); }));
}
std::size_t PdfValidationResult::WarningCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const auto& issue) {
        return !issue.IsError() && issue.GetSeverity() == PdfValidationSeverity::Warning;
    }));
}
std::size_t PdfValidationResult::InfoCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const auto& issue) {
        return !issue.IsError() && issue.GetSeverity() == PdfValidationSeverity::Info;
    }));
}

std::string PdfValidationResult::ToText() const {
    std::ostringstream output;
    output << ProfileName(profile) << ": " << (IsValid() ? "PASS" : "FAIL")
           << " (errors=" << ErrorCount() << ", warnings=" << WarningCount()
           << ", info=" << InfoCount() << ")\n";
    for (const auto& issue : issues) {
        const auto severity = issue.GetSeverity();
        output << (severity == PdfValidationSeverity::Error ? "ERROR" :
                   severity == PdfValidationSeverity::Warning ? "WARN" : "INFO")
               << " [" << issue.code << "] ";
        if (!issue.clause.empty()) output << issue.clause << ": ";
        output << issue.message;
        if (issue.pageIndex) output << " (page " << (*issue.pageIndex + 1U) << ')';
        if (issue.objectNumber) output << " (object " << *issue.objectNumber << ')';
        if (!issue.objectPath.empty()) output << " at " << issue.objectPath;
        output << '\n';
    }
    return output.str();
}

std::string PdfValidationResult::ToJson() const {
    std::ostringstream output;
    output << "{\"profile\":\"" << JsonEscape(ProfileName(profile)) << "\","
           << "\"valid\":" << (IsValid() ? "true" : "false") << ','
           << "\"errors\":" << ErrorCount() << ','
           << "\"warnings\":" << WarningCount() << ','
           << "\"info\":" << InfoCount() << ",\"issues\":[";
    for (std::size_t index = 0U; index < issues.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& issue = issues[index];
        const auto severity = issue.GetSeverity();
        output << "{\"severity\":\""
               << (severity == PdfValidationSeverity::Error ? "error" :
                   severity == PdfValidationSeverity::Warning ? "warning" : "info")
               << "\",\"code\":\"" << JsonEscape(issue.code)
               << "\",\"message\":\"" << JsonEscape(issue.message) << '"';
        if (!issue.clause.empty()) output << ",\"clause\":\"" << JsonEscape(issue.clause) << '"';
        if (issue.pageIndex) output << ",\"pageIndex\":" << *issue.pageIndex;
        if (issue.objectNumber) output << ",\"objectNumber\":" << *issue.objectNumber;
        if (!issue.objectPath.empty()) output << ",\"path\":\"" << JsonEscape(issue.objectPath) << '"';
        output << '}';
    }
    output << "]}";
    return output.str();
}

PdfValidationResult PdfConformanceValidator::Validate(
    const PdfDocument& document, const PdfConformanceProfile profile,
    const PdfValidationOptions& options) {
    PdfValidationResult result;
    result.profile = profile;
    ValidationContext context(result, options);
    if (document.GetPageCount() == 0U) context.Error("PDF-PAGE-001", "Document must contain at least one page.");

    const auto pdfA = PdfARequirementsFor(profile);
    const auto pdfUa = PdfUaRequirementsFor(profile);
    if (pdfA) {
        if (document.IsEncrypted()) context.Error("PDFA-ENCRYPT-001", "PDF/A forbids document encryption.");
        if (!VersionMatches(document, pdfA->version)) {
            context.Error("PDFA-VERSION-001", ProfileName(profile) + " requires PDF " + pdfA->version + '.');
        }
        const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
        if (!catalog) context.Error("PDFA-CATALOG-001", "Catalog object is not a dictionary.");
        else {
            if (!HasValidOutputIntent(document, context)) {
                context.Error("PDFA-OUTPUT-001", "PDF/A requires a /GTS_PDFA1 output intent.");
            }
            if (catalog->Find(PdfName("Collection"))) context.Error("PDFA-COLLECTION-001", "PDF/A forbids portfolio collection mode.");
            if (pdfA->tagged) {
                if (!catalog->Find(PdfName("StructTreeRoot"))) context.Error("PDFA-TAGGED-001", "PDF/A level A requires a structure tree.");
                if (DictionaryStringOrName(document, catalog, "Lang").empty()) context.Error("PDFA-LANG-001", "PDF/A level A requires a language.");
            }
        }
        if (options.inspectMetadata) ValidateMetadata(document, pdfA, {}, context);
        if (options.inspectFonts) ValidateFonts(document, pdfA->unicode || pdfA->tagged, context);
        ValidatePdfAObjects(document, *pdfA, context, options);
        if (pdfA->tagged && options.inspectStructureTree) {
            const PdfUaRequirements shared{1, 0, pdfA->version};
            ValidateStructureTree(document, shared, context, options);
        }
    }

    if (pdfUa) {
        if (!VersionMatches(document, pdfUa->version)) {
            context.Error("PDFUA-VERSION-001", ProfileName(profile) + " requires PDF " + pdfUa->version + '.');
        }
        const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
        if (!catalog) context.Error("PDFUA-CATALOG-001", "Catalog object is not a dictionary.");
        else {
            if (!catalog->Find(PdfName("StructTreeRoot"))) context.Error("PDFUA-TAG-001", "PDF/UA requires a structure tree.");
            const auto* markInfo = ResolveDictionary(document, catalog->Find(PdfName("MarkInfo")));
            if (!DictionaryBoolean(document, markInfo, "Marked")) context.Error("PDFUA-MARKED-001", "PDF/UA requires /MarkInfo /Marked true.");
            if (DictionaryStringOrName(document, catalog, "Lang").empty()) context.Error("PDFUA-LANG-001", "PDF/UA requires /Lang.");
            const auto* preferences = ResolveDictionary(document, catalog->Find(PdfName("ViewerPreferences")));
            if (!DictionaryBoolean(document, preferences, "DisplayDocTitle")) {
                context.Error("PDFUA-TITLE-003", "PDF/UA requires /DisplayDocTitle true.");
            }
        }
        if (options.inspectMetadata) ValidateMetadata(document, {}, pdfUa, context);
        if (options.inspectFonts) ValidateFonts(document, true, context);
        if (options.inspectStructureTree) ValidateStructureTree(document, *pdfUa, context, options);
    }

    context.Info("VALIDATION-PROFILE", "Validated using the Pdf++ rule-based conformance profile.");
    return result;
}

PdfValidationResult PdfConformanceValidator::ValidateFile(
    const std::filesystem::path& path, const PdfConformanceProfile profile,
    const PdfValidationOptions& options) {
    return Validate(PdfDocument::Open(path), profile, options);
}

} // namespace CPPPdf

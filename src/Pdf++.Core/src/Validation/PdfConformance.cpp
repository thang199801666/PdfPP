#include <CPPPdf/Validation/PdfConformance.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Document/PdfPage.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Rendering/PdfDisplayList.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf {
namespace {

struct PdfARequirements {
    int part{};          // 1, 2, 3, or 4
    char level{};        // 'A', 'B', or 'U' ('\0' for A4)
    bool tagged{};       // level A
    bool unicode{};      // level U
    bool minimalVersionFour{}; // A-1 requires PDF 1.4
    bool permitsTransparency{};
};

std::optional<PdfARequirements> pdfARequirements(const PdfConformanceProfile profile) {
    switch (profile) {
    case PdfConformanceProfile::PdfA1A: return PdfARequirements{1, 'A', true, false, true, false};
    case PdfConformanceProfile::PdfA1B: return PdfARequirements{1, 'B', false, false, true, false};
    case PdfConformanceProfile::PdfA2A: return PdfARequirements{2, 'A', true, false, false, true};
    case PdfConformanceProfile::PdfA2B: return PdfARequirements{2, 'B', false, false, false, true};
    case PdfConformanceProfile::PdfA2U: return PdfARequirements{2, 'U', false, true, false, true};
    case PdfConformanceProfile::PdfA3A: return PdfARequirements{3, 'A', true, false, false, true};
    case PdfConformanceProfile::PdfA3B: return PdfARequirements{3, 'B', false, false, false, true};
    case PdfConformanceProfile::PdfA3U: return PdfARequirements{3, 'U', false, true, false, true};
    case PdfConformanceProfile::PdfA4: return PdfARequirements{4, '\0', false, false, false, true};
    default: return std::nullopt;
    }
}

const PdfObject* resolveObject(const PdfDocument& document, const PdfObject* object) {
    if (object == nullptr) return nullptr;
    const auto reference = object->AsReference();
    if (!reference) return object;
    return &document.GetObject(PdfReference{reference->first, reference->second});
}

const PdfDictionary* resolveDictionary(const PdfDocument& document, const PdfObject* object) {
    object = resolveObject(document, object);
    if (object == nullptr) return nullptr;
    if (const auto* dictionary = object->AsDictionary()) return dictionary;
    if (const auto* stream = object->AsStream()) return &stream->dictionary();
    return nullptr;
}

std::string decodeStream(const PdfDocument& document, const PdfObject* object) {
    object = resolveObject(document, object);
    const auto* stream = object ? object->AsStream() : nullptr;
    if (!stream) return {};
    std::vector<std::byte> bytes(stream->bytes().begin(), stream->bytes().end());
    const PdfObject* filterObject = stream->dictionary().Find(PdfName("Filter"));
    if (filterObject == nullptr) {
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    std::vector<PdfFilterSpec> filters;
    if (const auto* name = filterObject->AsName()) {
        filters.push_back({name->value(), {}});
    } else if (const auto* array = filterObject->AsArray()) {
        for (const auto& item : array->values()) {
            if (const auto* itemName = item.AsName()) filters.push_back({itemName->value(), {}});
        }
    }
    try {
        if (!filters.empty()) bytes = PdfFilterPipeline().Decode(bytes, filters);
    } catch (...) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool containsWord(std::string_view text, std::string_view word) {
    return text.find(word) != std::string_view::npos;
}

std::string xmpTagValue(const std::string& metadata, const std::string_view tag) {
    const std::string open = "<" + std::string(tag) + ">";
    const std::string close = "</" + std::string(tag) + ">";
    const auto begin = metadata.find(open);
    if (begin == std::string::npos) return {};
    const auto start = begin + open.size();
    const auto end = metadata.find(close, start);
    if (end == std::string::npos) return {};
    return metadata.substr(start, end - start);
}

int parsePdfVersionMajor(std::string_view version) {
    if (version.size() < 3U || version[0] < '0' || version[0] > '9') return 0;
    return version[0] - '0';
}

int parsePdfVersionMinor(std::string_view version) {
    if (version.size() < 3U || version[0] < '0' || version[0] > '9' || version[1] != '.') return 0;
    if (version[2] < '0' || version[2] > '9') return 0;
    return version[2] - '0';
}

bool versionAtLeast(const PdfDocument& document, const int major, const int minor) {
    const int currentMajor = parsePdfVersionMajor(document.GetVersion());
    const int currentMinor = parsePdfVersionMinor(document.GetVersion());
    if (currentMajor != major) return currentMajor > major;
    return currentMinor >= minor;
}

std::string xmpConformanceName(const int part, const char level) {
    (void)part;
    if (level == '\0') return "A";
    return std::string(1, level);
}

bool hasGtsPdfAOutputIntent(const PdfDocument& document) {
    const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    if (!catalog) return false;
    const auto* outputIntents = catalog->Find(PdfName("OutputIntents"));
    if (!outputIntents) return false;
    const auto* array = resolveObject(document, outputIntents);
    if (!array) return false;
    const PdfArray* intents = nullptr;
    if (const auto* direct = array->AsArray()) intents = direct;
    else if (const auto reference = array->AsReference()) {
        if (const auto* resolved = document.GetObject({reference->first, reference->second}).AsArray()) {
            intents = resolved;
        }
    }
    if (!intents || intents->empty()) return false;
    for (const auto& intent : intents->values()) {
        const auto* dictionary = resolveDictionary(document, &intent);
        if (!dictionary) continue;
        const auto subtype = dictionary->GetAsName(PdfName("S"));
        if (subtype && subtype->value() == "GTS_PDFA1") return true;
    }
    return false;
}

std::string annotationSubtype(const PdfDocument& document, const PdfObject& annotation) {
    const auto* dictionary = resolveDictionary(document, &annotation);
    if (!dictionary) return {};
    const auto subtype = dictionary->GetAsName(PdfName("Subtype"));
    return subtype ? subtype->value() : std::string{};
}

void collectAnnotationSubtypes(
    const PdfDocument& document,
    std::vector<std::string>& output) {
    for (std::size_t pageIndex = 0; pageIndex < document.GetPageCount(); ++pageIndex) {
        const auto pageReference = document.GetPageReference(pageIndex);
        const auto* page = document.GetObject(pageReference).AsDictionary();
        if (!page) continue;
        const PdfObject* annotsObject = page->Find(PdfName("Annots"));
        if (!annotsObject) continue;
        const auto* resolved = resolveObject(document, annotsObject);
        if (!resolved) continue;
        const PdfArray* annots = nullptr;
        if (const auto* direct = resolved->AsArray()) annots = direct;
        else if (const auto reference = resolved->AsReference()) {
            if (const auto* array = document.GetObject({reference->first, reference->second}).AsArray()) {
                annots = array;
            }
        }
        if (!annots) continue;
        for (const auto& annotation : annots->values()) {
            const std::string subtype = annotationSubtype(document, annotation);
            if (!subtype.empty()) output.push_back(subtype);
        }
    }
}

bool pdfA1ForbiddenAnnotation(const std::string_view subtype) {
    return subtype == "Sound" || subtype == "Movie" || subtype == "Screen" ||
           subtype == "FileAttachment";
}

} // namespace

bool PdfValidationResult::IsValid() const noexcept {
    for (const auto& issue : issues) if (issue.error) return false;
    return true;
}

PdfValidationResult PdfConformanceValidator::Validate(
    const PdfDocument& document, const PdfConformanceProfile profile) {
    PdfValidationResult result;
    result.profile = profile;
    if (document.GetPageCount() == 0U) {
        result.issues.push_back({"PDF-PAGE-001", "Document must contain at least one page.", true});
    }

    const auto pdfA = pdfARequirements(profile);
    if (pdfA.has_value()) {
        // A PDF/A file must not be encrypted.
        if (document.IsEncrypted()) {
            result.issues.push_back({"PDFA-ENCRYPT-001",
                "PDF/A forbids document encryption.", true});
        }

        // File header version requirement.
        const bool versionOk = pdfA->minimalVersionFour
            ? versionAtLeast(document, 1, 4)
            : versionAtLeast(document, 1, 7);
        if (!versionOk) {
            result.issues.push_back({"PDFA-VERSION-001",
                "PDF/A-" + std::to_string(pdfA->part) +
                    " requires a supported PDF version header.", true});
        }

        const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
        if (catalog == nullptr) {
            result.issues.push_back({"PDFA-CATALOG-001", "Catalog object is not a dictionary.", true});
        } else {
            // XMP metadata stream with a matching pdfaid:part/conformance.
            const PdfObject* metadataObject = catalog->Find(PdfName("Metadata"));
            const std::string metadata = metadataObject ? decodeStream(document, metadataObject) : std::string{};
            if (metadataObject == nullptr || metadata.empty()) {
                result.issues.push_back({"PDFA-METADATA-001",
                    "PDF/A requires an XMP metadata stream.", true});
            } else {
                if (!containsWord(metadata, "pdfaid:part")) {
                    result.issues.push_back({"PDFA-METADATA-002",
                        "XMP metadata is missing the pdfaid:part declaration.", true});
                }
                const std::string declaredPart = xmpTagValue(metadata, "pdfaid:part");
                const std::string expectedPart = std::to_string(pdfA->part);
                if (declaredPart != expectedPart) {
                    result.issues.push_back({"PDFA-METADATA-003",
                        "XMP metadata pdfaid:part does not match PDF/A-" + expectedPart + ".", true});
                }
                const std::string conformance = xmpConformanceName(pdfA->part, pdfA->level);
                if (conformance != "A") {
                    const std::string declaredConformance = xmpTagValue(metadata, "pdfaid:conformance");
                    if (declaredConformance != conformance) {
                        result.issues.push_back({"PDFA-METADATA-004",
                            "XMP metadata pdfaid:conformance does not match level " + conformance + ".", true});
                    }
                }
            }

            if (catalog->Find(PdfName("OutputIntents")) == nullptr) {
                result.issues.push_back({"PDFA-OUTPUT-001",
                    "PDF/A requires an output intent.", true});
            } else if (!hasGtsPdfAOutputIntent(document)) {
                result.issues.push_back({"PDFA-OUTPUT-002",
                    "PDF/A requires a /GTS_PDFA1 output intent.", true});
            }

            if (pdfA->tagged) {
                if (catalog->Find(PdfName("StructTreeRoot")) == nullptr) {
                    result.issues.push_back({"PDFA-TAGGED-001",
                        "PDF/A-" + std::to_string(pdfA->part) + "A requires a structure tree root.", true});
                }
                const auto* markInfo = resolveDictionary(document, catalog->Find(PdfName("MarkInfo")));
                const auto* marked = markInfo ? markInfo->Find(PdfName("Marked")) : nullptr;
                if (marked == nullptr || !marked->AsBoolean().value_or(false)) {
                    result.issues.push_back({"PDFA-TAGGED-002",
                        "PDF/A-" + std::to_string(pdfA->part) + "A requires /MarkInfo /Marked true.", true});
                }
            }
        }

        // Annotation subtype restrictions.
        if (pdfA->part == 1) {
            std::vector<std::string> subtypes;
            collectAnnotationSubtypes(document, subtypes);
            for (const auto& subtype : subtypes) {
                if (pdfA1ForbiddenAnnotation(subtype)) {
                    result.issues.push_back({"PDFA-ANNOT-001",
                        "PDF/A-1 forbids /" + subtype + " annotations.", true});
                }
            }
        }

        for (std::size_t page = 0; page < document.GetPageCount(); ++page) {
            if (document.GetPage(page).GetResourcesDictionary().empty()) {
                result.issues.push_back({"PDFA-FONT-001",
                    "Page resources are missing; embedded font validation cannot pass.", true});
            }
            const auto displayList = document.BuildPageDisplayList(page);
            const bool hasTransparency = displayList.Count(
                PdfContentEventType::BeginTransparencyGroup) != 0U;
            if (hasTransparency && !pdfA->permitsTransparency) {
                result.issues.push_back({"PDFA-TRANSPARENCY-001",
                    "PDF/A-1 forbids transparency groups and blend modes.", true});
            }
            for (const auto& event : displayList.Events()) {
                if (event.type != PdfContentEventType::SetFont || event.textState.fontResource.empty()) continue;
                const auto font = document.ResolveFont(page, event.resourceObjectNumber, event.textState.fontResource);
                const auto* embeddedTrueType = font ? font->GetEmbeddedTrueTypeFont() : nullptr;
                const bool embeddedCff = font != nullptr && font->HasEmbeddedCffFont();
                if (embeddedTrueType == nullptr && !embeddedCff) {
                    result.issues.push_back({"PDFA-FONT-002",
                        "PDF/A requires embedded fonts; " + event.textState.fontResource + " is not embedded.", true});
                } else if (embeddedTrueType != nullptr &&
                           (!embeddedTrueType->HasTable("cmap") ||
                            !embeddedTrueType->HasTable("head") ||
                            !embeddedTrueType->HasTable("hhea") ||
                            !embeddedTrueType->HasTable("maxp"))) {
                    result.issues.push_back({"PDFA-FONT-003",
                        "Embedded TrueType font is missing required tables.", true});
                }
                if (pdfA->unicode && font && !font->HasUnicodeMapping()) {
                    result.issues.push_back({"PDFA-FONT-004",
                        "PDF/A-" + std::to_string(pdfA->part) + "U requires a ToUnicode CMap.", true});
                }
            }
        }
    }

    if (profile == PdfConformanceProfile::PdfUA1) {
        const auto catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
        if (catalog == nullptr || catalog->Find(PdfName("StructTreeRoot")) == nullptr) {
            result.issues.push_back({"PDFUA-TAG-001", "PDF/UA requires a structure tree root.", true});
        }
        if (document.GetDocumentInfo().title.empty()) {
            result.issues.push_back({"PDFUA-TITLE-001", "PDF/UA requires a document title.", false});
        }
    }

    if (profile == PdfConformanceProfile::PdfA4) {
        result.issues.push_back({"PDFA-IMPLEMENTATION-001",
            "PDF/A-4 validation is experimental; catalog and structure checks are partial.", false});
    }
    if (profile == PdfConformanceProfile::PdfUA1) {
        result.issues.push_back({"PDFUA-IMPLEMENTATION-001",
            "PDF/UA validation requires tagged structure and language inspection.", false});
    }
    return result;
}

} // namespace CPPPdf

#include <CPPPdf/Validation/PdfConformance.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>

namespace CPPPdf {

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
    if (profile == PdfConformanceProfile::PdfA1B ||
        profile == PdfConformanceProfile::PdfA2B ||
        profile == PdfConformanceProfile::PdfA3B) {
        const auto catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
        if (catalog == nullptr) {
            result.issues.push_back({"PDFA-CATALOG-001", "Catalog object is not a dictionary.", true});
        } else if (catalog->Find(PdfName("Metadata")) == nullptr) {
            result.issues.push_back({"PDFA-METADATA-001", "PDF/A requires an XMP metadata stream.", true});
        }
        if (catalog != nullptr && catalog->Find(PdfName("OutputIntents")) == nullptr) {
            result.issues.push_back({"PDFA-OUTPUT-001", "PDF/A requires an output intent.", true});
        }
        for (std::size_t page = 0; page < document.GetPageCount(); ++page) {
            if (document.GetPage(page).GetResourcesDictionary().empty()) {
                result.issues.push_back({"PDFA-FONT-001", "Page resources are missing; embedded font validation cannot pass.", true});
            }
            const auto displayList = document.BuildPageDisplayList(page);
            for (const auto& event : displayList.Events()) {
                if (event.type != PdfContentEventType::SetFont || event.textState.fontResource.empty()) continue;
                const auto font = document.ResolveFont(page, event.resourceObjectNumber, event.textState.fontResource);
                const auto* embedded = font ? font->GetEmbeddedTrueTypeFont() : nullptr;
                if (embedded == nullptr) {
                    result.issues.push_back({"PDFA-FONT-002", "PDF/A requires embedded fonts.", true});
                } else if (!embedded->HasTable("cmap") || !embedded->HasTable("head") ||
                           !embedded->HasTable("hhea") || !embedded->HasTable("maxp")) {
                    result.issues.push_back({"PDFA-FONT-003", "Embedded TrueType font is missing required tables.", true});
                }
                if (font && font->RequiresExternalShaping()) {
                    result.issues.push_back({"FONT-SHAPING-001",
                        "Composite/CID font requires an external shaping and positioning engine.", false});
                }
                if (font && font->HasEmbeddedCffFont() && !font->CanRenderEmbeddedGlyphs()) {
                    result.issues.push_back({"FONT-CFF-001",
                        "Embedded CFF/OpenType font is detected but native glyph rasterization is not available.", false});
                }
            }
            const auto content = document.BuildPageDisplayList(page);
            if (content.Count(PdfContentEventType::UnknownOperator) != 0U) {
                result.issues.push_back({"PDFA-TRANSPARENCY-001",
                    "Page contains unsupported graphics-state operators that may affect PDF/A transparency conformance.", false});
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
    if (profile == PdfConformanceProfile::PdfA1B ||
        profile == PdfConformanceProfile::PdfA2B ||
        profile == PdfConformanceProfile::PdfA3B) {
        result.issues.push_back({"PDFA-IMPLEMENTATION-001",
            "PDF/A validation requires output intent, embedded fonts, and metadata inspection.", false});
    }
    if (profile == PdfConformanceProfile::PdfUA1) {
        result.issues.push_back({"PDFUA-IMPLEMENTATION-001",
            "PDF/UA validation requires tagged structure and language inspection.", false});
    }
    return result;
}

} // namespace CPPPdf

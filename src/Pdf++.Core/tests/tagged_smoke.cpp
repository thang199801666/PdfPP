#include <CPPPdf/CPPPdf.h>
#include <CPPPdf/Validation/PdfConformance.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main() {
    try {
        using namespace CPPPdf;
        const auto path = std::filesystem::temp_directory_path() / "pdfpp_tagged_mcid_smoke.pdf";
        PdfWriter writer;
        writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Tagged smoke document");
        writer.SetTaggedDocumentAlternativeText("Tagged smoke document");
        writer.SetTaggedRoleMap("Callout", "Note");
        const auto pageIndex = writer.AddPage(PdfRectangle{0.0, 0.0, 300.0, 200.0});
        auto canvas = writer.GetCanvas(pageIndex);
        PdfMarkedContentOptions section;
        section.role = "Sect";
        section.title = "Smoke section";
        section.language = "en-US";
        section.expandedText = "Expanded smoke section";
        canvas.BeginMarkedContent(section);
        canvas.BeginMarkedContent("P", "A tagged paragraph", "Accessible paragraph")
              .FillRectangle(20.0, 20.0, 80.0, 30.0)
              .EndMarkedContent()
              .EndMarkedContent();
        canvas.BeginArtifact("Pagination")
              .DrawLine(0.0, 10.0, 300.0, 10.0)
              .EndMarkedContent();
        writer.Save(path);

        const PdfDocument document = PdfDocument::Open(path);
        const auto validation = PdfConformanceValidator::Validate(document, PdfConformanceProfile::PdfUA1);
        if (!validation.IsValid()) {
            std::cerr << validation.ToText();
            return 1;
        }

        std::ifstream input(path, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const bool hasMcid = bytes.find("/MCID 0") != std::string::npos;
        const bool hasParentTree = bytes.find("/ParentTree") != std::string::npos;
        const bool hasStructParents = bytes.find("/StructParents 0") != std::string::npos;
        const bool hasAlt = bytes.find("/Alt (A tagged paragraph)") != std::string::npos;
        const bool hasArtifact = bytes.find("/Artifact << /Type /Pagination >> BDC") != std::string::npos;
        const bool hasNestedSection = bytes.find("/S /Sect") != std::string::npos &&
            bytes.find("/T (Smoke section)") != std::string::npos &&
            bytes.find("/Lang (en-US)") != std::string::npos &&
            bytes.find("/E (Expanded smoke section)") != std::string::npos;
        std::filesystem::remove(path);
        if (!hasMcid || !hasParentTree || !hasStructParents || !hasAlt || !hasArtifact || !hasNestedSection) return 2;
        std::cout << "Tagged PDF/MCID smoke test passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}

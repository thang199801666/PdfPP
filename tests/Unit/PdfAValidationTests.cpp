#include <CPPPdf/Validation/PdfConformance.hpp>
#include <CPPPdf/Document/PdfDocument.hpp>
#include "TestRunner.hpp"

#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace CPPPdf;

namespace {

std::vector<std::byte> buildConformancePdf(bool includeMetadata, bool includeOutputIntent) {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 7> offsets{};
    const auto addObject = [&](const std::size_t number, const std::string& body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
    };
    std::string catalog = "<< /Type /Catalog /Pages 2 0 R";
    if (includeMetadata) catalog += " /Metadata 5 0 R";
    if (includeOutputIntent) catalog += " /OutputIntents [6 0 R]";
    catalog += " >>";
    addObject(1, catalog);
    addObject(2, "<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 4 0 R >>");
    addObject(4, "<< /Length 0 >>\nstream\n\nendstream");
    const std::string xmp =
        "<?xpacket begin=\"\xff\xfe\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "<rdf:Description rdf:about=\"\" "
        "xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\">\n"
        "<pdfaid:part>1</pdfaid:part>\n"
        "<pdfaid:conformance>B</pdfaid:conformance>\n"
        "</rdf:Description>\n</rdf:RDF>\n</x:xmpmeta>\n";
    addObject(5, "<< /Type /Metadata /Subtype /XML /Length " + std::to_string(xmp.size()) + " >>\nstream\n" + xmp + "\nendstream");
    addObject(6, "<< /Type /OutputIntent /S /GTS_PDFA1 /OutputConditionIdentifier (sRGB) /Info (sRGB) >>");
    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 7\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();
    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(pdf[i]);
    return result;
}

bool hasIssue(const PdfValidationResult& result, const char* code) {
    for (const auto& issue : result.issues) {
        if (issue.error && issue.code == code) return true;
    }
    return false;
}

} // namespace

void TestPdfAConforming() {
    const auto conforming = buildConformancePdf(true, true);
    const auto document = PdfDocument::Open(std::span<const std::byte>(conforming));
    const auto result = PdfConformanceValidator::Validate(document, PdfConformanceProfile::PdfA1B);
    PDFPP_TEST_CHECK(!hasIssue(result, "PDFA-METADATA-001"));
    PDFPP_TEST_CHECK(!hasIssue(result, "PDFA-OUTPUT-001"));
    PDFPP_TEST_CHECK(!hasIssue(result, "PDFA-OUTPUT-002"));
}

void TestPdfAMissingMetadataAndOutput() {
    const auto document = PdfDocument::Open(std::span<const std::byte>(buildConformancePdf(false, false)));
    const auto result = PdfConformanceValidator::Validate(document, PdfConformanceProfile::PdfA1B);
    PDFPP_TEST_CHECK(hasIssue(result, "PDFA-METADATA-001"));
    PDFPP_TEST_CHECK(hasIssue(result, "PDFA-OUTPUT-001"));
}

void TestPdfAPartMismatch() {
    auto bytes = buildConformancePdf(true, true);
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto pos = text.find("<pdfaid:part>1</pdfaid:part>");
    PDFPP_TEST_CHECK(pos != std::string::npos);
    text.replace(pos, 28, "<pdfaid:part>2</pdfaid:part>");
    std::vector<std::byte> mismatched(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) mismatched[i] = static_cast<std::byte>(text[i]);
    const auto document = PdfDocument::Open(std::span<const std::byte>(mismatched));
    const auto result = PdfConformanceValidator::Validate(document, PdfConformanceProfile::PdfA1B);
    PDFPP_TEST_CHECK(hasIssue(result, "PDFA-METADATA-003"));
}

void TestPdfUAStructure() {
    const auto conforming = buildConformancePdf(true, true);
    const auto document = PdfDocument::Open(std::span<const std::byte>(conforming));
    const auto result = PdfConformanceValidator::Validate(document, PdfConformanceProfile::PdfUA1);
    // The minimal test file has no structure tree, /Lang, or /MarkInfo.
    PDFPP_TEST_CHECK(hasIssue(result, "PDFUA-TAG-001"));
    PDFPP_TEST_CHECK(hasIssue(result, "PDFUA-LANG-001"));
    PDFPP_TEST_CHECK(hasIssue(result, "PDFUA-MARKED-001"));
}

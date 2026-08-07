#include <CPPPdf/CPPPdf.h>
#include <CPPPdf/Validation/PdfConformance.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open test data: " + path.string());
    const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> bytes(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
    }
    return bytes;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

template <typename Function>
bool Throws(Function&& function) {
    try {
        function();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

bool ValidateOrPrint(const std::filesystem::path& path,
                     const CPPPdf::PdfConformanceProfile profile) {
    const auto result = CPPPdf::PdfConformanceValidator::ValidateFile(path, profile);
    if (!result.IsValid()) std::cerr << result.ToText();
    if (result.ToJson().find("\"profile\"") == std::string::npos) return false;
    return result.IsValid();
}
} // namespace

int main() {
    try {
        using namespace CPPPdf;
        const auto directory = std::filesystem::current_path();
        const auto icc = ReadBytes(std::filesystem::path(PDFPP_TEST_DATA_DIR) / "pdfpp_test_srgb.icc");

        const auto a4fUa2Path = directory / "pdfa4f_ua2_smoke.pdf";
        {
            PdfWriter writer;
            writer.ConfigureForPdfA(PdfConformanceProfile::PdfA4F, icc, "sRGB IEC61966-2.1");
            writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA2, "en-US", "Pdf++ PDF/A-4F and PDF/UA-2 smoke");
            writer.SetAuthor("Pdf++ conformance tests");
            writer.SetTaggedDocumentAlternativeText("Accessible conformance sample");
            const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 300.0, 220.0});
            auto canvas = writer.GetCanvas(page);

            PdfMarkedContentOptions section;
            section.role = "Sect";
            section.title = "Conformance section";
            section.language = "en-US";
            canvas.BeginMarkedContent(section);
            PdfMarkedContentOptions figure;
            figure.role = "Figure";
            figure.alternativeText = "Blue rectangular engineering result graphic";
            figure.identifier = "result-figure";
            canvas.BeginMarkedContent(figure)
                  .SetFillColor(PdfColor{0.1, 0.3, 0.8})
                  .FillRectangle(30.0, 70.0, 150.0, 80.0)
                  .EndMarkedContent();

            PdfMarkedContentOptions list;
            list.role = "L";
            canvas.BeginMarkedContent(list);
            canvas.BeginMarkedContent("LI");
            canvas.BeginMarkedContent("Lbl", "List marker", "1.")
                  .FillRectangle(25.0, 52.0, 4.0, 4.0)
                  .EndMarkedContent();
            canvas.BeginMarkedContent("LBody", "List item body", "Engineering result item")
                  .FillRectangle(34.0, 50.0, 50.0, 8.0)
                  .EndMarkedContent();
            canvas.EndMarkedContent().EndMarkedContent();

            PdfMarkedContentOptions table;
            table.role = "Table";
            canvas.BeginMarkedContent(table);
            canvas.BeginMarkedContent("TR");
            PdfMarkedContentOptions header;
            header.role = "TH";
            header.identifier = "stress-header";
            header.attributes.scope = PdfTableScope::Column;
            canvas.BeginMarkedContent(header)
                  .FillRectangle(100.0, 45.0, 45.0, 10.0)
                  .EndMarkedContent();
            PdfMarkedContentOptions cell;
            cell.role = "TD";
            cell.attributes.headers = {"stress-header"};
            canvas.BeginMarkedContent(cell)
                  .FillRectangle(145.0, 45.0, 45.0, 10.0)
                  .EndMarkedContent();
            canvas.EndMarkedContent().EndMarkedContent();
            canvas.EndMarkedContent();
            canvas.BeginArtifact("Pagination")
                  .DrawLine(20.0, 20.0, 280.0, 20.0)
                  .EndMarkedContent();

            const std::string csv = "name,value\nstress,320\n";
            std::vector<std::byte> data(csv.size());
            for (std::size_t index = 0; index < csv.size(); ++index) {
                data[index] = static_cast<std::byte>(static_cast<unsigned char>(csv[index]));
            }
            PdfEmbeddedFileOptions embedded;
            embedded.description = "Source engineering data";
            embedded.mimeType = "text/csv";
            embedded.relationship = PdfAssociatedFileRelationship::Data;
            embedded.associateWithDocument = true;
            writer.AddEmbeddedFile("source-data.csv", data, embedded);

            PdfLinkOptions link;
            link.rectangle = PdfRectangle{30.0, 165.0, 190.0, 190.0};
            link.accessibleDescription = "Open the Pdf++ project website";
            writer.AddUriLink(page, "https://example.com/pdfpp", link);

            PdfFileAttachmentOptions attachment;
            attachment.rectangle = PdfRectangle{210.0, 70.0, 230.0, 90.0};
            attachment.contents = "Attached source engineering data";
            attachment.alternativeText = "CSV source data attachment";
            writer.AddFileAttachment(page, "source-data.csv", attachment);
            writer.Save(a4fUa2Path);
        }
        if (!ValidateOrPrint(a4fUa2Path, PdfConformanceProfile::PdfA4F)) return 1;
        if (!ValidateOrPrint(a4fUa2Path, PdfConformanceProfile::PdfUA2)) return 2;
        const auto a4fUa2 = ReadText(a4fUa2Path);
        if (a4fUa2.find("%PDF-2.0") == std::string::npos ||
            a4fUa2.find("<pdfuaid:rev>2024</pdfuaid:rev>") == std::string::npos ||
            a4fUa2.find("http://iso.org/pdf2/ssn") == std::string::npos ||
            a4fUa2.find("/AFRelationship /Data") == std::string::npos ||
            a4fUa2.find("/Type /OBJR") == std::string::npos ||
            a4fUa2.find("/Tabs /S") == std::string::npos ||
            a4fUa2.find("<dc:language><rdf:Bag><rdf:li>en-US</rdf:li>") == std::string::npos ||
            a4fUa2.find("<pdfaExtension:schemas>") == std::string::npos ||
            a4fUa2.find("<pdfaProperty:name>rev</pdfaProperty:name>") == std::string::npos ||
            a4fUa2.find("/S /L") == std::string::npos ||
            a4fUa2.find("/S /Table") == std::string::npos) return 3;

        const auto a2aUa1Path = directory / "pdfa2a_ua1_smoke.pdf";
        {
            PdfWriter writer;
            writer.ConfigureForPdfA(PdfConformanceProfile::PdfA2A, icc, "sRGB IEC61966-2.1");
            writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Pdf++ PDF/A-2A and PDF/UA-1 smoke");
            const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 200.0, 120.0});
            auto canvas = writer.GetCanvas(page);
            canvas.BeginMarkedContent("P", "Accessible geometric paragraph", "Accessible geometric paragraph")
                  .FillRectangle(20.0, 20.0, 80.0, 40.0)
                  .EndMarkedContent();
            writer.Save(a2aUa1Path);
        }
        if (!ValidateOrPrint(a2aUa1Path, PdfConformanceProfile::PdfA2A)) return 4;
        if (!ValidateOrPrint(a2aUa1Path, PdfConformanceProfile::PdfUA1)) return 5;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfA(PdfConformanceProfile::PdfA1A, icc, "sRGB");
                writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Incompatible profiles");
            })) return 6;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Untagged content");
                const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
                writer.GetCanvas(page).FillRectangle(10.0, 10.0, 20.0, 20.0);
                writer.Save(directory / "must_reject_untagged.pdf");
            })) return 7;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Missing figure alternative");
                const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
                PdfMarkedContentOptions figure;
                figure.role = "Figure";
                writer.GetCanvas(page).BeginMarkedContent(figure).FillRectangle(5.0, 5.0, 20.0, 20.0).EndMarkedContent();
                writer.Save(directory / "must_reject_missing_alt.pdf");
            })) return 8;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfA(PdfConformanceProfile::PdfA4F, icc, "sRGB");
                (void)writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
                writer.Save(directory / "must_reject_empty_a4f.pdf");
            })) return 9;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfA(PdfConformanceProfile::PdfA4, icc, "sRGB");
                (void)writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
                const std::vector<std::byte> bytes{std::byte{'x'}};
                writer.AddEmbeddedFile("not-allowed.bin", bytes);
                writer.Save(directory / "must_reject_a4_attachment.pdf");
            })) return 10;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Missing link description");
                const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
                writer.GetCanvas(page).BeginArtifact().FillRectangle(1.0, 1.0, 2.0, 2.0).EndMarkedContent();
                PdfLinkOptions link;
                link.rectangle = PdfRectangle{5.0, 5.0, 30.0, 20.0};
                writer.AddUriLink(page, "https://example.com", link);
                writer.Save(directory / "must_reject_link.pdf");
            })) return 11;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Invalid list structure");
                const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
                auto canvas = writer.GetCanvas(page);
                canvas.BeginMarkedContent("L")
                      .BeginMarkedContent("P", "Invalid list child", "Invalid list child")
                      .FillRectangle(5.0, 5.0, 10.0, 10.0)
                      .EndMarkedContent()
                      .EndMarkedContent();
                writer.Save(directory / "must_reject_invalid_list.pdf");
            })) return 12;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA2, "en-US", "Cyclic RoleMap");
                writer.SetTaggedRoleMap("RoleA", "RoleB");
                writer.SetTaggedRoleMap("RoleB", "RoleA");
                const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
                writer.GetCanvas(page)
                    .BeginMarkedContent("RoleA", "Mapped content", "Mapped content")
                    .FillRectangle(5.0, 5.0, 10.0, 10.0)
                    .EndMarkedContent();
                writer.Save(directory / "must_reject_role_cycle.pdf");
            })) return 13;

        if (!Throws([&] {
                PdfWriter writer;
                writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Invalid table headers");
                const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
                auto canvas = writer.GetCanvas(page);
                canvas.BeginMarkedContent("Table").BeginMarkedContent("TR");
                PdfMarkedContentOptions cell;
                cell.role = "TD";
                cell.attributes.headers = {"missing-header"};
                canvas.BeginMarkedContent(cell).FillRectangle(1.0, 1.0, 5.0, 5.0).EndMarkedContent();
                canvas.EndMarkedContent().EndMarkedContent();
                writer.Save(directory / "must_reject_missing_table_header.pdf");
            })) return 14;

        const auto invalidSemanticPath = directory / "pdfua_invalid_semantic_fixture.pdf";
        {
            PdfWriter writer;
            writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Invalid semantic fixture");
            writer.SetConformanceEnforcement(false);
            const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
            auto canvas = writer.GetCanvas(page);
            canvas.BeginMarkedContent("L")
                  .BeginMarkedContent("P", "Invalid list child", "Invalid list child")
                  .FillRectangle(10.0, 10.0, 20.0, 20.0)
                  .EndMarkedContent()
                  .EndMarkedContent();
            writer.Save(invalidSemanticPath);
        }
        const auto invalidSemantic = PdfConformanceValidator::ValidateFile(
            invalidSemanticPath, PdfConformanceProfile::PdfUA1);
        if (invalidSemantic.IsValid() ||
            invalidSemantic.ToJson().find("PDFUA-SEMANTIC-001") == std::string::npos) return 15;

        const auto invalidExtensionPath = directory / "pdfa4f_ua2_missing_extension.pdf";
        {
            std::string modified = a4fUa2;
            std::size_t position = 0U;
            while ((position = modified.find("pdfaExtension:schemas", position)) != std::string::npos) {
                modified.replace(position, std::string("pdfaExtension:schemas").size(), "pdfaExtension:schemaX");
                position += std::string("pdfaExtension:schemaX").size();
            }
            std::ofstream output(invalidExtensionPath, std::ios::binary);
            output.write(modified.data(), static_cast<std::streamsize>(modified.size()));
        }
        const auto invalidExtension = PdfConformanceValidator::ValidateFile(
            invalidExtensionPath, PdfConformanceProfile::PdfA4F);
        if (invalidExtension.IsValid() ||
            invalidExtension.ToJson().find("PDFA-XMP-EXT-001") == std::string::npos) return 16;

        const auto invalidPath = directory / "pdfua_invalid_fixture.pdf";
        {
            PdfWriter writer;
            writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA1, "en-US", "Invalid fixture");
            writer.SetConformanceEnforcement(false);
            const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 100.0, 100.0});
            writer.GetCanvas(page).FillRectangle(10.0, 10.0, 20.0, 20.0);
            writer.Save(invalidPath);
        }
        const auto invalid = PdfConformanceValidator::ValidateFile(invalidPath, PdfConformanceProfile::PdfUA1);
        if (invalid.IsValid() || invalid.ErrorCount() == 0U ||
            invalid.ToJson().find("PDFUA-CONTENT-001") == std::string::npos) return 17;

        std::cout << "PDF/A and PDF/UA conformance smoke tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 20;
    }
}

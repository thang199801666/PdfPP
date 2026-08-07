#include <CPPPdf/CPPPdf.h>
#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
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
    if (!input) throw std::runtime_error("Cannot open " + path.string());
    const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> bytes(chars.size());
    for (std::size_t index = 0U; index < chars.size(); ++index) {
        bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
    }
    return bytes;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

void WriteText(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write " + path.string());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool ContainsCode(const CPPPdf::PdfValidationResult& result, const std::string& code) {
    for (const auto& issue : result.issues) {
        if (issue.code == code) return true;
    }
    return false;
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
} // namespace

int main() {
    try {
        using namespace CPPPdf;
        const auto directory = std::filesystem::current_path();
        const auto icc = ReadBytes(std::filesystem::path(PDFPP_TEST_DATA_DIR) / "pdfpp_test_srgb.icc");

        const auto validPath = directory / "pdfa_ua_hardening_smoke.pdf";
        {
            PdfWriter writer;
            writer.ConfigureForPdfA(PdfConformanceProfile::PdfA4F, icc, "sRGB IEC61966-2.1");
            writer.ConfigureForPdfUa(PdfConformanceProfile::PdfUA2, "en-US", "Pdf++ conformance hardening");
            const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 240.0, 180.0});
            auto canvas = writer.GetCanvas(page);
            canvas.BeginMarkedContent("H1", "Primary heading", "Primary heading")
                  .FillRectangle(20.0, 140.0, 100.0, 12.0)
                  .EndMarkedContent();
            canvas.BeginMarkedContent("H3", "Skipped heading", "Skipped heading")
                  .FillRectangle(20.0, 115.0, 80.0, 10.0)
                  .EndMarkedContent();
            canvas.BeginMarkedContent("P", "First paragraph", "First paragraph")
                  .FillRectangle(20.0, 85.0, 120.0, 10.0)
                  .EndMarkedContent();
            canvas.BeginMarkedContent("P", "Second paragraph", "Second paragraph")
                  .FillRectangle(20.0, 60.0, 120.0, 10.0)
                  .EndMarkedContent();
            const std::string csv = "name,value\nstress,320\n";
            std::vector<std::byte> data(csv.size());
            for (std::size_t index = 0U; index < csv.size(); ++index) {
                data[index] = static_cast<std::byte>(static_cast<unsigned char>(csv[index]));
            }
            PdfEmbeddedFileOptions embedded;
            embedded.description = "Engineering source data";
            embedded.mimeType = "text/csv";
            embedded.relationship = PdfAssociatedFileRelationship::Data;
            writer.AddEmbeddedFile("source-data.csv", data, embedded);
            writer.Save(validPath);
        }

        const auto raw = ReadText(validPath);
        if (raw.find("/CheckSum <5B4E0B74790E5C50DB2F7070F2238542>") == std::string::npos ||
            raw.find("/ModDate (D:") == std::string::npos) return 1;

        const auto pdfa = PdfConformanceValidator::ValidateFile(validPath, PdfConformanceProfile::PdfA4F);
        if (!pdfa.IsValid()) {
            std::cerr << pdfa.ToText();
            return 2;
        }
        const auto pdfua = PdfConformanceValidator::ValidateFile(validPath, PdfConformanceProfile::PdfUA2);
        if (!pdfua.IsValid() || !ContainsCode(pdfua, "PDFUA-HEADING-003")) {
            std::cerr << pdfua.ToText();
            return 3;
        }

        auto corrupt = raw;
        const auto checksum = corrupt.find("/CheckSum <5B4E0B74790E5C50DB2F7070F2238542>");
        if (checksum == std::string::npos) return 4;
        corrupt[checksum + std::string("/CheckSum <").size()] = 'A';
        const auto corruptPath = directory / "pdfa_corrupt_embedded_checksum.pdf";
        WriteText(corruptPath, corrupt);
        const auto corruptResult = PdfConformanceValidator::ValidateFile(
            corruptPath, PdfConformanceProfile::PdfA4F);
        if (corruptResult.IsValid() || !ContainsCode(corruptResult, "PDFA-AF-011")) return 5;

        auto reordered = raw;
        const auto mcid0 = reordered.find("/MCID 0");
        const auto mcid1 = reordered.find("/MCID 1");
        if (mcid0 == std::string::npos || mcid1 == std::string::npos) return 6;
        reordered[mcid0 + 6U] = '1';
        reordered[mcid1 + 6U] = '0';
        const auto reorderedPath = directory / "pdfua_physical_order_differs.pdf";
        WriteText(reorderedPath, reordered);
        const auto orderResult = PdfConformanceValidator::ValidateFile(
            reorderedPath, PdfConformanceProfile::PdfUA2);
        if (!orderResult.IsValid() || !ContainsCode(orderResult, "PDFUA-ORDER-001")) {
            std::cerr << orderResult.ToText();
            return 7;
        }

        std::vector<std::byte> expanded(2U * 1024U * 1024U, std::byte{0});
        const auto compressed = PdfFilterPipeline::EncodeFlate(expanded);
        if (!Throws([&] {
                (void)PdfFilterPipeline(4U * 1024U * 1024U, 10U).Decode(
                    compressed, {PdfFilterSpec{"FlateDecode", {}}});
            })) return 8;
        const auto decoded = PdfFilterPipeline(4U * 1024U * 1024U, 0U).Decode(
            compressed, {PdfFilterSpec{"FlateDecode", {}}});
        if (decoded != expanded) return 9;

        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 100;
    }
}

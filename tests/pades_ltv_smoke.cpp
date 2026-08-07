#include <CPPPdf/CPPPdf.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::byte> Bytes(std::initializer_list<unsigned int> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) result.push_back(static_cast<std::byte>(value));
    return result;
}
}

int main() {
    try {
        using namespace CPPPdf;
        const auto directory = std::filesystem::current_path();
        const auto basePath = directory / "pades_ltv_base.pdf";
        const auto firstPath = directory / "pades_ltv_first.pdf";
        const auto mergedPath = directory / "pades_ltv_merged.pdf";
        const auto timestampOnlyPath = directory / "pades_ltv_timestamp_only.pdf";

        PdfWriter baseWriter;
        const auto page = baseWriter.AddPage(PdfRectangle{0.0, 0.0, 300.0, 200.0});
        baseWriter.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 12.0)
                  .MoveText(30.0, 100.0).ShowText("PAdES LTV smoke").EndText();
        baseWriter.Save(basePath);

        PdfDss::DssOptions first;
        first.certificates = {Bytes({0x30, 0x03, 0x02, 0x01, 0x01})};
        first.crls = {Bytes({0x30, 0x03, 0x02, 0x01, 0x02})};
        first.ocspResponses = {Bytes({0x30, 0x03, 0x02, 0x01, 0x03})};
        first.timestamps = {Bytes({0x30, 0x03, 0x02, 0x01, 0x04})};
        first.signatureContents = Bytes({1, 2, 3, 4, 5});
        first.validationTime = "D:20260806093000+07'00'";

        const auto result = PdfDss::AddDocumentSecurityStore(basePath, firstPath, first);
        if (result.certificateCount != 1U || result.crlCount != 1U ||
            result.ocspCount != 1U || result.timestampCount != 1U) return 1;
        if (result.vriKey != "11966AB9C099F8FABEFAC54C08D5BE2BD8C903AF") return 2;
        if (!PdfDss::HasDocumentSecurityStore(firstPath)) return 3;
        if (PdfDocument::Open(firstPath).GetPageCount() != 1U) return 4;

        PdfDss::DssOptions second;
        second.certificates = {Bytes({0x30, 0x03, 0x02, 0x01, 0x05})};
        second.timestamps = {Bytes({0x30, 0x03, 0x02, 0x01, 0x06})};
        second.signatureContents = first.signatureContents;
        second.validationTime = "D:20260806100000+07'00'";
        (void)PdfDss::AddDocumentSecurityStore(firstPath, mergedPath, second);
        if (!PdfDss::HasDocumentSecurityStore(mergedPath)) return 5;
        if (PdfDocument::Open(mergedPath).GetPageCount() != 1U) return 6;

        const auto merged = ReadText(mergedPath);
        for (const std::string token : {
                 "/Type /DSS", "/Certs [", "/CRLs [", "/OCSPs [", "/VRI <<",
                 "/11966AB9C099F8FABEFAC54C08D5BE2BD8C903AF <<",
                 "/Cert [", "/CRL [", "/OCSP [", "/TS [",
                 "/TU (D:20260806100000+07'00')"}) {
            if (merged.find(token) == std::string::npos) {
                std::cerr << "Missing token: " << token << '\n';
                return 7;
            }
        }

        PdfDss::DssOptions timestampOnly;
        timestampOnly.timestamps = {Bytes({0x30, 0x03, 0x02, 0x01, 0x07})};
        timestampOnly.signatureContents = Bytes({9, 8, 7});
        (void)PdfDss::AddDocumentSecurityStore(basePath, timestampOnlyPath, timestampOnly);
        const auto timestampOnlyBytes = ReadText(timestampOnlyPath);
        if (timestampOnlyBytes.find("/DSS") == std::string::npos ||
            timestampOnlyBytes.find("/TS [") == std::string::npos) return 8;

        std::cout << "PAdES DSS/VRI smoke test passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}

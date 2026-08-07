#include <CPPPdf/CPPPdf.h>
#include <CPPPdf/Validation/PdfConformance.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {
std::vector<std::byte> MinimalRgbIccProfile() {
    std::vector<std::byte> profile(132U, std::byte{0});
    const auto put32 = [&](const std::size_t offset, const std::uint32_t value) {
        profile[offset] = static_cast<std::byte>((value >> 24U) & 0xFFU);
        profile[offset + 1U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
        profile[offset + 2U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
        profile[offset + 3U] = static_cast<std::byte>(value & 0xFFU);
    };
    const auto putText = [&](const std::size_t offset, const char* text) {
        for (std::size_t i = 0U; i < 4U; ++i) profile[offset + i] = static_cast<std::byte>(text[i]);
    };
    put32(0U, static_cast<std::uint32_t>(profile.size()));
    put32(8U, 0x04300000U); // ICC 4.3
    putText(12U, "mntr");
    putText(16U, "RGB ");
    putText(20U, "XYZ ");
    putText(36U, "acsp");
    putText(40U, "MSFT");
    put32(68U, 0x0000F6D6U); // D50 X
    put32(72U, 0x00010000U); // D50 Y
    put32(76U, 0x0000D32DU); // D50 Z
    putText(80U, "PDFP");
    put32(128U, 0U); // no tag table entries in this structural smoke profile
    return profile;
}

bool CreateAndValidate(const std::filesystem::path& path,
                       const CPPPdf::PdfConformanceProfile profile,
                       const std::string& expectedVersion) {
    using namespace CPPPdf;
    PdfWriter writer;
    const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 300.0, 200.0});
    (void)page;
    const auto icc = MinimalRgbIccProfile();
    writer.ConfigureForPdfA(profile, icc, "Pdf++ test RGB");
    writer.SetTitle("Pdf++ PDF/A creation smoke test");
    writer.Save(path);

    const PdfDocument document = PdfDocument::Open(path);
    if (document.GetVersion() != expectedVersion || document.IsEncrypted()) return false;
    const auto validation = PdfConformanceValidator::Validate(document, profile);
    if (!validation.IsValid()) {
        for (const auto& issue : validation.issues) {
            if (issue.error) std::cerr << issue.code << ": " << issue.message << '\n';
        }
        return false;
    }
    return true;
}
} // namespace

int main() {
    try {
        const auto directory = std::filesystem::temp_directory_path();
        const auto a1 = directory / "pdfpp_pdfa1b_smoke.pdf";
        const auto a2 = directory / "pdfpp_pdfa2b_smoke.pdf";
        if (!CreateAndValidate(a1, CPPPdf::PdfConformanceProfile::PdfA1B, "1.4")) return 1;
        if (!CreateAndValidate(a2, CPPPdf::PdfConformanceProfile::PdfA2B, "1.7")) return 2;
        std::filesystem::remove(a1);
        std::filesystem::remove(a2);
        std::cout << "PDF/A creation smoke tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}

#include <CPPPdf/Security/PdfCms.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef PDFPP_TEST_DATA_DIR
#error PDFPP_TEST_DATA_DIR must be defined
#endif

namespace {
std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}
} // namespace

int main() {
    using namespace CPPPdf;
    try {
        const std::filesystem::path dataDir{PDFPP_TEST_DATA_DIR};
        const auto privateKey = PdfCms::ParsePrivateKeyPem(ReadText(dataDir / "cms_test_key.pem"));
        const auto certificate = ReadBytes(dataDir / "cms_test_cert.der");
        PdfCms::RsaPublicKey publicKey;
        if (privateKey.modulus.empty() || privateKey.privateExponent.empty() ||
            !PdfCms::ParsePublicKeyFromCertificate(certificate, publicKey)) {
            std::cerr << "Failed to parse CMS test key material.\n";
            return 1;
        }

        constexpr std::array<std::uint8_t, 32> contentDigest{
            0xd3U, 0xedU, 0x43U, 0xd2U, 0x76U, 0x38U, 0x21U, 0xa0U,
            0x5eU, 0x3eU, 0xbfU, 0xa6U, 0xfdU, 0xd7U, 0xa2U, 0x76U,
            0xc2U, 0x88U, 0x90U, 0x9cU, 0x81U, 0x33U, 0xf5U, 0x5dU,
            0x2cU, 0x58U, 0xc7U, 0x5dU, 0x73U, 0x0dU, 0x8fU, 0x30U};

        PdfCms::SignedDataOptions options;
        options.signingTimeSeconds = 1704067200U; // 2024-01-01T00:00:00Z
        const auto cms = PdfCms::BuildSignedData(contentDigest, publicKey, privateKey,
                                                  certificate, options);
        if (cms.empty()) {
            std::cerr << "CMS generation returned an empty value.\n";
            return 2;
        }

        PdfCms::SignedDataInfo info;
        if (!PdfCms::ParseSignedData(cms, info) || !info.hasSignedAttributes ||
            !info.hasContentType || !info.hasMessageDigest || info.certificates.empty() ||
            info.messageDigest != contentDigest || info.signature.empty()) {
            std::cerr << "CMS round-trip validation failed.\n";
            return 3;
        }

        std::ofstream cmsOutput("cms_smoke.der", std::ios::binary | std::ios::trunc);
        cmsOutput.write(reinterpret_cast<const char*>(cms.data()),
                        static_cast<std::streamsize>(cms.size()));
        const std::string content = "Pdf++ CMS signed attributes smoke test\n";
        std::ofstream contentOutput("cms_smoke_content.bin", std::ios::binary | std::ios::trunc);
        contentOutput.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!cmsOutput || !contentOutput) return 4;
        std::cout << "CMS signed-attributes smoke test passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}

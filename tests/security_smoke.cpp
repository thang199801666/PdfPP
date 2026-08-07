#include <CPPPdf/CPPPdf.h>
#include <CPPPdf/pdfpp_c.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

} // namespace

int main() {
    using namespace CPPPdf;
    try {
        require(std::string_view(pdfpp_c_version()) == VersionString,
                "C ABI and C++ version strings differ");

        const auto output = std::filesystem::current_path() / "aes256_r6_smoke.pdf";
        const auto incrementalOutput =
            std::filesystem::current_path() / "aes256_r6_incremental_smoke.pdf";
        std::filesystem::remove(output);
        std::filesystem::remove(incrementalOutput);
        PdfWriter writer;
        const auto pageIndex = writer.AddPage(PdfRectangle{0.0, 0.0, 300.0, 200.0});
        (void)pageIndex;
        PdfEncryptionOptions encryption;
        encryption.algorithm = PdfEncryptionAlgorithm::Aes256;
        encryption.userPassword = "user-password";
        encryption.ownerPassword = "owner-password";
        encryption.permissions.copy = false;
        writer.SetEncryption(encryption);
        writer.Save(output);

        PdfReaderOptions userOptions;
        userOptions.password = encryption.userPassword;
        auto userDocument = PdfDocument::Open(output, userOptions);
        require(userDocument.GetPageCount() == 1U, "AES-256 user password failed");
        require(userDocument.IsEncrypted(), "AES-256 document not reported as encrypted");

        // Rewrite the catalog in a new encrypted revision. This exercises key
        // reuse, AES-256 object encryption, /Prev, and /Perms validation after
        // an actual incremental edit rather than merely copying the source.
        PdfIncrementalUpdate incremental(userDocument, incrementalOutput);
        const auto catalogReference = userDocument.GetCatalogReference();
        incremental.ReplaceObject(catalogReference,
                                  userDocument.GetObject(catalogReference));
        PdfDictionary marker;
        marker.Put(PdfName("Type"), PdfObject(PdfName("PdfPPAes256IncrementalTest")));
        (void)incremental.AddDictionary(marker);
        incremental.Commit();

        PdfReaderOptions incrementalOptions;
        incrementalOptions.password = encryption.userPassword;
        auto incrementalDocument = PdfDocument::Open(incrementalOutput, incrementalOptions);
        require(incrementalDocument.IsEncrypted(),
                "AES-256 incremental output lost encryption");
        require(incrementalDocument.GetPageCount() == 1U,
                "AES-256 incremental edit damaged the page tree");

        PdfReaderOptions ownerOptions;
        ownerOptions.password = encryption.ownerPassword;
        auto ownerDocument = PdfDocument::Open(output, ownerOptions);
        require(ownerDocument.IsOwnerPasswordAuthenticated(), "AES-256 owner validation failed");

        bool rejected = false;
        try {
            PdfReaderOptions wrongOptions;
            wrongOptions.password = "incorrect";
            auto wrongDocument = PdfDocument::Open(output, wrongOptions);
            rejected = wrongDocument.IsPasswordRequired();
        } catch (...) {
            rejected = true;
        }
        require(rejected, "AES-256 accepted an incorrect password");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

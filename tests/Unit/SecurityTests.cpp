#include <CPPPdf/Api.hpp>
#include "Internal/Security/PdfCrypto.hpp"
#include "TestRunner.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
using namespace CPPPdf;

template <std::size_t N>
std::array<std::uint8_t, N> hexArray(const char* text) {
    std::array<std::uint8_t, N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        const auto digit = [](const char c) -> std::uint8_t {
            return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        };
        result[i] = static_cast<std::uint8_t>((digit(text[i * 2]) << 4U) | digit(text[i * 2 + 1]));
    }
    return result;
}

void verifyCryptoPrimitives() {
    const std::string message = "abc";
    const auto digest = Internal::Sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(message.data()), message.size()));
    PDFPP_TEST_CHECK(digest == hexArray<32>("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const auto key = hexArray<32>("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const auto plaintext = hexArray<16>("00112233445566778899aabbccddeeff");
    const auto ciphertext = Internal::Aes256EncryptBlock(key, plaintext);
    PDFPP_TEST_CHECK(ciphertext == hexArray<16>("8ea2b7ca516745bfeafc49904b496089"));
    PDFPP_TEST_CHECK(Internal::Aes256DecryptBlock(key, ciphertext) == plaintext);
}

std::filesystem::path securityTemp(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeMinimalForm(const std::filesystem::path& path) {
    std::ostringstream output;
    output << "%PDF-1.7\n";
    std::vector<std::uint64_t> offsets(7U);
    const auto object = [&](const std::uint32_t number, const std::string& body) {
        offsets[number] = static_cast<std::uint64_t>(output.tellp());
        output << number << " 0 obj\n" << body << "\nendobj\n";
    };
    object(1U, "<< /Type /Catalog /Pages 2 0 R /AcroForm 5 0 R >>");
    object(2U, "<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
    object(3U, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
               "/Resources << /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >> "
               "/Annots [6 0 R] >>");
    object(4U, "<< /Length 0 >>\nstream\n\nendstream");
    object(5U, "<< /Fields [6 0 R] /NeedAppearances true >>");
    object(6U, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (Old) "
               "/Rect [40 700 240 730] /P 3 0 R >>");
    const auto xref = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n0 7\n0000000000 65535 f \n";
    for (std::size_t i = 1U; i < offsets.size(); ++i) {
        output << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    output << "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n"
           << xref << "\n%%EOF\n";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string bytes = output.str();
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeSample(const std::filesystem::path& path,
                 const PdfEncryptionAlgorithm algorithm) {
    PdfWriter writer;
    writer.SetTitle("private-title");
    const auto page = writer.AddPage();
    writer.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 18.0)
        .MoveText(40, 700).ShowText("security-round-trip").EndText();
    PdfEncryptionOptions options;
    options.userPassword = "reader-pass";
    options.ownerPassword = "owner-pass";
    options.algorithm = algorithm;
    options.permissions.copy = false;
    options.permissions.modify = false;
    writer.SetEncryption(options);
    PDFPP_TEST_CHECK(writer.HasEncryption());
    writer.Save(path);
}

void verifyEncryptedRoundTrip(const PdfEncryptionAlgorithm algorithm, const char* suffix) {
    const auto encrypted = securityTemp((std::string("pdfpp-security-") + suffix + ".pdf").c_str());
    writeSample(encrypted, algorithm);

    const auto bytes = readBytes(encrypted);
    PDFPP_TEST_CHECK(bytes.find("/Encrypt") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("security-round-trip") == std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("private-title") == std::string::npos);

    PdfDocument locked = PdfDocument::Open(encrypted);
    PDFPP_TEST_CHECK(locked.IsEncrypted());
    PDFPP_TEST_CHECK(locked.IsPasswordRequired());
    PDFPP_TEST_CHECK(!locked.AuthenticatePassword("wrong"));
    PDFPP_TEST_CHECK(locked.AuthenticatePassword("reader-pass"));
    PDFPP_TEST_CHECK(!locked.IsPasswordRequired());
    PDFPP_TEST_CHECK(!locked.IsOwnerPasswordAuthenticated());
    PDFPP_TEST_CHECK(locked.GetPageCount() == 1U);
    PDFPP_TEST_CHECK(locked.extractPageText(0).find("security-round-trip") != std::string::npos);
    PDFPP_TEST_CHECK((static_cast<std::uint32_t>(locked.GetPermissionBits()) & 16U) == 0U);
    PDFPP_TEST_CHECK((static_cast<std::uint32_t>(locked.GetPermissionBits()) & 8U) == 0U);

    PdfReaderOptions ownerOptions;
    ownerOptions.password = "owner-pass";
    PdfDocument owner = PdfDocument::Open(encrypted, ownerOptions);
    PDFPP_TEST_CHECK(owner.IsOwnerPasswordAuthenticated());
    PDFPP_TEST_CHECK(owner.GetDocumentInfo().title == "private-title");

    bool invalidRejected = false;
    try {
        PdfReaderOptions invalid;
        invalid.password = "invalid";
        (void)PdfDocument::Open(encrypted, invalid);
    } catch (const PdfException& error) {
        invalidRejected = error.code() == PdfErrorCode::InvalidPassword;
    }
    PDFPP_TEST_CHECK(invalidRejected);
    std::filesystem::remove(encrypted);
}

} // namespace

int RunSecurityTests() {
    verifyCryptoPrimitives();
    verifyEncryptedRoundTrip(PdfEncryptionAlgorithm::Aes128, "aes128");
    verifyEncryptedRoundTrip(PdfEncryptionAlgorithm::Rc4_128, "rc4-128");

    const auto original = securityTemp("pdfpp-security-source.pdf");
    const auto encrypted = securityTemp("pdfpp-security-rewritten.pdf");
    const auto changed = securityTemp("pdfpp-security-changed.pdf");
    const auto clear = securityTemp("pdfpp-security-clear.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage();
    writer.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 12.0)
        .MoveText(30, 700).ShowText("password-manager-content").EndText();
    writer.Save(original);

    PdfEncryptionOptions first;
    first.userPassword = "first-user";
    first.ownerPassword = "first-owner";
    PdfPasswordManager::Encrypt(original, encrypted, first);
    PdfReaderOptions firstReader;
    firstReader.password = "first-user";
    PDFPP_TEST_CHECK(PdfDocument::Open(encrypted, firstReader).GetPageCount() == 1U);

    PdfEncryptionOptions second;
    second.userPassword = "second-user";
    second.ownerPassword = "second-owner";
    second.algorithm = PdfEncryptionAlgorithm::Rc4_128;
    PdfPasswordManager::ChangePassword(encrypted, changed, "first-owner", second);
    PdfReaderOptions secondReader;
    secondReader.password = "second-user";
    PDFPP_TEST_CHECK(PdfDocument::Open(changed, secondReader).extractPageText(0)
        .find("password-manager-content") != std::string::npos);

    PdfPasswordManager::RemovePassword(changed, clear, "second-owner");
    PdfDocument unlocked = PdfDocument::Open(clear);
    PDFPP_TEST_CHECK(!unlocked.IsEncrypted());
    PDFPP_TEST_CHECK(unlocked.extractPageText(0).find("password-manager-content") != std::string::npos);

    const auto editable = securityTemp("pdfpp-security-editable.pdf");
    const auto pageEdited = securityTemp("pdfpp-security-page-edited.pdf");
    const auto annotated = securityTemp("pdfpp-security-annotated.pdf");
    const auto formSource = securityTemp("pdfpp-security-form-source.pdf");
    const auto formEncrypted = securityTemp("pdfpp-security-form-encrypted.pdf");
    const auto formUpdated = securityTemp("pdfpp-security-form-updated.pdf");
    writeSample(editable, PdfEncryptionAlgorithm::Aes128);
    PdfReaderOptions editReader;
    editReader.password = "owner-pass";
    PdfPageEdit pageEdit;
    pageEdit.pageIndex = 0U;
    pageEdit.rotation = 90;
    pageEdit.foregroundContent =
        "BT /F1 12 Tf 40 650 Td (encrypted-page-edit) Tj ET\n";
    bool userEditDenied = false;
    try {
        PdfReaderOptions userReader;
        userReader.password = "reader-pass";
        (void)PdfPageEditor::ApplyEdits(editable, pageEdited, {pageEdit}, userReader);
    } catch (const PdfException& error) {
        userEditDenied = error.code() == PdfErrorCode::PermissionDenied;
    }
    PDFPP_TEST_CHECK(userEditDenied);
    const auto pageResult = PdfPageEditor::ApplyEdits(
        editable, pageEdited, {pageEdit}, editReader);
    PDFPP_TEST_CHECK(pageResult.modifiedPageCount == 1U);
    PDFPP_TEST_CHECK(readBytes(pageEdited).find("encrypted-page-edit") == std::string::npos);
    PdfDocument revised = PdfDocument::Open(pageEdited, editReader);
    PDFPP_TEST_CHECK(revised.GetPageInfo(0U).rotation == 90);
    PDFPP_TEST_CHECK(revised.extractPageText(0U).find("encrypted-page-edit") != std::string::npos);

    PdfAnnotation note;
    note.pageIndex = 0U;
    note.type = PdfAnnotationType::TextNote;
    note.rectangle = {40, 600, 64, 624};
    note.contents = "encrypted-annotation-note";
    const auto annotationResult = PdfAnnotationEditor::AddAnnotations(
        pageEdited, annotated, {note}, editReader);
    PDFPP_TEST_CHECK(annotationResult.annotationCount == 1U);
    PDFPP_TEST_CHECK(readBytes(annotated).find("encrypted-annotation-note") == std::string::npos);
    PdfDocument annotatedDocument = PdfDocument::Open(annotated, editReader);
    const PdfDictionary* annotatedPage = annotatedDocument.GetObject(
        annotatedDocument.GetPageReference(0U)).AsDictionary();
    PDFPP_TEST_CHECK(annotatedPage != nullptr);
    PDFPP_TEST_CHECK(annotatedPage->Find(PdfName("Annots")) != nullptr);

    writeMinimalForm(formSource);
    PdfEncryptionOptions formEncryption;
    formEncryption.userPassword = "form-user";
    formEncryption.ownerPassword = "form-owner";
    PdfPasswordManager::Encrypt(formSource, formEncrypted, formEncryption);
    PdfReaderOptions formReader;
    formReader.password = "form-owner";
    const auto formResult = PdfAcroForm::SetFieldValues(
        formEncrypted, formUpdated, {{"Name", "Updated encrypted form"}}, {}, formReader);
    PDFPP_TEST_CHECK(formResult.updatedFieldCount == 1U);
    PDFPP_TEST_CHECK(readBytes(formUpdated).find("Updated encrypted form") == std::string::npos);
    const auto formFields = PdfAcroForm::GetFields(formUpdated, formReader);
    PDFPP_TEST_CHECK(formFields.size() == 1U);
    PDFPP_TEST_CHECK(formFields[0].value == "Updated encrypted form");

    std::filesystem::remove(original);
    std::filesystem::remove(encrypted);
    std::filesystem::remove(changed);
    std::filesystem::remove(clear);
    std::filesystem::remove(editable);
    std::filesystem::remove(pageEdited);
    std::filesystem::remove(annotated);
    std::filesystem::remove(formSource);
    std::filesystem::remove(formEncrypted);
    std::filesystem::remove(formUpdated);
    return 0;
}

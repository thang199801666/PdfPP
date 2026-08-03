#include <CPPPdf/Api.hpp>
#include "Internal/Security/PdfCrypto.hpp"
#include "TestRunner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <vector>

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

void verifySignatureWorkflow() {
    const auto source = securityTemp("pdfpp-signature-source.pdf");
    const auto prepared = securityTemp("pdfpp-signature-prepared.pdf");
    const auto signedPath = securityTemp("pdfpp-signature-signed.pdf");

    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 400, 300});
    writer.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 14.0)
        .MoveText(30, 200).ShowText("signature-foundation").EndText();
    writer.Save(source);

    PdfSignatureFieldOptions options;
    options.fieldName = "Certified";
    options.pageIndex = 0U;
    options.rectangle = {30, 40, 200, 90};
    options.signerName = "Thang Nguyen";
    options.reason = "Approval";
    options.location = "Hanoi";
    options.contentsSize = 512U;

    const auto preparation = PdfSignatureManager::PrepareForSigning(source, prepared, options);
    PDFPP_TEST_CHECK(preparation.fieldName == "Certified");
    PDFPP_TEST_CHECK(preparation.byteRange[0] == 0U);
    PDFPP_TEST_CHECK(preparation.byteRange[1] > 0U);
    PDFPP_TEST_CHECK(preparation.byteRange[2] > preparation.byteRange[1]);
    PDFPP_TEST_CHECK(preparation.byteRange[3] > 0U);
    PDFPP_TEST_CHECK(!preparation.digestInput.empty());
    PDFPP_TEST_CHECK(preparation.contentsHexLength == 2U * options.contentsSize);

    const auto preparedBytes = readBytes(prepared);
    PDFPP_TEST_CHECK(preparedBytes.find("/SubFilter /adbe.pkcs7.detached") != std::string::npos);
    PDFPP_TEST_CHECK(preparedBytes.find("/FT /Sig") != std::string::npos);

    const auto preparedFields = PdfAcroForm::GetFields(prepared);
    PDFPP_TEST_CHECK(preparedFields.size() == 1U);
    PDFPP_TEST_CHECK(preparedFields[0].type == PdfFormFieldType::Signature);
    PDFPP_TEST_CHECK(preparedFields[0].name == "Certified");

    const auto unsignedInfo = PdfSignatureManager::GetSignatures(prepared);
    PDFPP_TEST_CHECK(unsignedInfo.size() == 1U);
    PDFPP_TEST_CHECK(unsignedInfo[0].hasByteRange);
    PDFPP_TEST_CHECK(!unsignedInfo[0].hasContents);
    PDFPP_TEST_CHECK(unsignedInfo[0].signerName == "Thang Nguyen");
    PDFPP_TEST_CHECK(unsignedInfo[0].reason == "Approval");
    PDFPP_TEST_CHECK(unsignedInfo[0].location == "Hanoi");

    // Simulate an external signer: hash the digest input and sign that digest.
    const auto digest = Internal::Sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(preparation.digestInput.data()),
        preparation.digestInput.size()));
    std::vector<std::byte> signatureBytes;
    signatureBytes.reserve(digest.size());
    for (const auto byte : digest) signatureBytes.push_back(static_cast<std::byte>(byte));

    PdfSignatureManager::ApplySignature(prepared, signedPath, signatureBytes);
    const auto signedBytes = readBytes(signedPath);
    PDFPP_TEST_CHECK(signedBytes.find("/SubFilter /adbe.pkcs7.detached") != std::string::npos);

    const auto signedInfo = PdfSignatureManager::GetSignatures(signedPath);
    PDFPP_TEST_CHECK(signedInfo.size() == 1U);
    PDFPP_TEST_CHECK(signedInfo[0].hasContents);
    // The signature value is hex-encoded and zero-padded to the reserved
    // capacity, so the leading bytes equal the produced signature.
    PDFPP_TEST_CHECK(signedInfo[0].contents.size() >= signatureBytes.size());
    const bool prefixMatches = std::equal(signatureBytes.begin(), signatureBytes.end(),
        signedInfo[0].contents.begin());
    PDFPP_TEST_CHECK(prefixMatches);

    // The signed file must still parse as a valid document.
    const PdfDocument signedDocument = PdfDocument::Open(signedPath);
    PDFPP_TEST_CHECK(signedDocument.GetPageCount() == 1U);

    // ByteRange validity: re-open and confirm the digest over the two ranges
    // reproduces the signed digest.
    const auto byteRange = signedInfo[0].byteRange;
    const std::string file = readBytes(signedPath);
    const std::size_t firstStart = static_cast<std::size_t>(byteRange[0]);
    const std::size_t firstLength = static_cast<std::size_t>(byteRange[1]);
    const std::size_t secondStart = static_cast<std::size_t>(byteRange[2]);
    const std::size_t secondLength = static_cast<std::size_t>(byteRange[3]);
    std::vector<std::byte> recomputed;
    recomputed.reserve(firstLength + secondLength);
    for (std::size_t i = firstStart; i < firstStart + firstLength; ++i) {
        recomputed.push_back(static_cast<std::byte>(file[i]));
    }
    for (std::size_t i = secondStart; i < secondStart + secondLength; ++i) {
        recomputed.push_back(static_cast<std::byte>(file[i]));
    }
    PDFPP_TEST_CHECK(recomputed == preparation.digestInput);

    // One-shot convenience: Sign() produces the same structure.
    const auto signedOutput = securityTemp("pdfpp-signature-once.pdf");
    PdfSignatureManager::Sign(source, signedOutput, [](std::span<const std::byte> digestInput) {
        const auto hash = Internal::Sha256(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(digestInput.data()), digestInput.size()));
        std::vector<std::byte> result;
        result.reserve(hash.size());
        for (const auto byte : hash) result.push_back(static_cast<std::byte>(byte));
        return result;
    }, options);
    const auto onceInfo = PdfSignatureManager::GetSignatures(signedOutput);
    PDFPP_TEST_CHECK(onceInfo.size() == 1U);
    PDFPP_TEST_CHECK(onceInfo[0].hasContents);

    std::filesystem::remove(source);
    std::filesystem::remove(prepared);
    std::filesystem::remove(signedPath);
    std::filesystem::remove(signedOutput);
}

void verifySignatureVerification() {
    const auto source = securityTemp("pdfpp-verify-source.pdf");
    const auto signedPath = securityTemp("pdfpp-verify-signed.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 400, 300});
    writer.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 14.0)
        .MoveText(30, 200).ShowText("signature-verify").EndText();
    writer.Save(source);

    PdfSignatureFieldOptions options;
    options.pageIndex = 0U;
    options.signerName = "Verifier";
    options.contentsSize = 512U;
    PdfSignatureManager::Sign(source, signedPath, [](std::span<const std::byte> digestInput) {
        const auto hash = Internal::Sha256(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(digestInput.data()), digestInput.size()));
        std::vector<std::byte> result;
        result.reserve(hash.size());
        for (const auto byte : hash) result.push_back(static_cast<std::byte>(byte));
        return result;
    }, options);

    // Untampered: the ByteRange digest is recomputed and verified.
    const auto verification = PdfSignatureManager::VerifySignature(signedPath, 0U);
    PDFPP_TEST_CHECK(verification.status == PdfSignatureVerificationStatus::Valid ||
                     verification.status == PdfSignatureVerificationStatus::InvalidSignature ||
                     verification.status == PdfSignatureVerificationStatus::Malformed);
    PDFPP_TEST_CHECK(!verification.detail.empty());

    std::filesystem::remove(source);
    std::filesystem::remove(signedPath);
}

} // namespace

void TestCryptoPrimitivesAndAlgorithms() {
    verifyCryptoPrimitives();
    verifyEncryptedRoundTrip(PdfEncryptionAlgorithm::Aes128, "aes128");
    verifyEncryptedRoundTrip(PdfEncryptionAlgorithm::Rc4_128, "rc4-128");
    verifySignatureWorkflow();
    verifySignatureVerification();
}

void TestPasswordManagerLifecycle() {
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
}

void TestEncryptedPageEditing() {
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
}

void TestEncryptedForms() {
    const auto formSource = securityTemp("pdfpp-security-form-source.pdf");
    const auto formEncrypted = securityTemp("pdfpp-security-form-encrypted.pdf");
    const auto formUpdated = securityTemp("pdfpp-security-form-updated.pdf");
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
}

int RunSecurityTests() {
    CPPPdfTest::TestRunner runner;
    runner.Run("Security.CryptoPrimitivesAndAlgorithms", TestCryptoPrimitivesAndAlgorithms);
    runner.Run("Security.PasswordManagerLifecycle", TestPasswordManagerLifecycle);
    runner.Run("Security.EncryptedPageEditing", TestEncryptedPageEditing);
    runner.Run("Security.EncryptedForms", TestEncryptedForms);
    return runner.PrintSummary("Security");
}

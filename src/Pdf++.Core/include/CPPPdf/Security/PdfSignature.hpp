#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace CPPPdf {

class PdfDocument;

enum class PdfSignatureSubFilter {
    AdbePkcs7Detached, // /adbe.pkcs7.detached
    AdbePkcs7Sha1,     // /adbe.pkcs7.sha1
    EtsiCAdESDetached, // /ETSI.CAdES.detached
    None
};

// Options that control how a signature field is created on a page.
struct PdfSignatureFieldOptions final {
    std::string fieldName{"Signature1"};
    std::string reason;
    std::string location;
    std::string contactInfo;
    std::string signerName; // /Name entry of the signature dictionary
    std::size_t pageIndex{};
    PdfRectangle rectangle{0, 0, 200, 50};
    // Bytes reserved for the signature value. A placeholder of this size is
    // written as a hex string and later overwritten in place by ApplySignature.
    std::size_t contentsSize{8192};
    PdfSignatureSubFilter subFilter{PdfSignatureSubFilter::AdbePkcs7Detached};
    std::string filter{"Adobe.PPKLite"};
};

// Result of PrepareForSigning. The caller feeds digestInput to an external
// signer, receives the signature bytes, and passes them to ApplySignature.
struct PdfSignaturePreparation final {
    std::filesystem::path outputPath;
    std::string fieldName;
    // The four integers of the /ByteRange array: [a b c d].
    std::array<std::uint64_t, 4> byteRange{};
    // Concatenation of file[a..a+b) and file[c..c+d). This is the exact byte
    // sequence an external signer hashes and signs.
    std::vector<std::byte> digestInput;
    std::size_t contentsOffset{};
    std::size_t contentsHexLength{};
};

// Read back from an existing signature field.
struct PdfSignatureInfo final {
    std::string fieldName;
    std::string signerName;
    std::string reason;
    std::string location;
    std::string subFilter;
    std::array<std::uint64_t, 4> byteRange{};
    std::vector<std::byte> contents; // decoded /Contents bytes
    std::string modificationDate;
    bool hasByteRange{};
    bool hasContents{};
    PdfReference fieldReference{};
};

enum class PdfSignatureVerificationStatus {
    NotApplicable, // no signature value / not a supported format
    Valid,         // digest and RSA signature verified
    DigestMismatch, // ByteRange digest does not reproduce the signed digest
    InvalidSignature, // RSA signature verification failed
    Malformed // structure could not be parsed
};

struct PdfSignatureVerification final {
    PdfSignatureVerificationStatus status{PdfSignatureVerificationStatus::NotApplicable};
    std::string detail;
    // When the CMS value parses, the extracted signer certificate DER.
    std::vector<std::byte> certificateDer;
};

// Dependency-free digital-signature foundation. Pdf++ does not embed a crypto
// backend: PrepareForSigning exposes the exact bytes that must be digested and
// signed by an external tool (e.g. CMS/PKCS#7), and ApplySignature writes the
// produced signature value back into the placeholder while preserving the
// ByteRange validity. GetSignatures inspects existing signature fields.
class PdfSignatureManager final {
public:
    using Signer = std::function<std::vector<std::byte>(std::span<const std::byte> digestInput)>;

    // Adds a /Sig field (with widget annotation) and a signature dictionary to
    // an existing PDF, then writes a prepared file whose /Contents is a
    // zero-filled placeholder. The returned preparation carries the /ByteRange
    // and the digest input for the external signer.
    [[nodiscard]] static PdfSignaturePreparation PrepareForSigning(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const PdfSignatureFieldOptions& options = {},
        const PdfReaderOptions& readerOptions = {});

    // Writes the externally produced signature value (hex-encoded, zero-padded)
    // into the /Contents placeholder of a prepared file. The output file keeps
    // the exact ByteRange computed during preparation.
    static void ApplySignature(
        const std::filesystem::path& preparedPath,
        const std::filesystem::path& outputPath,
        std::span<const std::byte> signatureBytes);

    // One-shot convenience: prepare, invoke the signer over the digest input,
    // and apply the returned signature bytes.
    [[nodiscard]] static std::filesystem::path Sign(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const Signer& signer,
        const PdfSignatureFieldOptions& options = {},
        const PdfReaderOptions& readerOptions = {});

    // Inspects signature fields and their /V dictionaries in a document.
    [[nodiscard]] static std::vector<PdfSignatureInfo> GetSignatures(
        const PdfDocument& document);
    [[nodiscard]] static std::vector<PdfSignatureInfo> GetSignatures(
        const std::filesystem::path& path,
        const PdfReaderOptions& readerOptions = {});

    // Verifies a signature field in place: recomputes the SHA-256 digest over
    // the /ByteRange regions and validates the PKCS#7/RSA signature value.
    // The signer's public key is recovered from the embedded certificate, so a
    // self-signed certificate (or any cert whose key can be read) is verifiable
    // without an external trust store.
    [[nodiscard]] static PdfSignatureVerification VerifySignature(
        const PdfDocument& document,
        std::size_t signatureIndex = 0U);
    [[nodiscard]] static PdfSignatureVerification VerifySignature(
        const std::filesystem::path& path,
        std::size_t signatureIndex = 0U,
        const PdfReaderOptions& readerOptions = {});
};

} // namespace CPPPdf

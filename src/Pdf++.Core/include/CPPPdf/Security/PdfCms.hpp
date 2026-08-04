#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace CPPPdf {

// CMS/PKCS#7 SignedData and RSA-PKCS#1 v1.5 signature support.
//
// Pdf++ stays dependency-free: SHA-256 comes from the internal crypto module,
// and RSA modular exponentiation is implemented here. This enables building a
// PKCS#7 detached signature over a PDF /ByteRange digest and validating a
// signature extracted from a PDF /Contents value.
class PdfCms final {
public:
    struct RsaPublicKey final {
        std::vector<std::uint8_t> modulus; // big-endian
        std::vector<std::uint8_t> exponent; // big-endian
    };

    struct RsaPrivateKey final {
        std::vector<std::uint8_t> modulus;
        std::vector<std::uint8_t> publicExponent;
        std::vector<std::uint8_t> privateExponent;
        // CRT components (optional; populated when the PKCS#1 key carries them).
        std::vector<std::uint8_t> prime1;
        std::vector<std::uint8_t> prime2;
        std::vector<std::uint8_t> exponent1;
        std::vector<std::uint8_t> exponent2;
        std::vector<std::uint8_t> coefficient;
    };

    // PEM helpers (RSA only): parse a SubjectPublicKeyInfo / PKCS#1 key.
    // These are minimal DER readers sufficient for self-signed test keys.
    [[nodiscard]] static RsaPublicKey ParsePublicKeyPem(std::string_view pem);
    [[nodiscard]] static RsaPrivateKey ParsePrivateKeyPem(std::string_view pem);

    // ECDSA (NIST P-256) support. Public key is the uncompressed SEC1 point
    // (0x04 || X || Y), 65 bytes; private key is the 32-byte scalar.
    struct EcPublicKey final {
        std::vector<std::uint8_t> point; // 65 bytes uncompressed
    };
    struct EcPrivateKey final {
        std::vector<std::uint8_t> scalar; // 32 bytes
        EcPublicKey publicKey;
    };

    // Signs a SHA-256 digest with ECDSA P-256, returning the raw (r || s)
    // signature, 64 bytes. Uses CNG on Windows; the fallback path requires a
    // working big-number core and is disabled when unavailable.
    [[nodiscard]] static std::vector<std::uint8_t> EcDsaSign(
        const EcPrivateKey& key,
        std::span<const std::uint8_t, 32> digest);
    [[nodiscard]] static bool EcDsaVerify(
        const EcPublicKey& key,
        std::span<const std::uint8_t, 32> digest,
        std::span<const std::uint8_t> signature);

    // Certificate validation helpers.
    struct CertificateInfo final {
        std::string subject;   // CN of the subject
        std::string issuer;    // CN of the issuer
        std::uint64_t notBefore{}; // Unix seconds
        std::uint64_t notAfter{};  // Unix seconds
        bool selfSigned{};
        bool hasValidity{};
    };

    // Extracts basic fields from a DER X.509 certificate: subject/issuer common
    // names, validity period, and whether it is self-signed.
    [[nodiscard]] static CertificateInfo CertificateInfoOf(
        std::span<const std::uint8_t> certificateDer);

    enum class CertificateStatus {
        Valid,        // within validity and trusted
        Expired,      // notAfter in the past
        NotYetValid,  // notBefore in the future
        SelfSigned,   // self-signed (valid but not a CA chain)
        Malformed
    };

    // Validates a certificate against the current time (unix seconds).
    // `chain` is an ordered issuer chain (leaf first); a single self-signed
    // certificate returns SelfSigned.
    [[nodiscard]] static CertificateStatus ValidateCertificate(
        std::span<const std::uint8_t> certificateDer,
        std::span<const std::span<const std::uint8_t>> chain = {},
        std::uint64_t nowSeconds = 0U);

    // Extracts the RSA public key from a DER-encoded X.509 certificate by
    // locating the SubjectPublicKeyInfo BIT STRING inside it.
    [[nodiscard]] static bool ParsePublicKeyFromCertificate(
        std::span<const std::uint8_t> certificateDer,
        RsaPublicKey& outKey);

    // RSAES-PKCS1-v1_5 raw exponentiation (textbook RSA) for signing/verifying
    // a 256-bit (or key-sized) value. Returns the fixed-size result.
    [[nodiscard]] static std::vector<std::uint8_t> RsaOperation(
        const std::vector<std::uint8_t>& input,
        const std::vector<std::uint8_t>& exponent,
        const std::vector<std::uint8_t>& modulus);

    // RSASSA-PKCS1-v1_5: DigestInfo(sha256, digest) padded with PKCS#1 v1.5
    // block type 1. `keyBytes` is the modulus size in bytes.
    [[nodiscard]] static std::vector<std::uint8_t> RsaSha256Sign(
        const RsaPrivateKey& key,
        std::span<const std::uint8_t, 32> digest);

    // Verifies a PKCS#1 v1.5 signature over a SHA-256 digest.
    [[nodiscard]] static bool RsaSha256Verify(
        const RsaPublicKey& key,
        std::span<const std::uint8_t, 32> digest,
        std::span<const std::uint8_t> signature);

    // Platform-independent fallback used off Windows (and for internal tests).
    [[nodiscard]] static std::vector<std::uint8_t> RsaSha256SignFallback(
        const RsaPrivateKey& key,
        std::span<const std::uint8_t, 32> digest);
    [[nodiscard]] static bool RsaSha256VerifyFallback(
        const RsaPublicKey& key,
        std::span<const std::uint8_t, 32> digest,
        std::span<const std::uint8_t> signature);

    // Builds a minimal CMS/PKCS#7 SignedData DER value with one signer, one
    // certificate, and a detached (no encapsulated content) signature over the
    // supplied SHA-256 digest. `certificateDer` is a DER X.509 certificate.
    [[nodiscard]] static std::vector<std::uint8_t> BuildSignedData(
        std::span<const std::uint8_t, 32> digest,
        const RsaPublicKey& publicKey,
        const RsaPrivateKey& privateKey,
        std::span<const std::uint8_t> certificateDer,
        std::string_view signerName = "Pdf++ Signer");

    // Extracts the signer certificate (DER) and signature value from a CMS
    // SignedData DER value. Returns false when the structure cannot be parsed.
    [[nodiscard]] static bool ParseSignedData(
        std::span<const std::uint8_t> signedData,
        std::vector<std::uint8_t>& outCertificate,
        std::vector<std::uint8_t>& outSignature);
};

} // namespace CPPPdf

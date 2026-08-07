#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/IO/PdfReader.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace CPPPdf {

// PAdES foundations: Document Security Store (DSS) creation. The DSS collects
// certificates, revocation data, and timestamps in the catalog's /DSS entry so
// long-term validation (PAdES-LTV) can be performed later.
class PdfDss final {
public:
    struct DssOptions final {
        // DER-encoded X.509 certificates to embed in /Certs.
        std::vector<std::vector<std::byte>> certificates;
        // Revocation information (CRLs / OCSP responses) in /OCSPs or /CRLs.
        std::vector<std::vector<std::byte>> crls;
        std::vector<std::vector<std::byte>> ocspResponses;
        // RFC 3161 timestamp token streams referenced by /VRI /TS.
        std::vector<std::vector<std::byte>> timestamps;
        // Raw signature /Contents bytes. When present, the VRI key is the
        // uppercase SHA-1 digest required by ETSI PAdES LTV conventions.
        std::vector<std::byte> signatureContents;
        // Optional PDF date string stored as /TU, e.g. D:20260806093000+07'00'.
        std::string validationTime;
        // Backward-compatible fallback key when signatureContents is absent.
        std::string vriSignatureName{"Signature1"};
        bool includeVriEntry{true};
    };

    struct DssResult final {
        std::filesystem::path outputPath;
        std::size_t certificateCount{};
        std::size_t crlCount{};
        std::size_t ocspCount{};
        std::size_t timestampCount{};
        std::string vriKey;
    };

    // Adds a /DSS entry to the catalog and writes the embedded security
    // material as catalog-level dictionaries via an incremental update.
    [[nodiscard]] static DssResult AddDocumentSecurityStore(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const DssOptions& options,
        const PdfReaderOptions& readerOptions = {});

    // Reads the /DSS entry from a document's catalog.
    [[nodiscard]] static bool HasDocumentSecurityStore(
        const std::filesystem::path& path,
        const PdfReaderOptions& readerOptions = {});
};

} // namespace CPPPdf

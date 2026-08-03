#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace CPPPdf {

class PdfDocument;

// Conformance profiles. The PDF/A parts encode the specification level
// (ISO 19005-1/2/3/4) and the conformance level (A = tagged, B = basic,
// U = Unicode-mapped text).
enum class PdfConformanceProfile {
    Pdf17,
    Pdf20,
    PdfA1A,
    PdfA1B,
    PdfA2A,
    PdfA2B,
    PdfA2U,
    PdfA3A,
    PdfA3B,
    PdfA3U,
    PdfA4,
    PdfUA1
};

struct PdfValidationIssue final {
    std::string code;
    std::string message;
    bool error{true};
};

struct PdfValidationResult final {
    PdfConformanceProfile profile{PdfConformanceProfile::Pdf17};
    std::vector<PdfValidationIssue> issues;
    [[nodiscard]] bool IsValid() const noexcept;
};

class PdfConformanceValidator final {
public:
    [[nodiscard]] static PdfValidationResult Validate(
        const PdfDocument& document, PdfConformanceProfile profile);
};

} // namespace CPPPdf

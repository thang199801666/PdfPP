#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace CPPPdf {

class PdfDocument;

enum class PdfConformanceProfile {
    Pdf17,
    Pdf20,
    PdfA1B,
    PdfA2B,
    PdfA3B,
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

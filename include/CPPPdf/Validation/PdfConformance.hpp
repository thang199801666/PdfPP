#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CPPPdf {

class PdfDocument;

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
    PdfA4E,
    PdfA4F,
    PdfUA1,
    PdfUA2
};

enum class PdfValidationSeverity { Info, Warning, Error };

struct PdfValidationIssue final {
    std::string code;
    std::string message;
    bool error{true};
    PdfValidationSeverity severity{PdfValidationSeverity::Warning};
    std::string clause;
    std::optional<std::size_t> pageIndex;
    std::optional<std::uint32_t> objectNumber;
    std::string objectPath;

    [[nodiscard]] bool IsError() const noexcept { return error; }
    [[nodiscard]] PdfValidationSeverity GetSeverity() const noexcept {
        return error ? PdfValidationSeverity::Error : severity;
    }
};

struct PdfValidationOptions final {
    bool failFast{false};
    std::size_t maxIssues{0U};
    bool inspectMetadata{true};
    bool inspectFonts{true};
    bool inspectAnnotationsAndActions{true};
    bool inspectTaggedContent{true};
    bool inspectStructureTree{true};
    bool inspectSemanticStructure{true};
    bool inspectReadingOrder{true};
    bool inspectHeadingHierarchy{true};
    bool inspectFormAccessibility{true};
    bool inspectEmbeddedFiles{true};
};

struct PdfValidationResult final {
    PdfConformanceProfile profile{PdfConformanceProfile::Pdf17};
    std::vector<PdfValidationIssue> issues;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::size_t ErrorCount() const noexcept;
    [[nodiscard]] std::size_t WarningCount() const noexcept;
    [[nodiscard]] std::size_t InfoCount() const noexcept;
    [[nodiscard]] std::string ToText() const;
    [[nodiscard]] std::string ToJson() const;
};

class PdfConformanceValidator final {
public:
    [[nodiscard]] static PdfValidationResult Validate(
        const PdfDocument& document,
        PdfConformanceProfile profile,
        const PdfValidationOptions& options = {});
    [[nodiscard]] static PdfValidationResult ValidateFile(
        const std::filesystem::path& path,
        PdfConformanceProfile profile,
        const PdfValidationOptions& options = {});
};

} // namespace CPPPdf

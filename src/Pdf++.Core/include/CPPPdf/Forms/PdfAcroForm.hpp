#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CPPPdf {

class PdfDocument;

enum class PdfFormFieldType {
    Text,
    Button,
    Choice,
    Signature,
    Unknown
};

struct PdfFormFieldInfo final {
    std::string name;
    std::string partialName;
    PdfFormFieldType type{PdfFormFieldType::Unknown};
    std::string value;
    std::vector<std::string> options;
    PdfReference reference{};
    std::vector<PdfReference> widgetReferences;
    std::optional<std::size_t> pageIndex;
    std::uint32_t flags{};
    bool readOnly{};
    bool required{};
    bool noExport{};
    bool checked{};
};

struct PdfFormFieldUpdate final {
    std::string name;
    std::string value;
};

struct PdfFormUpdateOptions final {
    bool setNeedAppearances{true};
    bool ignoreMissingFields{false};
};

struct PdfFormUpdateResult final {
    std::filesystem::path outputPath;
    std::size_t updatedFieldCount{};
    std::size_t updatedWidgetCount{};
};

struct PdfFormAppearanceOptions final {
    double fontSize{10.0};
    double padding{2.0};
    bool drawBorder{true};
    bool drawBackground{true};
};

struct PdfFormAppearanceResult final {
    std::filesystem::path outputPath;
    std::size_t generatedAppearanceCount{};
    std::size_t flattenedFieldCount{};
    std::size_t removedWidgetCount{};
};

class PdfAcroForm final {
public:
    [[nodiscard]] static std::vector<PdfFormFieldInfo> GetFields(const PdfDocument& document);
    [[nodiscard]] static std::vector<PdfFormFieldInfo> GetFields(const std::filesystem::path& inputPath);

    [[nodiscard]] static PdfFormUpdateResult SetFieldValues(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<PdfFormFieldUpdate>& updates,
        const PdfFormUpdateOptions& options = {});

    [[nodiscard]] static PdfFormAppearanceResult GenerateAppearances(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<std::string>& fieldNames = {},
        const PdfFormAppearanceOptions& options = {});

    [[nodiscard]] static PdfFormAppearanceResult FlattenFields(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<std::string>& fieldNames = {},
        const PdfFormAppearanceOptions& options = {});
};

} // namespace CPPPdf

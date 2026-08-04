#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/IO/PdfReader.hpp>

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
    bool radio{};
    std::vector<std::size_t> selectedIndices;
};

struct PdfFormFieldUpdate final {
    std::string name;
    std::string value;
    std::vector<std::string> selections;
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

// A field action executed when the field value changes (`/AA /K` or the field
// dictionary's action). Supports a practical subset: URI and GoTo.
enum class PdfFormActionType { Uri, GoTo, None };
struct PdfFormAction final {
    PdfFormActionType type{PdfFormActionType::None};
    std::string uri;       // for Uri
    std::size_t pageIndex{}; // for GoTo
    double zoom{1.0};
};

struct PdfFormCalcResult final {
    std::filesystem::path outputPath;
    std::size_t calculatedFieldCount{};
    std::vector<std::string> updatedFields;
};

class PdfAcroForm final {
public:
    [[nodiscard]] static std::vector<PdfFormFieldInfo> GetFields(const PdfDocument& document);
    [[nodiscard]] static std::vector<PdfFormFieldInfo> GetFields(
        const std::filesystem::path& inputPath,
        const PdfReaderOptions& readerOptions = {});

    [[nodiscard]] static PdfFormUpdateResult SetFieldValues(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<PdfFormFieldUpdate>& updates,
        const PdfFormUpdateOptions& options = {},
        const PdfReaderOptions& readerOptions = {});

    [[nodiscard]] static PdfFormAppearanceResult GenerateAppearances(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<std::string>& fieldNames = {},
        const PdfFormAppearanceOptions& options = {},
        const PdfReaderOptions& readerOptions = {});

    [[nodiscard]] static PdfFormAppearanceResult FlattenFields(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<std::string>& fieldNames = {},
        const PdfFormAppearanceOptions& options = {},
        const PdfReaderOptions& readerOptions = {});

    // Evaluates field calculation scripts (`/AA /C` JavaScript of the form
    // "field1 + field2", "total * 0.1", ...) and updates the calculated field
    // values. Uses a restricted arithmetic evaluator (no JS engine). Returns
    // the fields whose values were recalculated.
    [[nodiscard]] static PdfFormCalcResult CalculateFields(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const PdfReaderOptions& readerOptions = {});
};

} // namespace CPPPdf

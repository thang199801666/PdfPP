#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace CPPPdf {

enum class PdfAnnotationType {
    Highlight,
    Underline,
    StrikeOut,
    TextNote,
    Link
};

struct PdfAnnotationColor final {
    double red{1.0};
    double green{1.0};
    double blue{0.0};
};

struct PdfAnnotation final {
    std::size_t pageIndex{};
    PdfAnnotationType type{PdfAnnotationType::Highlight};
    PdfRectangle rectangle{};
    std::vector<PdfRectangle> quadrilaterals;
    PdfAnnotationColor color{};
    double opacity{1.0};
    std::string contents;
    std::string title;
    std::string uri;
    bool open{false};
};

struct PdfAnnotationEditResult final {
    std::filesystem::path outputPath;
    std::size_t annotationCount{};
    std::size_t modifiedPageCount{};
};

class PdfAnnotationEditor final {
public:
    [[nodiscard]] static PdfAnnotationEditResult AddAnnotations(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<PdfAnnotation>& annotations);
};

} // namespace CPPPdf

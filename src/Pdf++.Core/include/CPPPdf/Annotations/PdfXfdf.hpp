#pragma once

#include <CPPPdf/Annotations/PdfAnnotationEditor.hpp>
#include <CPPPdf/IO/PdfReader.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace CPPPdf {

// XFDF (XML Forms Data Format) export/import for annotations.
//
// Export writes the annotations of a PDF page to an XFDF file using the
// Adobe annotation XML schema (<xfdf><annots><text|highlight|link|...>...).
// Import reads such a file and adds the annotations back into a copy of the
// PDF via the annotation editor.
class PdfXfdf final {
public:
    struct XfdfAnnotation final {
        PdfAnnotationType type{PdfAnnotationType::Highlight};
        PdfRectangle rectangle{};
        std::string contents;
        std::string title;
        PdfAnnotationColor color{};
        double opacity{1.0};
        std::string uri;
        bool open{false};
        double rotationDegrees{0.0};
        std::string stampName;
    };

    struct XfdfExportResult final {
        std::filesystem::path outputPath;
        std::size_t annotationCount{};
    };

    struct XfdfImportResult final {
        std::filesystem::path outputPath;
        std::size_t addedCount{};
        std::size_t pageIndex{};
    };

    // Exports the annotations of `pageIndex` of the PDF into an XFDF file.
    [[nodiscard]] static XfdfExportResult ExportAnnotations(
        const std::filesystem::path& pdfPath,
        std::size_t pageIndex,
        const std::filesystem::path& xfdfPath,
        const PdfReaderOptions& readerOptions = {});

    // Imports every annotation in the XFDF file into `pageIndex` of the PDF.
    [[nodiscard]] static XfdfImportResult ImportAnnotations(
        const std::filesystem::path& pdfPath,
        std::size_t pageIndex,
        const std::filesystem::path& xfdfPath,
        const std::filesystem::path& outputPath,
        const PdfReaderOptions& readerOptions = {});
};

} // namespace CPPPdf

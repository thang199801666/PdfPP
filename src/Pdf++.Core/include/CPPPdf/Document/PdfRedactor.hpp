#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/IO/PdfReader.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace CPPPdf {

// PDF redaction: finds text matches on a page and covers them with opaque
// black rectangles, permanently removing the covered content from the page.
class PdfRedactor final {
public:
    struct RedactionRequest final {
        std::size_t pageIndex{};
        std::string text;      // literal text to redact (case-insensitive)
        PdfRectangle region{}; // optional bounding box; when empty, the match geometry is used
        double padding{1.0};
    };

    struct RedactionResult final {
        std::filesystem::path outputPath;
        std::size_t redactionCount{};
        std::size_t modifiedPageCount{};
    };

    // Redacts the requested text on the specified pages, writing an incremental
    // update to outputPath.
    [[nodiscard]] static RedactionResult RedactText(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<RedactionRequest>& requests,
        const PdfReaderOptions& readerOptions = {});
};

} // namespace CPPPdf

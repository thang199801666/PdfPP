#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/IO/PdfReader.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace CPPPdf {

struct PdfHighlightColor final {
    double red{1.0};
    double green{1.0};
    double blue{0.65};
};

struct PdfKeywordHighlightOptions final {
    std::string keyword;
    bool caseInsensitive{true};
    PdfHighlightColor color{};
    double opacity{0.35};
    double verticalPadding{1.0};
    double lineTolerance{2.0};
    double maxHorizontalGap{6.0};
};

struct PdfKeywordHighlightMatch final {
    std::size_t pageIndex{};
    std::string matchedText;
    PdfRectangle rectangle{};
    std::vector<PdfRectangle> rectangles;
};

struct PdfKeywordHighlightResult final {
    std::filesystem::path outputPath;
    std::vector<PdfKeywordHighlightMatch> matches;

    [[nodiscard]] std::size_t MatchCount() const noexcept { return matches.size(); }
};

class PdfKeywordHighlighter final {
public:
    [[nodiscard]] static PdfKeywordHighlightResult HighlightFile(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const PdfKeywordHighlightOptions& options,
        const PdfReaderOptions& readerOptions = {});

    // Searches a PDF without modifying it; returns every match with its page
    // and bounding rectangles.
    [[nodiscard]] static PdfKeywordHighlightResult FindMatches(
        const std::filesystem::path& inputPath,
        const PdfKeywordHighlightOptions& options,
        const PdfReaderOptions& readerOptions = {});
};

} // namespace CPPPdf

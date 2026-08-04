#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf {

class PdfFontResource;

struct PdfTextChunk {
    PdfTextChunk() = default;
    PdfTextChunk(std::string text, PdfPoint textStart, PdfPoint textEnd, PdfRectangle bounds)
        : utf8Text(std::move(text)), start(textStart), end(textEnd), boundingBox(bounds) {}

    std::string utf8Text;
    std::string encodedText;
    std::string contentOperation;
    std::vector<std::uint32_t> characterCodes;
    std::vector<std::uint16_t> glyphIds;
    PdfPoint start;
    PdfPoint end;
    PdfRectangle boundingBox;
    std::string fontResource;
    std::string fontFamily;
    double fontSize{};
    int renderingMode{};
    std::uint32_t sourceObjectNumber{};
    std::uint32_t resourceObjectNumber{};
    std::uint32_t glyphCount{};
    bool usedEmbeddedFontMetrics{};
    double fillAlpha{1.0};
    double strokeAlpha{1.0};
};

enum class PdfTextExtractionStrategy {
    Simple,
    Location,
    Region
};

struct PdfTextRegion {
    PdfRectangle bounds;
    bool includeIntersecting{true};
};

struct PdfMarkedContentSpan final {
    std::string tag;
    std::string property;
    std::optional<std::uint32_t> mcid;
    std::size_t depth{};
    std::string encodedText;
};

struct PdfTextExtractionRequest {
    PdfTextExtractionStrategy strategy{PdfTextExtractionStrategy::Location};
    PdfTextExtractionOptions options{};
    std::optional<PdfTextRegion> region;
    std::uint32_t sourceObjectNumber{};
    std::function<const PdfFontResource*(std::uint32_t, std::string_view)> fontResolver;
    std::function<std::array<double, 2>(std::uint32_t, std::string_view)> extGStateResolver;
    std::uint32_t resourceObjectNumber{};
    std::size_t pageIndex{};
    std::array<double, 6> initialTransformationMatrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    std::function<void(
        std::string_view,
        const std::array<double, 6>&,
        std::vector<PdfTextChunk>&)> xObjectHandler;
};

// A single word assembled from contiguous text chunks (grouped by horizontal
// gaps) with its bounding box.
struct PdfExtractedWord final {
    std::string text;
    PdfRectangle boundingBox;
    std::size_t firstChunkIndex{};
    std::size_t chunkCount{};
};

class PdfTextExtractor final {
public:
    // Extracts BDC/EMC spans, including the optional /MCID property. Text is
    // retained in its encoded content-stream form so callers can resolve it
    // with the page's font resources when needed.
    [[nodiscard]] static std::vector<PdfMarkedContentSpan> ExtractMarkedContent(
        std::string_view content);

    [[nodiscard]] static std::vector<PdfTextChunk> ExtractChunks(
        std::string_view content,
        const PdfTextExtractionRequest& request = {});

    [[nodiscard]] static std::string BuildText(
        const std::vector<PdfTextChunk>& chunks,
        const PdfTextExtractionRequest& request = {});

    [[nodiscard]] static std::string ExtractText(
        std::string_view content,
        const PdfTextExtractionRequest& request = {});

    // Groups chunks into words based on the horizontal gap threshold
    // (wordGapThreshold in the request options).
    [[nodiscard]] static std::vector<PdfExtractedWord> ExtractWords(
        const std::vector<PdfTextChunk>& chunks,
        double wordGapThreshold = 3.0);
};

} // namespace CPPPdf

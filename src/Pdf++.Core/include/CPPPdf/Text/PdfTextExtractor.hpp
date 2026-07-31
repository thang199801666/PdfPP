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
    std::string utf8Text;
    PdfPoint start;
    PdfPoint end;
    PdfRectangle boundingBox;
    std::string fontResource;
    double fontSize{};
    int renderingMode{};
    std::uint32_t sourceObjectNumber{};
    std::uint32_t glyphCount{};
    bool usedEmbeddedFontMetrics{};
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

struct PdfTextExtractionRequest {
    PdfTextExtractionStrategy strategy{PdfTextExtractionStrategy::Location};
    PdfTextExtractionOptions options{};
    std::optional<PdfTextRegion> region;
    std::uint32_t sourceObjectNumber{};
    std::function<const PdfFontResource*(std::string_view)> fontResolver;
    std::array<double, 6> initialTransformationMatrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    std::function<void(
        std::string_view,
        const std::array<double, 6>&,
        std::vector<PdfTextChunk>&)> xObjectHandler;
};

class PdfTextExtractor final {
public:
    [[nodiscard]] static std::vector<PdfTextChunk> ExtractChunks(
        std::string_view content,
        const PdfTextExtractionRequest& request = {});

    [[nodiscard]] static std::string BuildText(
        const std::vector<PdfTextChunk>& chunks,
        const PdfTextExtractionRequest& request = {});

    [[nodiscard]] static std::string ExtractText(
        std::string_view content,
        const PdfTextExtractionRequest& request = {});
};

} // namespace CPPPdf

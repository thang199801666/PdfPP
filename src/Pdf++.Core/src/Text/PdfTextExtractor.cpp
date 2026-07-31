#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace CPPPdf {
namespace {

bool Intersects(const PdfRectangle& a, const PdfRectangle& b) noexcept {
    return a.left <= b.right && a.right >= b.left && a.bottom <= b.top && a.top >= b.bottom;
}

bool Contains(const PdfRectangle& outer, const PdfRectangle& inner) noexcept {
    return inner.left >= outer.left && inner.right <= outer.right &&
           inner.bottom >= outer.bottom && inner.top <= outer.top;
}

struct TextMetrics final {
    std::size_t codePoints{};
    std::size_t spaces{};
};

TextMetrics AnalyzeText(std::string_view value) noexcept {
    TextMetrics metrics;
    for (const unsigned char byte : value) {
        if ((byte & 0xC0U) != 0x80U) ++metrics.codePoints;
        if (byte == static_cast<unsigned char>(' ')) ++metrics.spaces;
    }
    return metrics;
}

bool IsWhitespaceBoundary(const std::string& value) noexcept {
    return value.empty() || value.back() == ' ' || value.back() == '\n' || value.back() == '\r';
}

PdfPoint TransformPoint(
    const std::array<double, 6>& matrix,
    const double x,
    const double y) noexcept {
    return {
        matrix[0] * x + matrix[2] * y + matrix[4],
        matrix[1] * x + matrix[3] * y + matrix[5]
    };
}

PdfRectangle BoundsFromPoints(const std::array<PdfPoint, 4>& points) noexcept {
    PdfRectangle bounds{
        points[0].x, points[0].y, points[0].x, points[0].y
    };
    for (const auto& point : points) {
        bounds.left = std::min(bounds.left, point.x);
        bounds.bottom = std::min(bounds.bottom, point.y);
        bounds.right = std::max(bounds.right, point.x);
        bounds.top = std::max(bounds.top, point.y);
    }
    return bounds;
}

} // namespace

std::vector<PdfTextChunk> PdfTextExtractor::ExtractChunks(
    const std::string_view content,
    const PdfTextExtractionRequest& request) {
    std::vector<PdfTextChunk> chunks;
    // Most text-showing operators carry several bytes. This conservative
    // estimate reduces vector growth without over-allocating large streams.
    chunks.reserve(std::min<std::size_t>(content.size() / 16U + 1U, 65536U));
    std::unordered_map<std::string, const PdfFontResource*> fontCache;
    fontCache.reserve(16U);
    double currentX{};
    double currentY{};
    bool hasPosition{};

    PdfContentProcessor processor;
    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::SetTextMatrix ||
            event.type == PdfContentEventType::MoveText) {
            currentX = event.textState.textMatrix[4];
            currentY = event.textState.textMatrix[5] + event.textState.rise;
            hasPosition = true;
            return;
        }
        if (event.type == PdfContentEventType::InvokeXObject) {
            if (request.xObjectHandler) {
                request.xObjectHandler(
                    event.text,
                    event.textState.currentTransformationMatrix,
                    chunks);
            }
            return;
        }
        if (event.type != PdfContentEventType::RenderText || event.text.empty()) return;

        if (!hasPosition) {
            currentX = event.textState.textMatrix[4];
            currentY = event.textState.textMatrix[5] + event.textState.rise;
            hasPosition = true;
        }

        const double fontSize = event.textState.fontSize > 0.0 ? event.textState.fontSize : 12.0;
        const double scale = event.textState.horizontalScaling / 100.0;
        const PdfFontResource* font{};
        if (request.fontResolver) {
            const auto found = fontCache.find(event.textState.fontResource);
            if (found != fontCache.end()) {
                font = found->second;
            } else {
                font = request.fontResolver(event.textState.fontResource);
                fontCache.emplace(event.textState.fontResource, font);
            }
        }
        std::string decodedText = font ? font->Decode(event.text) : event.text;
        const TextMetrics decodedMetrics = AnalyzeText(decodedText);
        const auto glyphCountValue = font
            ? font->GetGlyphCount(event.text)
            : std::max<std::size_t>(1U, decodedMetrics.codePoints);
        const double glyphCount = static_cast<double>(std::max<std::size_t>(1U, glyphCountValue));
        double width = font
            ? (font->MeasureEncodedText(event.text) / 1000.0) * fontSize * scale
            : glyphCount * fontSize * 0.5 * scale;
        width += std::max(0.0, glyphCount - 1.0) * event.textState.characterSpacing * scale;
        width += static_cast<double>(decodedMetrics.spaces) * event.textState.wordSpacing * scale;

        if (event.operation == "TJ") {
            for (const double adjustment : event.numbers) {
                width += (-adjustment / 1000.0) * fontSize * scale;
            }
        }
        width = std::max(0.0, width);

        const auto& ctm = event.textState.currentTransformationMatrix;
        const PdfPoint start = TransformPoint(ctm, currentX, currentY);
        const PdfPoint end = TransformPoint(ctm, currentX + width, currentY);
        const std::array<PdfPoint, 4> boxPoints{
            TransformPoint(ctm, currentX, currentY - fontSize * 0.20),
            TransformPoint(ctm, currentX + width, currentY - fontSize * 0.20),
            TransformPoint(ctm, currentX + width, currentY + fontSize * 0.80),
            TransformPoint(ctm, currentX, currentY + fontSize * 0.80)
        };

        PdfTextChunk chunk;
        chunk.utf8Text = std::move(decodedText);
        chunk.start = start;
        chunk.end = end;
        chunk.boundingBox = BoundsFromPoints(boxPoints);
        chunk.fontResource = event.textState.fontResource;
        chunk.fontSize = fontSize;
        chunk.renderingMode = event.textState.renderingMode;
        chunk.sourceObjectNumber = request.sourceObjectNumber;
        chunk.glyphCount = static_cast<std::uint32_t>(glyphCountValue);
        chunk.usedEmbeddedFontMetrics = font != nullptr;

        if (request.strategy == PdfTextExtractionStrategy::Region && request.region.has_value()) {
            const bool accepted = request.region->includeIntersecting
                ? Intersects(request.region->bounds, chunk.boundingBox)
                : Contains(request.region->bounds, chunk.boundingBox);
            if (!accepted) {
                currentX += width;
                return;
            }
        }
        chunks.push_back(std::move(chunk));
        currentX += width;
    });
    PdfTextStateSnapshot initialState;
    initialState.currentTransformationMatrix = request.initialTransformationMatrix;
    processor.Process(content, initialState);
    return chunks;
}

std::string PdfTextExtractor::BuildText(
    const std::vector<PdfTextChunk>& inputChunks,
    const PdfTextExtractionRequest& request) {
    if (inputChunks.empty()) return {};

    std::vector<const PdfTextChunk*> chunks;
    chunks.reserve(inputChunks.size());
    for (const auto& chunk : inputChunks) chunks.push_back(&chunk);

    if (request.strategy != PdfTextExtractionStrategy::Simple) {
        std::stable_sort(chunks.begin(), chunks.end(), [&](const PdfTextChunk* a, const PdfTextChunk* b) {
            const double tolerance = std::max(0.01, request.options.lineTolerance);
            if (std::abs(a->start.y - b->start.y) > tolerance) return a->start.y > b->start.y;
            return a->start.x < b->start.x;
        });
    }

    std::size_t outputCapacity{};
    for (const auto& chunk : inputChunks) outputCapacity += chunk.utf8Text.size();
    // Account for possible inserted spaces/newlines while preserving a single
    // allocation for the common case.
    outputCapacity += inputChunks.size();
    std::string output;
    output.reserve(outputCapacity);
    const PdfTextChunk* previous{};
    for (const auto* chunk : chunks) {
        if (previous != nullptr && request.strategy != PdfTextExtractionStrategy::Simple) {
            const double deltaY = std::abs(previous->start.y - chunk->start.y);
            if (deltaY > request.options.lineTolerance) {
                while (!output.empty() && output.back() == ' ') output.pop_back();
                if (!output.empty() && output.back() != '\n') output.push_back('\n');
            } else if (request.options.insertSpaces) {
                const double gap = chunk->start.x - previous->end.x;
                const double threshold = std::max(request.options.wordGapThreshold,
                                                  std::min(previous->fontSize, chunk->fontSize) * 0.20);
                if (gap > threshold && !IsWhitespaceBoundary(output)) output.push_back(' ');
            }
        }
        output += chunk->utf8Text;
        previous = chunk;
    }
    while (!output.empty() && (output.back() == ' ' || output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return output;
}

std::string PdfTextExtractor::ExtractText(
    const std::string_view content,
    const PdfTextExtractionRequest& request) {
    return BuildText(ExtractChunks(content, request), request);
}

} // namespace CPPPdf

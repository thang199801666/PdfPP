#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <charconv>
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

std::vector<PdfMarkedContentSpan> PdfTextExtractor::ExtractMarkedContent(
    const std::string_view content) {
    std::vector<PdfMarkedContentSpan> spans;
    std::vector<std::size_t> active;
    PdfContentProcessor processor;
    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::BeginMarkedContent) {
            PdfMarkedContentSpan span;
            span.tag = event.text;
            span.property = event.markedContentProperty;
            span.depth = active.size();
            const auto marker = span.property.find("/MCID");
            if (marker != std::string::npos) {
                std::size_t begin = marker + 5U;
                while (begin < span.property.size() &&
                       std::isspace(static_cast<unsigned char>(span.property[begin]))) ++begin;
                std::uint32_t value{};
                const auto parsed = std::from_chars(
                    span.property.data() + begin,
                    span.property.data() + span.property.size(), value);
                if (parsed.ec == std::errc{}) span.mcid = value;
            }
            spans.push_back(std::move(span));
            active.push_back(spans.size() - 1U);
            return;
        }
        if (event.type == PdfContentEventType::EndMarkedContent) {
            if (!active.empty()) active.pop_back();
            return;
        }
        if (event.type == PdfContentEventType::RenderText && !event.text.empty()) {
            for (const auto index : active) spans[index].encodedText += event.text;
        }
    });
    processor.Process(content);
    return spans;
}

std::vector<PdfTextChunk> PdfTextExtractor::ExtractChunks(
    const std::string_view content,
    const PdfTextExtractionRequest& request) {
    std::vector<PdfTextChunk> chunks;
    // Most text-showing operators carry several bytes. This conservative
    // estimate reduces vector growth without over-allocating large streams.
    chunks.reserve(std::min<std::size_t>(content.size() / 16U + 1U, 65536U));
    std::unordered_map<std::string, const PdfFontResource*> fontCache;
    fontCache.reserve(16U);
    double fillAlpha = 1.0;
    double strokeAlpha = 1.0;
    std::vector<std::array<double, 2>> alphaStack;
    double currentX{};
    double currentY{};
    double textLineX{};
    double textLineY{};
    bool hasPosition{};

    PdfContentProcessor processor;
    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::SaveState) {
            alphaStack.push_back({strokeAlpha, fillAlpha});
            return;
        }
        if (event.type == PdfContentEventType::RestoreState) {
            if (!alphaStack.empty()) {
                strokeAlpha = alphaStack.back()[0];
                fillAlpha = alphaStack.back()[1];
                alphaStack.pop_back();
            }
            return;
        }
        if (event.operation == "gs" && request.extGStateResolver && !event.text.empty()) {
            const auto alpha = request.extGStateResolver(request.resourceObjectNumber, event.text);
            strokeAlpha = alpha[0];
            fillAlpha = alpha[1];
            return;
        }
        if (event.type == PdfContentEventType::BeginText) {
            // BT starts a new text object: the line origin resets to the
            // current text matrix origin.
            textLineX = event.textState.textMatrix[4];
            textLineY = event.textState.textMatrix[5];
            currentX = textLineX;
            currentY = textLineY + event.textState.rise;
            hasPosition = true;
            return;
        }
        if (event.type == PdfContentEventType::SetTextMatrix) {
            textLineX = event.textState.textMatrix[4];
            textLineY = event.textState.textMatrix[5];
            currentX = textLineX;
            currentY = textLineY + event.textState.rise;
            hasPosition = true;
            return;
        }
        if (event.type == PdfContentEventType::MoveText) {
            // Td/T* translates the text-line matrix in text space. Tj
            // advances only the text matrix, so keep a separate line origin
            // and apply the Tm basis to the operator's delta.
            const auto& matrix = event.textState.textMatrix;
            const double scaleX = std::max(1.0e-9, std::hypot(matrix[0], matrix[1]));
            const double scaleY = std::max(1.0e-9, std::hypot(matrix[2], matrix[3]));
            if (event.numbers.size() >= 2U) {
                textLineX += event.numbers[0] * scaleX;
                textLineY += event.numbers[1] * scaleY;
            } else {
                textLineX = matrix[4];
                textLineY = matrix[5];
            }
            currentX = textLineX;
            currentY = textLineY + event.textState.rise;
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

        const bool invisibleText = event.textState.renderingMode == 3;

        if (!hasPosition) {
            currentX = event.textState.textMatrix[4];
            currentY = event.textState.textMatrix[5] + event.textState.rise;
            hasPosition = true;
        }

        const double fontSize = event.textState.fontSize > 0.0 ? event.textState.fontSize : 12.0;
        // PDF files commonly put the actual text size in Tm and use `1 Tf`.
        // The previous extractor only used the Tf operand, producing 1-point
        // bounding boxes for scaled text (including document.pdf). Account
        // for the text-matrix basis vectors before measuring and boxing it.
        const auto& textMatrix = event.textState.textMatrix;
        const double textScaleX = std::max(1.0e-9, std::hypot(textMatrix[0], textMatrix[1]));
        const double textScaleY = std::max(1.0e-9, std::hypot(textMatrix[2], textMatrix[3]));
        const double effectiveFontSize = fontSize * textScaleY;
        const double scale = event.textState.horizontalScaling / 100.0;
        const PdfFontResource* font{};
        if (request.fontResolver) {
            const auto found = fontCache.find(event.textState.fontResource);
            if (found != fontCache.end()) {
                font = found->second;
            } else {
                font = request.fontResolver(request.resourceObjectNumber, event.textState.fontResource);
                fontCache.emplace(event.textState.fontResource, font);
            }
        }
        std::string decodedText = font ? font->Decode(event.text) : event.text;
        if (event.operation == "TJ" && !event.textSegments.empty()) {
            decodedText.clear();
            for (std::size_t segmentIndex = 0; segmentIndex < event.textSegments.size(); ++segmentIndex) {
                const auto decodedSegment = font
                    ? font->Decode(event.textSegments[segmentIndex])
                    : event.textSegments[segmentIndex];
                decodedText += decodedSegment;
                const double adjustment = segmentIndex < event.textSegmentAdjustments.size()
                    ? event.textSegmentAdjustments[segmentIndex] : 0.0;
                if (adjustment < 0.0) {
                    const double adjustmentAdvance =
                        (-adjustment / 1000.0) * fontSize * textScaleX * scale;
                    if (adjustmentAdvance >= std::max(0.35, effectiveFontSize * 0.06) &&
                        (decodedText.empty() || decodedText.back() != ' ')) {
                        decodedText.push_back(' ');
                    }
                }
            }
        }
        const TextMetrics decodedMetrics = AnalyzeText(decodedText);
        const auto glyphCountValue = font
            ? font->GetGlyphCount(event.text)
            : std::max<std::size_t>(1U, decodedMetrics.codePoints);
        const double glyphCount = static_cast<double>(std::max<std::size_t>(1U, glyphCountValue));
        double width = font
            ? (font->MeasureEncodedText(event.text) / 1000.0) * fontSize * textScaleX * scale
            : glyphCount * fontSize * textScaleX * 0.5 * scale;
        width += std::max(0.0, glyphCount - 1.0) * event.textState.characterSpacing * textScaleX * scale;
        width += static_cast<double>(decodedMetrics.spaces) * event.textState.wordSpacing * textScaleX * scale;

        if (event.operation == "TJ") {
            for (const double adjustment : event.numbers) {
                width += (-adjustment / 1000.0) * fontSize * textScaleX * scale;
            }
        }
        width = std::max(0.0, width);

        const auto& ctm = event.textState.currentTransformationMatrix;
        const PdfPoint start = TransformPoint(ctm, currentX, currentY);
        const PdfPoint end = TransformPoint(ctm, currentX + width, currentY);
        const std::array<PdfPoint, 4> boxPoints{
            TransformPoint(ctm, currentX, currentY - effectiveFontSize * 0.20),
            TransformPoint(ctm, currentX + width, currentY - effectiveFontSize * 0.20),
            TransformPoint(ctm, currentX + width, currentY + effectiveFontSize * 0.80),
            TransformPoint(ctm, currentX, currentY + effectiveFontSize * 0.80)
        };

        PdfTextChunk chunk;
        chunk.utf8Text = std::move(decodedText);
        chunk.encodedText = event.text;
        chunk.contentOperation = event.operation;
        if (font) {
            chunk.characterCodes = font->GetCharacterCodes(event.text);
            chunk.glyphIds.reserve(chunk.characterCodes.size());
            for (const auto code : chunk.characterCodes) {
                const auto glyph = font->GetEmbeddedGlyphId(code);
                chunk.glyphIds.push_back(glyph.value_or(std::numeric_limits<std::uint16_t>::max()));
            }
        }
        chunk.start = start;
        chunk.end = end;
        chunk.boundingBox = BoundsFromPoints(boxPoints);
        chunk.fontResource = event.textState.fontResource;
        chunk.fontFamily = font ? font->GetDescriptor().baseFont : std::string{};
        chunk.fontSize = effectiveFontSize;
        chunk.renderingMode = event.textState.renderingMode;
        chunk.fillAlpha = fillAlpha;
        chunk.strokeAlpha = strokeAlpha;
        chunk.sourceObjectNumber = request.sourceObjectNumber;
        chunk.resourceObjectNumber = request.resourceObjectNumber;
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
        if (invisibleText && request.options.ignoreInvisibleText) {
            // Invisible text still advances the text matrix, but is not
            // exposed as extracted content by default.
            currentX += width;
            return;
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

std::vector<PdfExtractedWord> PdfTextExtractor::ExtractWords(
    const std::vector<PdfTextChunk>& chunks,
    const double wordGapThreshold) {
    std::vector<PdfExtractedWord> words;
    PdfExtractedWord current;
    bool hasCurrent = false;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto& chunk = chunks[i];
        if (chunk.utf8Text.empty() || chunk.boundingBox.empty()) continue;
        // A whitespace-only chunk terminates the current word.
        const bool isWhitespace = std::all_of(chunk.utf8Text.begin(), chunk.utf8Text.end(),
            [](const unsigned char c) { return std::isspace(c) != 0; });
        if (isWhitespace) {
            if (hasCurrent) {
                words.push_back(current);
                current = PdfExtractedWord{};
                hasCurrent = false;
            }
            continue;
        }
        if (!hasCurrent) {
            current.text = chunk.utf8Text;
            current.boundingBox = chunk.boundingBox;
            current.firstChunkIndex = i;
            current.chunkCount = 1U;
            hasCurrent = true;
            continue;
        }
        // Start a new word when there is a horizontal gap or a vertical break.
        const double gap = chunk.boundingBox.left - current.boundingBox.right;
        const double sameLine = std::abs(chunk.boundingBox.bottom - current.boundingBox.bottom);
        const bool separated = gap > std::max(wordGapThreshold, 0.0) ||
            sameLine > std::max(current.boundingBox.height(), chunk.boundingBox.height()) * 0.5;
        if (separated) {
            words.push_back(current);
            current = PdfExtractedWord{};
            current.text = chunk.utf8Text;
            current.boundingBox = chunk.boundingBox;
            current.firstChunkIndex = i;
            current.chunkCount = 1U;
            continue;
        }
        current.text += chunk.utf8Text;
        current.boundingBox.right = std::max(current.boundingBox.right, chunk.boundingBox.right);
        current.boundingBox.top = std::max(current.boundingBox.top, chunk.boundingBox.top);
        current.boundingBox.left = std::min(current.boundingBox.left, chunk.boundingBox.left);
        current.boundingBox.bottom = std::min(current.boundingBox.bottom, chunk.boundingBox.bottom);
        ++current.chunkCount;
    }
    if (hasCurrent) words.push_back(current);
    return words;
}

} // namespace CPPPdf

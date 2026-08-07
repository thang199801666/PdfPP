#include <CPPPdf/Text/PdfTextSearch.hpp>
#include <CPPPdf/Text/PdfTextLayout.hpp>

#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <regex>
#include <string>
#include <utility>
#include <vector>
#include <stdexcept>

namespace CPPPdf {
namespace {

constexpr std::size_t NoChunk = std::numeric_limits<std::size_t>::max();

void ValidateRenderingMode(const std::optional<int>& mode) {
    if (mode.has_value() && (*mode < 0 || *mode > 7)) {
        throw std::invalid_argument("PDF text rendering mode must be between 0 and 7.");
    }
}

struct ChunkSpan final {
    std::size_t textBegin{};
    std::size_t textEnd{};
    std::size_t chunkIndex{};
};

std::string asciiFold(const std::string_view value) {
    // Unicode-aware case folding: ASCII plus Latin-1 / Latin Extended-A.
    return PdfTextLayout::ToLower(value);
}

bool sameLine(const PdfTextChunk& left, const PdfTextChunk& right, const double tolerance) {
    const double leftCenter = (left.boundingBox.bottom + left.boundingBox.top) * 0.5;
    const double rightCenter = (right.boundingBox.bottom + right.boundingBox.top) * 0.5;
    return std::abs(leftCenter - rightCenter) <= tolerance;
}

PdfRectangle sliceRectangle(const PdfTextChunk& chunk, const std::size_t begin, const std::size_t end) {
    const std::size_t size = std::max<std::size_t>(1U, chunk.utf8Text.size());
    const double leftRatio = static_cast<double>(std::min(begin, size)) / static_cast<double>(size);
    const double rightRatio = static_cast<double>(std::min(end, size)) / static_cast<double>(size);
    const double width = chunk.boundingBox.right - chunk.boundingBox.left;
    return PdfRectangle{
        chunk.boundingBox.left + width * leftRatio,
        chunk.boundingBox.bottom,
        chunk.boundingBox.left + width * rightRatio,
        chunk.boundingBox.top};
}

PdfRectangle unionRectangles(const std::vector<PdfRectangle>& rectangles) {
    if (rectangles.empty()) return {};
    PdfRectangle result = rectangles.front();
    for (std::size_t index = 1; index < rectangles.size(); ++index) {
        result.left = std::min(result.left, rectangles[index].left);
        result.bottom = std::min(result.bottom, rectangles[index].bottom);
        result.right = std::max(result.right, rectangles[index].right);
        result.top = std::max(result.top, rectangles[index].top);
    }
    return result;
}

} // namespace

class PdfTextSearchIndex::Impl final {
public:
    explicit Impl(std::vector<PdfTextChunk> inputChunks, const PdfTextSearchIndexOptions& options)
        : chunks(std::move(inputChunks)) {
        std::size_t byteCapacity{};
        std::size_t nonEmptyChunks{};
        for (const auto& chunk : chunks) {
            byteCapacity += chunk.utf8Text.size();
            nonEmptyChunks += chunk.utf8Text.empty() ? 0U : 1U;
        }
        if (nonEmptyChunks > 1U) byteCapacity += nonEmptyChunks - 1U;
        text.reserve(byteCapacity);
        spans.reserve(nonEmptyChunks);
        barriers.reserve(nonEmptyChunks > 0U ? nonEmptyChunks - 1U : 0U);

        std::size_t previousChunk = NoChunk;
        for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
            const auto& chunk = chunks[chunkIndex];
            if (chunk.utf8Text.empty()) continue;
            if (previousChunk != NoChunk) {
                const auto& previous = chunks[previousChunk];
                const bool connected = sameLine(previous, chunk, options.lineTolerance) &&
                    chunk.boundingBox.left - previous.boundingBox.right <= options.maxHorizontalGap;
                if (!connected) {
                    barriers.push_back(text.size());
                    text.push_back('\n');
                }
            }
            const std::size_t begin = text.size();
            text.append(chunk.utf8Text);
            spans.push_back(ChunkSpan{begin, text.size(), chunkIndex});
            previousChunk = chunkIndex;
        }
        foldedText = asciiFold(text);
    }

    [[nodiscard]] bool containsBarrier(const std::size_t begin, const std::size_t end) const noexcept {
        const auto iterator = std::lower_bound(barriers.begin(), barriers.end(), begin);
        return iterator != barriers.end() && *iterator < end;
    }

    [[nodiscard]] PdfTextSearchMatch createMatch(const std::size_t begin, const std::size_t end) const {
        PdfTextSearchMatch match;
        match.matchedText = text.substr(begin, end - begin);
        match.firstChunkIndex = NoChunk;

        auto iterator = std::lower_bound(spans.begin(), spans.end(), begin, [](const ChunkSpan& span, const std::size_t position) {
            return span.textEnd <= position;
        });
        for (; iterator != spans.end() && iterator->textBegin < end; ++iterator) {
            const std::size_t overlapBegin = std::max(begin, iterator->textBegin);
            const std::size_t overlapEnd = std::min(end, iterator->textEnd);
            if (overlapBegin >= overlapEnd) continue;
            const auto& chunk = chunks[iterator->chunkIndex];
            match.firstChunkIndex = std::min(match.firstChunkIndex, iterator->chunkIndex);
            match.lastChunkIndex = std::max(match.lastChunkIndex, iterator->chunkIndex);
            match.rectangles.push_back(sliceRectangle(
                chunk,
                overlapBegin - iterator->textBegin,
                overlapEnd - iterator->textBegin));
        }
        if (!match.rectangles.empty()) match.boundingBox = unionRectangles(match.rectangles);
        return match;
    }

    std::vector<PdfTextChunk> chunks;
    std::string text;
    std::string foldedText;
    std::vector<ChunkSpan> spans;
    std::vector<std::size_t> barriers;
};

PdfTextSearchIndex::PdfTextSearchIndex(
    std::vector<PdfTextChunk>&& chunks,
    const PdfTextSearchIndexOptions& options)
    : impl_(std::make_unique<Impl>(std::move(chunks), options)) {}

PdfTextSearchIndex::PdfTextSearchIndex(
    const std::vector<PdfTextChunk>& chunks,
    const PdfTextSearchIndexOptions& options)
    : impl_(std::make_unique<Impl>(chunks, options)) {}

PdfTextSearchIndex::~PdfTextSearchIndex() = default;
PdfTextSearchIndex::PdfTextSearchIndex(PdfTextSearchIndex&&) noexcept = default;
PdfTextSearchIndex& PdfTextSearchIndex::operator=(PdfTextSearchIndex&&) noexcept = default;

std::size_t PdfTextSearchIndex::GetChunkCount() const noexcept { return impl_->chunks.size(); }
std::size_t PdfTextSearchIndex::GetSearchableByteCount() const noexcept { return impl_->text.size(); }
std::string_view PdfTextSearchIndex::GetSearchableText() const noexcept { return impl_->text; }
const std::vector<PdfTextChunk>& PdfTextSearchIndex::GetChunks() const noexcept { return impl_->chunks; }

std::vector<PdfTextSearchMatch> PdfTextSearchIndex::Find(
    const std::string_view keyword,
    const PdfTextSearchOptions& options) const {
    ValidateRenderingMode(options.renderingMode);
    if (keyword.empty()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Text search keyword cannot be empty.");
    }
    const std::string foldedNeedle = options.caseInsensitive ? asciiFold(keyword) : std::string{};
    const std::string_view haystack = options.caseInsensitive ? std::string_view(impl_->foldedText) : std::string_view(impl_->text);
    const std::string_view needle = options.caseInsensitive ? std::string_view(foldedNeedle) : keyword;
    std::vector<PdfTextSearchMatch> matches;
    std::size_t searchOffset{};
    while (searchOffset <= haystack.size()) {
        const std::size_t found = haystack.find(needle, searchOffset);
        if (found == std::string::npos) break;
        const std::size_t end = found + needle.size();
        if (!options.allowAcrossLineBreaks && impl_->containsBarrier(found, end)) {
            searchOffset = found + 1U;
            continue;
        }
        auto match = impl_->createMatch(found, end);
        if (!match.rectangles.empty()) matches.push_back(std::move(match));
        if (options.maxMatches != 0U && matches.size() >= options.maxMatches) break;
        searchOffset = found + std::max<std::size_t>(1U, needle.size());
    }
    return matches;
}

std::vector<PdfTextSearchMatch> PdfTextSearchIndex::FindRegex(
    const std::string_view pattern,
    const PdfRegexSearchOptions& options) const {
    ValidateRenderingMode(options.renderingMode);
    if (pattern.empty()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Text search regex cannot be empty.");
    }
    auto flags = std::regex_constants::ECMAScript;
    if (options.caseInsensitive) flags |= std::regex_constants::icase;
    if (options.optimize) flags |= std::regex_constants::optimize;
    try {
        return FindRegex(std::regex(pattern.begin(), pattern.end(), flags), options);
    } catch (const std::regex_error& error) {
        throw PdfException(PdfErrorCode::InvalidArgument,
            std::string("Invalid text-search regular expression: ") + error.what());
    }
}

std::vector<PdfTextSearchMatch> PdfTextSearchIndex::FindRegex(
    const std::regex& expression,
    const PdfRegexSearchOptions& options) const {
    ValidateRenderingMode(options.renderingMode);
    std::vector<PdfTextSearchMatch> matches;
    for (auto iterator = std::sregex_iterator(impl_->text.begin(), impl_->text.end(), expression);
         iterator != std::sregex_iterator(); ++iterator) {
        const std::size_t matchBegin = static_cast<std::size_t>(iterator->position());
        const std::size_t matchLength = static_cast<std::size_t>(iterator->length());
        if (matchLength == 0U) continue;
        const std::size_t matchEnd = matchBegin + matchLength;
        if (!options.allowAcrossLineBreaks && impl_->containsBarrier(matchBegin, matchEnd)) continue;
        auto match = impl_->createMatch(matchBegin, matchEnd);
        if (!match.rectangles.empty()) matches.push_back(std::move(match));
        if (options.maxMatches != 0U && matches.size() >= options.maxMatches) break;
    }
    return matches;
}

std::vector<PdfTextSearchMatch> PdfTextSearch::Find(
    const std::vector<PdfTextChunk>& chunks,
    const std::string_view keyword,
    const PdfTextSearchOptions& options) {
    if (options.ignoreAccents) {
        // Normalize both the haystack and the needle so accents are ignored.
        std::vector<PdfTextChunk> normalized = chunks;
        for (auto& chunk : normalized) {
            chunk.utf8Text = PdfTextLayout::RemoveDiacritics(chunk.utf8Text);
        }
        PdfTextSearchOptions matchOptions = options;
        matchOptions.ignoreAccents = false;
        const std::string normalizedNeedle = PdfTextLayout::RemoveDiacritics(keyword);
        return PdfTextSearchIndex(normalized, {options.lineTolerance, options.maxHorizontalGap})
            .Find(normalizedNeedle, matchOptions);
    }
    return PdfTextSearchIndex(chunks, {options.lineTolerance, options.maxHorizontalGap}).Find(keyword, options);
}

std::vector<PdfTextSearchMatch> PdfTextSearch::FindRegex(
    const std::vector<PdfTextChunk>& chunks,
    const std::string_view pattern,
    const PdfRegexSearchOptions& options) {
    return PdfTextSearchIndex(chunks, {options.lineTolerance, options.maxHorizontalGap}).FindRegex(pattern, options);
}

std::vector<PdfTextSearchMatch> PdfTextSearch::FindRegex(
    const std::vector<PdfTextChunk>& chunks,
    const std::regex& expression,
    const PdfRegexSearchOptions& options) {
    return PdfTextSearchIndex(chunks, {options.lineTolerance, options.maxHorizontalGap}).FindRegex(expression, options);
}

} // namespace CPPPdf

#pragma once

#include <CPPPdf/Text/PdfTextExtractor.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf {

struct PdfTextSearchOptions final {
    bool caseInsensitive{true};
    bool ignoreAccents{false};
    bool allowAcrossLineBreaks{false};
    std::size_t maxMatches{}; // Zero means unlimited.
    double lineTolerance{2.0};
    double maxHorizontalGap{6.0};
    std::optional<int> renderingMode;
};

struct PdfRegexSearchOptions final {
    bool caseInsensitive{true};
    bool allowAcrossLineBreaks{false};
    bool optimize{true};
    std::size_t maxMatches{}; // Zero means unlimited.
    double lineTolerance{2.0};
    double maxHorizontalGap{6.0};
    std::optional<int> renderingMode;
};

struct PdfTextSearchIndexOptions final {
    double lineTolerance{2.0};
    double maxHorizontalGap{6.0};
};

struct PdfTextSearchMatch final {
    std::string matchedText;
    std::size_t firstChunkIndex{};
    std::size_t lastChunkIndex{};
    PdfRectangle boundingBox{};
    std::vector<PdfRectangle> rectangles;
};

// Reusable search index for repeated literal or regular-expression queries.
// The index owns the supplied chunks and builds a compact range map rather
// than storing one origin record per UTF-8 byte.
class PdfTextSearchIndex final {
public:
    explicit PdfTextSearchIndex(
        std::vector<PdfTextChunk>&& chunks,
        const PdfTextSearchIndexOptions& options = {});
    explicit PdfTextSearchIndex(
        const std::vector<PdfTextChunk>& chunks,
        const PdfTextSearchIndexOptions& options = {});
    ~PdfTextSearchIndex();

    PdfTextSearchIndex(PdfTextSearchIndex&&) noexcept;
    PdfTextSearchIndex& operator=(PdfTextSearchIndex&&) noexcept;
    PdfTextSearchIndex(const PdfTextSearchIndex&) = delete;
    PdfTextSearchIndex& operator=(const PdfTextSearchIndex&) = delete;

    [[nodiscard]] std::size_t GetChunkCount() const noexcept;
    [[nodiscard]] std::size_t GetSearchableByteCount() const noexcept;
    [[nodiscard]] std::string_view GetSearchableText() const noexcept;
    [[nodiscard]] const std::vector<PdfTextChunk>& GetChunks() const noexcept;

    [[nodiscard]] std::vector<PdfTextSearchMatch> Find(
        std::string_view keyword,
        const PdfTextSearchOptions& options = {}) const;

    [[nodiscard]] std::vector<PdfTextSearchMatch> FindRegex(
        std::string_view pattern,
        const PdfRegexSearchOptions& options = {}) const;

    [[nodiscard]] std::vector<PdfTextSearchMatch> FindRegex(
        const std::regex& expression,
        const PdfRegexSearchOptions& options = {}) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class PdfTextSearch final {
public:
    [[nodiscard]] static std::vector<PdfTextSearchMatch> Find(
        const std::vector<PdfTextChunk>& chunks,
        std::string_view keyword,
        const PdfTextSearchOptions& options = {});

    [[nodiscard]] static std::vector<PdfTextSearchMatch> FindRegex(
        const std::vector<PdfTextChunk>& chunks,
        std::string_view pattern,
        const PdfRegexSearchOptions& options = {});

    [[nodiscard]] static std::vector<PdfTextSearchMatch> FindRegex(
        const std::vector<PdfTextChunk>& chunks,
        const std::regex& expression,
        const PdfRegexSearchOptions& options = {});
};

} // namespace CPPPdf

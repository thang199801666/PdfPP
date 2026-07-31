#pragma once

#include <CPPPdf/Text/PdfTextSearch.hpp>

#include <cstddef>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf {

class PdfDocument;

struct PdfDocumentTextIndexOptions final {
    std::size_t memoryBudgetBytes{64U * 1024U * 1024U};
    std::size_t maxConcurrency{}; // Zero selects a hardware-aware default.
    PdfTextExtractionRequest extraction{};
    PdfTextSearchIndexOptions search{};
};

struct PdfDocumentTextIndexStatistics final {
    std::size_t cachedPages{};
    std::size_t estimatedBytes{};
    std::size_t cacheHits{};
    std::size_t cacheMisses{};
    std::size_t evictions{};
};

struct PdfDocumentTextSearchMatch final {
    std::size_t pageIndex{};
    PdfTextSearchMatch match;
};

// Lazily extracts and indexes page text. Cached page entries are shared by
// literal and regex queries and evicted using a memory-budgeted LRU policy.
class PdfTextDocumentIndex final {
public:
    explicit PdfTextDocumentIndex(
        const PdfDocument& document,
        PdfDocumentTextIndexOptions options = {});
    ~PdfTextDocumentIndex();

    PdfTextDocumentIndex(PdfTextDocumentIndex&&) noexcept;
    PdfTextDocumentIndex& operator=(PdfTextDocumentIndex&&) noexcept;
    PdfTextDocumentIndex(const PdfTextDocumentIndex&) = delete;
    PdfTextDocumentIndex& operator=(const PdfTextDocumentIndex&) = delete;

    [[nodiscard]] std::size_t GetPageCount() const noexcept;
    [[nodiscard]] std::shared_ptr<const PdfTextSearchIndex> GetPageIndex(std::size_t pageIndex) const;
    [[nodiscard]] std::string GetPageText(std::size_t pageIndex) const;

    void Preload(std::size_t firstPage, std::size_t pageCount) const;
    void Clear() const noexcept;

    [[nodiscard]] std::vector<PdfDocumentTextSearchMatch> FindAll(
        std::string_view keyword,
        const PdfTextSearchOptions& options = {}) const;
    [[nodiscard]] std::vector<PdfDocumentTextSearchMatch> FindRegexAll(
        std::string_view pattern,
        const PdfRegexSearchOptions& options = {}) const;
    [[nodiscard]] std::vector<PdfDocumentTextSearchMatch> FindRegexAll(
        const std::regex& expression,
        const PdfRegexSearchOptions& options = {}) const;

    [[nodiscard]] PdfDocumentTextIndexStatistics GetStatistics() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace CPPPdf

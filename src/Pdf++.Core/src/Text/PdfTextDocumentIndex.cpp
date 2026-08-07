#include <CPPPdf/Text/PdfTextDocumentIndex.hpp>

#include <CPPPdf/PdfDocument.hpp>
#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <atomic>
#include <future>
#include <list>
#include <thread>
#include <unordered_map>
#include <utility>

namespace CPPPdf {
namespace {

std::size_t EstimateEntryBytes(const PdfTextSearchIndex& index) noexcept {
    return sizeof(PdfTextSearchIndex) + index.GetSearchableByteCount() +
        index.GetChunkCount() * sizeof(PdfTextChunk);
}

std::vector<PdfTextChunk> FilterRenderingMode(
    const std::vector<PdfTextChunk>& chunks, const int mode) {
    std::vector<PdfTextChunk> filtered;
    filtered.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        if (chunk.renderingMode == mode) filtered.push_back(chunk);
    }
    return filtered;
}

} // namespace

class PdfTextDocumentIndex::Impl final {
public:
    struct Entry final {
        std::shared_ptr<PdfTextSearchIndex> index;
        std::size_t bytes{};
        std::list<std::size_t>::iterator lru;
    };

    Impl(const PdfDocument& source, PdfDocumentTextIndexOptions configured)
        : document(source), options(std::move(configured)) {}

    std::shared_ptr<const PdfTextSearchIndex> Get(const std::size_t pageIndex) const {
        if (pageIndex >= document.GetPageCount()) {
            throw PdfException(PdfErrorCode::InvalidArgument, "Text-index page index is out of range.");
        }
        {
            std::scoped_lock lock(mutex);
            const auto found = entries.find(pageIndex);
            if (found != entries.end()) {
                ++statistics.cacheHits;
                lru.splice(lru.begin(), lru, found->second.lru);
                return found->second.index;
            }
            ++statistics.cacheMisses;
        }

        std::vector<PdfTextChunk> chunks;
        {
            std::scoped_lock extractionLock(extractionMutex);
            chunks = document.ExtractTextChunks(pageIndex, options.extraction);
        }
        auto created = std::make_shared<PdfTextSearchIndex>(std::move(chunks), options.search);
        const std::size_t bytes = EstimateEntryBytes(*created);

        std::scoped_lock lock(mutex);
        const auto existing = entries.find(pageIndex);
        if (existing != entries.end()) {
            lru.splice(lru.begin(), lru, existing->second.lru);
            return existing->second.index;
        }
        lru.push_front(pageIndex);
        entries.emplace(pageIndex, Entry{created, bytes, lru.begin()});
        statistics.estimatedBytes += bytes;
        EvictLocked(pageIndex);
        statistics.cachedPages = entries.size();
        return created;
    }

    void EvictLocked(const std::size_t protectedPage) const {
        const std::size_t budget = options.memoryBudgetBytes;
        if (budget == 0U) return;
        while (statistics.estimatedBytes > budget && entries.size() > 1U) {
            const std::size_t candidate = lru.back();
            if (candidate == protectedPage) {
                lru.splice(lru.begin(), lru, std::prev(lru.end()));
                continue;
            }
            const auto found = entries.find(candidate);
            if (found == entries.end()) {
                lru.pop_back();
                continue;
            }
            statistics.estimatedBytes -= found->second.bytes;
            lru.erase(found->second.lru);
            entries.erase(found);
            ++statistics.evictions;
        }
    }

    const PdfDocument& document;
    PdfDocumentTextIndexOptions options;
    mutable std::mutex mutex;
    mutable std::mutex extractionMutex;
    mutable std::unordered_map<std::size_t, Entry> entries;
    mutable std::list<std::size_t> lru;
    mutable PdfDocumentTextIndexStatistics statistics;
};

PdfTextDocumentIndex::PdfTextDocumentIndex(
    const PdfDocument& document,
    PdfDocumentTextIndexOptions options)
    : impl_(std::make_unique<Impl>(document, std::move(options))) {}

PdfTextDocumentIndex::~PdfTextDocumentIndex() = default;
PdfTextDocumentIndex::PdfTextDocumentIndex(PdfTextDocumentIndex&&) noexcept = default;
PdfTextDocumentIndex& PdfTextDocumentIndex::operator=(PdfTextDocumentIndex&&) noexcept = default;

std::size_t PdfTextDocumentIndex::GetPageCount() const noexcept {
    return impl_->document.GetPageCount();
}

std::shared_ptr<const PdfTextSearchIndex> PdfTextDocumentIndex::GetPageIndex(
    const std::size_t pageIndex) const {
    return impl_->Get(pageIndex);
}

std::string PdfTextDocumentIndex::GetPageText(const std::size_t pageIndex) const {
    return std::string(impl_->Get(pageIndex)->GetSearchableText());
}

void PdfTextDocumentIndex::Preload(const std::size_t firstPage, const std::size_t pageCount) const {
    const std::size_t total = GetPageCount();
    if (firstPage > total || pageCount > total - firstPage) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Text-index preload range is out of bounds.");
    }
    if (pageCount == 0U) return;
    std::size_t concurrency = impl_->options.maxConcurrency;
    if (concurrency == 0U) concurrency = std::max<std::size_t>(1U, std::thread::hardware_concurrency());
    concurrency = std::min(concurrency, pageCount);
    std::atomic_size_t next{};
    std::vector<std::future<void>> workers;
    workers.reserve(concurrency);
    for (std::size_t worker = 0; worker < concurrency; ++worker) {
        workers.emplace_back(std::async(std::launch::async, [&, firstPage, pageCount] {
            for (;;) {
                const std::size_t offset = next.fetch_add(1U);
                if (offset >= pageCount) break;
                (void)impl_->Get(firstPage + offset);
            }
        }));
    }
    for (auto& worker : workers) worker.get();
}

void PdfTextDocumentIndex::Clear() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->entries.clear();
    impl_->lru.clear();
    impl_->statistics.cachedPages = 0U;
    impl_->statistics.estimatedBytes = 0U;
}

std::vector<PdfDocumentTextSearchMatch> PdfTextDocumentIndex::FindAll(
    const std::string_view keyword,
    const PdfTextSearchOptions& options) const {
    std::vector<PdfDocumentTextSearchMatch> result;
    for (std::size_t page = 0; page < GetPageCount(); ++page) {
        const auto pageIndex = impl_->Get(page);
        std::vector<PdfTextSearchMatch> matches;
        if (!options.renderingMode.has_value()) {
            matches = pageIndex->Find(keyword, options);
        } else {
            auto filteredChunks = FilterRenderingMode(pageIndex->GetChunks(), *options.renderingMode);
            PdfTextSearchIndex filteredIndex(filteredChunks, impl_->options.search);
            matches = filteredIndex.Find(keyword, options);
        }
        for (auto& match : matches) result.push_back({page, std::move(match)});
    }
    return result;
}

std::vector<PdfDocumentTextSearchMatch> PdfTextDocumentIndex::FindRegexAll(
    const std::string_view pattern,
    const PdfRegexSearchOptions& options) const {
    std::vector<PdfDocumentTextSearchMatch> result;
    for (std::size_t page = 0; page < GetPageCount(); ++page) {
        const auto pageIndex = impl_->Get(page);
        std::vector<PdfTextSearchMatch> matches;
        if (!options.renderingMode.has_value()) {
            matches = pageIndex->FindRegex(pattern, options);
        } else {
            auto filteredChunks = FilterRenderingMode(pageIndex->GetChunks(), *options.renderingMode);
            PdfTextSearchIndex filteredIndex(filteredChunks, impl_->options.search);
            matches = filteredIndex.FindRegex(pattern, options);
        }
        for (auto& match : matches) result.push_back({page, std::move(match)});
        if (options.maxMatches != 0U && result.size() >= options.maxMatches) {
            result.resize(options.maxMatches);
            break;
        }
    }
    return result;
}

std::vector<PdfDocumentTextSearchMatch> PdfTextDocumentIndex::FindRegexAll(
    const std::regex& expression,
    const PdfRegexSearchOptions& options) const {
    std::vector<PdfDocumentTextSearchMatch> result;
    for (std::size_t page = 0; page < GetPageCount(); ++page) {
        auto pageOptions = options;
        if (options.maxMatches != 0U) pageOptions.maxMatches = options.maxMatches - result.size();
        const auto pageIndex = impl_->Get(page);
        std::vector<PdfTextSearchMatch> matches;
        if (!options.renderingMode.has_value()) {
            matches = pageIndex->FindRegex(expression, pageOptions);
        } else {
            auto filteredChunks = FilterRenderingMode(pageIndex->GetChunks(), *options.renderingMode);
            PdfTextSearchIndex filteredIndex(filteredChunks, impl_->options.search);
            matches = filteredIndex.FindRegex(expression, pageOptions);
        }
        for (auto& match : matches) result.push_back({page, std::move(match)});
        if (options.maxMatches != 0U && result.size() >= options.maxMatches) break;
    }
    return result;
}

PdfDocumentTextIndexStatistics PdfTextDocumentIndex::GetStatistics() const noexcept {
    std::scoped_lock lock(impl_->mutex);
    auto result = impl_->statistics;
    result.cachedPages = impl_->entries.size();
    return result;
}

} // namespace CPPPdf

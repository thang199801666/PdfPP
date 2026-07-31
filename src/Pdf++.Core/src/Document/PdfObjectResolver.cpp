#include "Internal/Document/PdfObjectResolver.hpp"

#include "Internal/Parsing/PdfObjectParser.hpp"
#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <utility>

namespace CPPPdf::Internal {

PdfObjectResolver::PdfObjectResolver(PdfReaderLimits limits)
    : limits_(limits) {
    const std::size_t reserveCount = std::min(limits_.maxCachedObjects, std::size_t{4096});
    cache_.reserve(reserveCount);
    resolving_.reserve(std::min(limits_.maxRecursionDepth, std::size_t{256}));
}

std::size_t PdfObjectResolver::ReferenceKeyHash::operator()(const ReferenceKey& key) const noexcept {
    const std::uint64_t packed =
        (static_cast<std::uint64_t>(key.objectNumber) << 16U) |
        static_cast<std::uint64_t>(key.generation);
    return std::hash<std::uint64_t>{}(packed);
}

void PdfObjectResolver::Touch(
    std::unordered_map<ReferenceKey, CacheEntry, ReferenceKeyHash>::iterator entry) {
    recency_.splice(recency_.begin(), recency_, entry->second.recency);
    entry->second.recency = recency_.begin();
}

void PdfObjectResolver::EvictIfNeeded() {
    while (cache_.size() > limits_.maxCachedObjects && !recency_.empty()) {
        const ReferenceKey oldest = recency_.back();
        recency_.pop_back();
        cache_.erase(oldest);
    }
}

const PdfObject& PdfObjectResolver::Resolve(const PdfReference& reference,
                                            const ObjectReader& reader) {
    const ReferenceKey key{reference.objectNumber, reference.generation};

    if (auto cached = cache_.find(key); cached != cache_.end()) {
        Touch(cached);
        return cached->second.object;
    }

    if (resolving_.size() >= limits_.maxObjectCount) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Configured maximum object count was exceeded.");
    }

    if (!resolving_.insert(key).second) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Cyclic indirect object resolution detected.");
    }

    if (resolving_.size() > limits_.maxRecursionDepth) {
        resolving_.erase(key);
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Indirect object recursion limit exceeded.");
    }

    try {
        PdfObject parsed = PdfObjectParser::Parse(
            reader(reference.objectNumber), limits_.maxRecursionDepth);
        resolving_.erase(key);

        // A zero capacity is useful for memory-constrained streaming workflows.
        // Keep one transient slot so the returned reference remains valid until
        // the next Resolve call, then evict it normally.
        const std::size_t effectiveCapacity = std::max<std::size_t>(1U, limits_.maxCachedObjects);
        recency_.push_front(key);
        auto [entry, inserted] = cache_.emplace(
            key, CacheEntry{std::move(parsed), recency_.begin()});
        if (!inserted) {
            recency_.pop_front();
            Touch(entry);
        }
        while (cache_.size() > effectiveCapacity && !recency_.empty()) {
            const ReferenceKey oldest = recency_.back();
            if (oldest == key) break;
            recency_.pop_back();
            cache_.erase(oldest);
        }
        return entry->second.object;
    } catch (...) {
        resolving_.erase(key);
        throw;
    }
}

void PdfObjectResolver::Clear() noexcept {
    resolving_.clear();
    cache_.clear();
    recency_.clear();
}

std::size_t PdfObjectResolver::CachedObjectCount() const noexcept {
    return cache_.size();
}

std::size_t PdfObjectResolver::CacheCapacity() const noexcept {
    return limits_.maxCachedObjects;
}

} // namespace CPPPdf::Internal

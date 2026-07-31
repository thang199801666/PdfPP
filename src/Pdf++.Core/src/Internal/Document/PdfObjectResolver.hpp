#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace CPPPdf::Internal {

class PdfObjectResolver final {
public:
    using ObjectReader = std::function<std::string(std::uint32_t)>;

    explicit PdfObjectResolver(PdfReaderLimits limits);

    [[nodiscard]] const PdfObject& Resolve(const PdfReference& reference,
                                           const ObjectReader& reader);
    void Clear() noexcept;
    [[nodiscard]] std::size_t CachedObjectCount() const noexcept;
    [[nodiscard]] std::size_t CacheCapacity() const noexcept;

private:
    struct ReferenceKey final {
        std::uint32_t objectNumber{};
        std::uint16_t generation{};

        [[nodiscard]] bool operator==(const ReferenceKey&) const noexcept = default;
    };

    struct ReferenceKeyHash final {
        [[nodiscard]] std::size_t operator()(const ReferenceKey& key) const noexcept;
    };

    struct CacheEntry final {
        PdfObject object;
        std::list<ReferenceKey>::iterator recency;
    };

    void Touch(typename std::unordered_map<ReferenceKey, CacheEntry, ReferenceKeyHash>::iterator entry);
    void EvictIfNeeded();

    PdfReaderLimits limits_{};
    std::unordered_map<ReferenceKey, CacheEntry, ReferenceKeyHash> cache_;
    std::list<ReferenceKey> recency_;
    std::unordered_set<ReferenceKey, ReferenceKeyHash> resolving_;
};

} // namespace CPPPdf::Internal

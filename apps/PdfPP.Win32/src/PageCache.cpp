#include <PdfPP/Win32/PageCache.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace PdfPP::Win32 {

PageCache::PageCache(const std::size_t capacity) noexcept
    : capacity_(std::max<std::size_t>(1, capacity)) {}

void PageCache::Clear() noexcept {
    entries_.clear();
}

bool PageCache::Matches(const PageBitmap& bitmap, const int page,
                        const double zoom, const unsigned int dpi) noexcept {
    return bitmap.page == page && bitmap.dpi == dpi &&
           std::abs(bitmap.zoom - zoom) < 1.0e-9;
}

void PageCache::Store(PageBitmap bitmap) {
    if (!bitmap.IsValid()) return;
    const auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const PageBitmap& entry) {
        return Matches(entry, bitmap.page, bitmap.zoom, bitmap.dpi);
    });
    if (existing != entries_.end()) entries_.erase(existing);
    entries_.push_front(std::move(bitmap));
    while (entries_.size() > capacity_) entries_.pop_back();
}

bool PageCache::Contains(const int page, const double zoom, const unsigned int dpi) const noexcept {
    return std::any_of(entries_.begin(), entries_.end(), [&](const PageBitmap& entry) {
        return Matches(entry, page, zoom, dpi);
    });
}

std::optional<PageBitmap> PageCache::Get(const int page, const double zoom,
                                         const unsigned int dpi) {
    const auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const PageBitmap& entry) {
        return Matches(entry, page, zoom, dpi);
    });
    if (existing == entries_.end()) return std::nullopt;

    PageBitmap bitmap = *existing;
    if (existing != entries_.begin()) {
        PageBitmap recent = std::move(*existing);
        entries_.erase(existing);
        entries_.push_front(std::move(recent));
    }
    return bitmap;
}

} // namespace PdfPP::Win32

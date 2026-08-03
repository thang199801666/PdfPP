#pragma once

#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>

#include <utility>
#include <vector>
#include <functional>
#include <optional>

namespace CPPPdf {

class PdfDisplayList final {
public:
    using ImageResolver = std::function<std::optional<PdfExtractedImage>(
        std::uint32_t, std::string_view, const PdfContentEvent&)>;
    void Add(PdfContentEvent event) { events_.push_back(std::move(event)); }
    void Clear() noexcept { events_.clear(); }
    void Replay(const std::function<void(const PdfContentEvent&)>& callback) const {
        if (!callback) return;
        for (const auto& event : events_) callback(event);
    }
    void ForEach(const std::function<void(const PdfContentEvent&)>& callback) const { Replay(callback); }
    void ReplayContent(const std::function<void(const PdfContentEvent&)>& callback) const { Replay(callback); }
    void ReplayType(PdfContentEventType type,
                    const std::function<void(const PdfContentEvent&)>& callback) const {
        if (!callback) return;
        for (const auto& event : events_) if (event.type == type) callback(event);
    }
    void ReplayScope(std::string_view scope,
                     const std::function<void(const PdfContentEvent&)>& callback) const {
        if (!callback) return;
        for (const auto& event : events_) if (event.resourceScope == scope) callback(event);
    }
    void SetImageResolver(ImageResolver resolver) { imageResolver_ = std::move(resolver); }
    [[nodiscard]] std::optional<PdfExtractedImage> ResolveImage(const PdfContentEvent& event) const {
        if (!imageResolver_ || (event.type != PdfContentEventType::InvokeXObject &&
                                event.type != PdfContentEventType::RenderInlineImage)) return std::nullopt;
        return imageResolver_(event.resourceObjectNumber, event.text, event);
    }
    void ReplayImages(const std::function<void(const PdfContentEvent&, const PdfExtractedImage&)>& callback) const {
        if (!callback || !imageResolver_) return;
        for (const auto& event : events_) {
            if (const auto image = ResolveImage(event)) callback(event, *image);
        }
    }
    [[nodiscard]] const std::vector<PdfContentEvent>& Events() const noexcept { return events_; }
    [[nodiscard]] bool Empty() const noexcept { return events_.empty(); }
    [[nodiscard]] std::size_t Size() const noexcept { return events_.size(); }
    [[nodiscard]] std::size_t Count(PdfContentEventType type) const noexcept {
        std::size_t count = 0;
        for (const auto& event : events_) if (event.type == type) ++count;
        return count;
    }

private:
    std::vector<PdfContentEvent> events_;
    ImageResolver imageResolver_;
};

} // namespace CPPdf

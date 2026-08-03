#pragma once

#include <CPPPdf/Rendering/PdfBitmap.hpp>

namespace CPPPdf {

struct PdfTransparencyGroup final {
    PdfBitmap bitmap;
    bool isolated{};
    bool knockout{};
    PdfBlendMode blendMode{PdfBlendMode::SourceOver};

    void Clear() noexcept {
        bitmap.Clear({0U, 0U, 0U, 0U});
    }

    void BlendInto(PdfBitmap& target, const std::int32_t x = 0, const std::int32_t y = 0,
                   const std::uint8_t opacity = 255U) const {
        if (!knockout) {
            CompositeInto(target, x, y, opacity);
            return;
        }
        // Knockout groups do not accumulate overlapping source marks.
        for (std::size_t row = 0; row < bitmap.GetHeight(); ++row) {
            for (std::size_t column = 0; column < bitmap.GetWidth(); ++column) {
                const auto pixel = bitmap.GetPixel(column, row);
                if (pixel.alpha == 0U) continue;
                const auto destinationX = x + static_cast<std::int32_t>(column);
                const auto destinationY = y + static_cast<std::int32_t>(row);
                target.SetPixel(destinationX, destinationY, {0U, 0U, 0U, 0U});
                target.BlendPixel(destinationX, destinationY, pixel, blendMode);
            }
        }
    }

    void CompositeInto(PdfBitmap& target, std::int32_t x = 0, std::int32_t y = 0,
                       std::uint8_t opacity = 255U) const {
        target.BlendBitmap(bitmap, x, y, blendMode, opacity);
    }
};

} // namespace CPPPdf

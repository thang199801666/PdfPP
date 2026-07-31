#pragma once

#include <CPPPdf/Rendering/PdfBitmap.hpp>

#include <cstddef>

namespace CPPPdf {

class PdfDocument;

struct PdfRenderOptions final {
    double dpi{96.0};
    PdfRgbaColor background{PdfRgbaColor::White()};
    bool renderPaths{true};
    bool renderText{true};
    bool renderImages{true};
    bool interpolateImages{true};
    bool honorClippingPaths{true};
    std::size_t antiAliasSamples{1U};
    bool honorCropBox{true};
    std::size_t maximumDimension{16384U};
};

class PdfPageRenderer final {
public:
    [[nodiscard]] static PdfBitmap Render(
        const PdfDocument& document,
        std::size_t pageIndex,
        const PdfRenderOptions& options = {});
};

} // namespace CPPPdf

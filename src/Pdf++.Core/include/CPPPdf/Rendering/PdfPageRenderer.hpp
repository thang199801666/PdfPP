#pragma once

#include <CPPPdf/Rendering/PdfBitmap.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

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

struct PdfRenderResult final {
    PdfBitmap bitmap;
    std::size_t pageIndex{};
};

class PdfPageRenderer final {
public:
    [[nodiscard]] static PdfBitmap Render(
        const PdfDocument& document,
        std::size_t pageIndex,
        const PdfRenderOptions& options = {});

    // Renders every page in parallel. Each worker opens an independent
    // PdfDocument from `path` (so no mutable resolver/cache state is shared).
    // Falls back to sequential rendering when the file cannot be reopened or
    // `maxConcurrency` is 1. `maxConcurrency` of 0 uses the hardware
    // concurrency.
    [[nodiscard]] static std::vector<PdfRenderResult> RenderAllPagesParallel(
        const std::filesystem::path& path,
        const PdfRenderOptions& options = {},
        std::size_t maxConcurrency = 0U);
};

} // namespace CPPPdf

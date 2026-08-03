#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Graphics/PdfFunction.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace CPPPdf {

struct PdfAxialShading final {
    std::array<double, 4> coordinates{};
    std::array<double, 2> domain{0.0, 1.0};
    bool extendStart{};
    bool extendEnd{};
    std::optional<PdfExponentialFunction> function;

    [[nodiscard]] std::optional<std::vector<double>> Sample(double x, double y) const;
};

struct PdfRadialShading final {
    std::array<double, 6> coordinates{};
    std::array<double, 2> domain{0.0, 1.0};
    bool extendStart{};
    bool extendEnd{};
    std::optional<PdfExponentialFunction> function;

    [[nodiscard]] std::optional<std::vector<double>> Sample(double x, double y) const;
};

struct PdfResolvedShading final {
    PdfAxialShading axial;
    PdfRadialShading radial;
    std::uint32_t type{};
    std::array<double, 6> matrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
};

// A tiling pattern (PDF 32000-1 §8.7.3): a small content stream repeated to
// fill a region. The renderer paints the tile at its /BBox translated by the
// pattern matrix and repeated at /XStep//YStep spacing.
struct PdfTilingPattern final {
    PdfRectangle boundingBox{};
    double xStep{1.0};
    double yStep{1.0};
    std::uint32_t paintType{1};
    std::uint32_t tilingType{1};
    std::array<double, 6> matrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    std::string content;
    // Resources (fonts, images, ExtGState) referenced by the tile content.
    std::string resourcesDictionary;
    std::uint32_t resourceObjectNumber{};
};

struct PdfResolvedPattern final {
    PdfTilingPattern tiling;
    // The /Pattern dictionary entry may be a coloring pattern (type 2) which
    // we do not yet render; tiling patterns have type 1.
    std::uint32_t patternType{1};
};

} // namespace CPPPdf

#pragma once

#include <CPPPdf/Graphics/PdfFunction.hpp>

#include <array>
#include <optional>

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

} // namespace CPPPdf

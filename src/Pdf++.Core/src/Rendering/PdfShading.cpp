#include <CPPPdf/Rendering/PdfShading.hpp>

#include <algorithm>
#include <cmath>

namespace CPPPdf {

std::optional<std::vector<double>> PdfAxialShading::Sample(const double x, const double y) const {
    const double dx = coordinates[2] - coordinates[0];
    const double dy = coordinates[3] - coordinates[1];
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1.0e-18 || !function.has_value()) return std::nullopt;
    const double projection = ((x - coordinates[0]) * dx + (y - coordinates[1]) * dy) / lengthSquared;
    if (projection < 0.0 && !extendStart) return std::nullopt;
    if (projection > 1.0 && !extendEnd) return std::nullopt;
    const double clamped = std::clamp(projection, 0.0, 1.0);
    const double parameter = domain[0] + clamped * (domain[1] - domain[0]);
    return function->Evaluate(parameter);
}

std::optional<std::vector<double>> PdfRadialShading::Sample(const double x, const double y) const {
    if (!function.has_value()) return std::nullopt;
    const double dx = coordinates[3] - coordinates[0];
    const double dy = coordinates[4] - coordinates[1];
    const double dr = coordinates[5] - coordinates[2];
    const double px = x - coordinates[0];
    const double py = y - coordinates[1];
    const double a = dx * dx + dy * dy - dr * dr;
    const double b = -2.0 * (px * dx + py * dy + coordinates[2] * dr);
    const double c = px * px + py * py - coordinates[2] * coordinates[2];
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return std::nullopt;
    double parameter{};
    if (std::abs(a) <= 1.0e-12) {
        if (std::abs(b) <= 1.0e-12) return std::nullopt;
        parameter = -c / b;
    } else {
        const double root = std::sqrt(std::max(0.0, discriminant));
        const double first = (-b - root) / (2.0 * a);
        const double second = (-b + root) / (2.0 * a);
        parameter = std::max(first, second);
    }
    if (parameter < 0.0 && !extendStart) return std::nullopt;
    if (parameter > 1.0 && !extendEnd) return std::nullopt;
    const double clamped = std::clamp(parameter, 0.0, 1.0);
    return function->Evaluate(domain[0] + clamped * (domain[1] - domain[0]));
}

} // namespace CPPPdf

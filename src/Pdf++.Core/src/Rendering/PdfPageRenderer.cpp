#include <CPPPdf/Rendering/PdfPageRenderer.hpp>

#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Document/PdfPage.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <CPPPdf/Rendering/PdfDisplayList.hpp>
#include <CPPPdf/Rendering/PdfTransparencyGroup.h>
#include <CPPPdf/Graphics/PdfFunction.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <vector>

namespace CPPPdf {
namespace {

double sRgbGammaOfProfile(const std::vector<std::byte>& profile);

std::size_t CheckedPixelCount(const std::size_t width, const std::size_t height) {
    if (height != 0U && width > std::numeric_limits<std::size_t>::max() / height) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Renderer buffer dimensions overflow.");
    }
    return width * height;
}

struct DevicePoint final { double x{}; double y{}; };
using Subpath = std::vector<DevicePoint>;

PdfRgbaColor ToColor(const std::array<double, 3>& color) {
    const auto channel = [](const double value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };
    return {channel(color[0]), channel(color[1]), channel(color[2]), 255U};
}

PdfRgbaColor WithAlpha(PdfRgbaColor color, const double alpha) {
    color.alpha = static_cast<std::uint8_t>(std::lround(
        std::clamp(alpha, 0.0, 1.0) * static_cast<double>(color.alpha)));
    return color;
}

std::array<double, 2> Transform(
    const std::array<double, 6>& matrix,
    const double x,
    const double y) noexcept {
    return {
        matrix[0] * x + matrix[2] * y + matrix[4],
        matrix[1] * x + matrix[3] * y + matrix[5]
    };
}

class CoordinateMapper final {
public:
    CoordinateMapper(const PdfRectangle box, const int rotation, const double scale)
        : box_(box), rotation_(((rotation % 360) + 360) % 360), scale_(scale) {}

    [[nodiscard]] DevicePoint Map(const double x, const double y) const noexcept {
        const double localX = x - box_.left;
        const double localY = y - box_.bottom;
        switch (rotation_) {
        case 90:
            return {localY * scale_, localX * scale_};
        case 180:
            return {(box_.width() - localX) * scale_, localY * scale_};
        case 270:
            return {(box_.height() - localY) * scale_, (box_.width() - localX) * scale_};
        default:
            return {localX * scale_, (box_.height() - localY) * scale_};
        }
    }

private:
    PdfRectangle box_{};
    int rotation_{};
    double scale_{1.0};
};

void DrawDisk(PdfBitmap& bitmap, const std::int32_t centerX, const std::int32_t centerY,
              const std::int32_t radius, const PdfRgbaColor color) {
    const auto effectiveRadius = std::max<std::int32_t>(0, radius);
    for (std::int32_t y = -effectiveRadius; y <= effectiveRadius; ++y) {
        for (std::int32_t x = -effectiveRadius; x <= effectiveRadius; ++x) {
            if (x * x + y * y <= effectiveRadius * effectiveRadius) {
                bitmap.BlendPixel(centerX + x, centerY + y, color);
            }
        }
    }
}

void DrawLine(PdfBitmap& bitmap, DevicePoint a, const DevicePoint b,
              const double width, const PdfRgbaColor color,
              const int lineCap = 2) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double distance = std::ceil(std::max(std::abs(dx), std::abs(dy)));
    const auto steps = static_cast<std::int32_t>(std::min(
        distance, static_cast<double>(std::numeric_limits<std::int32_t>::max())));
    const auto radius = std::max<std::int32_t>(0, static_cast<std::int32_t>(std::lround(width * 0.5)));
    if (steps <= 0) {
        DrawDisk(bitmap, static_cast<std::int32_t>(std::lround(a.x)),
                 static_cast<std::int32_t>(std::lround(a.y)), radius, color);
        return;
    }
    const double length = std::hypot(dx, dy);
    if (lineCap == 1) {
        const double extendX = dx / length * width * 0.5;
        const double extendY = dy / length * width * 0.5;
        a.x -= extendX; a.y -= extendY;
    }
    const double incrementX = dx / static_cast<double>(steps);
    const double incrementY = dy / static_cast<double>(steps);
    for (std::int32_t i = 0; i <= steps; ++i) {
        const bool endpoint = i == 0 || i == steps;
        if (lineCap == 0 && endpoint) { a.x += incrementX; a.y += incrementY; continue; }
        DrawDisk(bitmap, static_cast<std::int32_t>(std::lround(a.x)),
                 static_cast<std::int32_t>(std::lround(a.y)), radius, color);
        a.x += incrementX;
        a.y += incrementY;
    }
}

void StrokePath(PdfBitmap& bitmap, const std::vector<Subpath>& paths,
                const double width, const PdfRgbaColor color,
                const int lineCap = 2, const int lineJoin = 0,
                const double miterLimit = 10.0) {
    for (const auto& path : paths) {
        for (std::size_t i = 1; i < path.size(); ++i) {
            DrawLine(bitmap, path[i - 1U], path[i], width, color, lineCap);
            if (i + 1U < path.size() && lineJoin == 2) {
                DrawDisk(bitmap, static_cast<std::int32_t>(std::lround(path[i].x)),
                         static_cast<std::int32_t>(std::lround(path[i].y)),
                         std::max<std::int32_t>(0, static_cast<std::int32_t>(std::lround(width * 0.5))), color);
            } else if (i + 1U < path.size() && lineJoin == 0) {
                const auto& previous = path[i - 1U];
                const auto& vertex = path[i];
                const auto& next = path[i + 1U];
                const double inX = vertex.x - previous.x;
                const double inY = vertex.y - previous.y;
                const double outX = next.x - vertex.x;
                const double outY = next.y - vertex.y;
                const double inLength = std::hypot(inX, inY);
                const double outLength = std::hypot(outX, outY);
                if (inLength > 1.0e-9 && outLength > 1.0e-9) {
                    const double half = width * 0.5;
                    const DevicePoint inNormal{-inY / inLength * half, inX / inLength * half};
                    const DevicePoint outNormal{-outY / outLength * half, outX / outLength * half};
                    const auto side = inX * outY - inY * outX;
                    const DevicePoint a{vertex.x + (side < 0.0 ? inNormal.x : -inNormal.x),
                                        vertex.y + (side < 0.0 ? inNormal.y : -inNormal.y)};
                    const DevicePoint b{vertex.x + (side < 0.0 ? outNormal.x : -outNormal.x),
                                        vertex.y + (side < 0.0 ? outNormal.y : -outNormal.y)};
                    const double joinDistance = std::hypot(a.x - vertex.x, a.y - vertex.y);
                    if (joinDistance <= std::max(1.0, miterLimit) * half) {
                        DrawLine(bitmap, a, b, width, color, 0);
                    } else {
                        DrawLine(bitmap, a, vertex, width, color, 0);
                        DrawLine(bitmap, vertex, b, width, color, 0);
                    }
                }
            }
        }
    }
}

// Draws a single polyline subpath with a PDF dash pattern. The pattern is
// [d1 d2 ... dn] alternating on/off, starting at dashPhase. Each on-segment
// is drawn as an independent line so gaps remain transparent.
void StrokeDashedPath(PdfBitmap& bitmap, const Subpath& path,
                      const double width, const PdfRgbaColor color,
                      const int lineCap,
                      const std::vector<double>& pattern,
                      const double phase) {
    if (path.size() < 2U) return;
    if (pattern.empty()) {
        for (std::size_t i = 1; i < path.size(); ++i) {
            DrawLine(bitmap, path[i - 1U], path[i], width, color, lineCap);
        }
        return;
    }
    double patternTotal = 0.0;
    for (const double value : pattern) patternTotal += value;
    if (patternTotal <= 0.0) {
        for (std::size_t i = 1; i < path.size(); ++i) {
            DrawLine(bitmap, path[i - 1U], path[i], width, color, lineCap);
        }
        return;
    }

    std::size_t patternIndex = 0U;
    double patternRemaining = phase;
    if (patternRemaining > 0.0) {
        while (patternRemaining >= pattern[patternIndex] + 1.0e-9) {
            patternRemaining -= pattern[patternIndex];
            patternIndex = (patternIndex + 1U) % pattern.size();
        }
    }
    bool drawing = (patternIndex % 2U) == 0U;
    double dashRemaining = pattern[patternIndex] - patternRemaining;
    double strokeStartX = 0.0;
    double strokeStartY = 0.0;
    bool strokeOpen = false;

    const auto emitSegment = [&](const double x, const double y) {
        if (strokeOpen) DrawLine(bitmap, {strokeStartX, strokeStartY}, {x, y}, width, color, lineCap);
    };

    for (std::size_t i = 1; i < path.size(); ++i) {
        const auto& from = path[i - 1U];
        const auto& to = path[i];
        const double dx = to.x - from.x;
        const double dy = to.y - from.y;
        const double length = std::hypot(dx, dy);
        if (length <= 1.0e-9) continue;
        double remaining = length;
        double px = from.x;
        double py = from.y;
        while (remaining > 0.0) {
            const double step = std::min(remaining, dashRemaining);
            const double nx = px + dx / length * step;
            const double ny = py + dy / length * step;
            if (drawing) {
                if (!strokeOpen) {
                    strokeStartX = px;
                    strokeStartY = py;
                    strokeOpen = true;
                }
                emitSegment(nx, ny);
            } else if (strokeOpen) {
                emitSegment(px, py);
                strokeOpen = false;
            }
            px = nx;
            py = ny;
            remaining -= step;
            dashRemaining -= step;
            if (dashRemaining <= 1.0e-9) {
                patternIndex = (patternIndex + 1U) % pattern.size();
                drawing = (patternIndex % 2U) == 0U;
                dashRemaining = pattern[patternIndex];
                if (strokeOpen) {
                    emitSegment(px, py);
                    strokeOpen = false;
                }
            }
        }
    }
    if (strokeOpen) emitSegment(path.back().x, path.back().y);
}

// Strokes paths honoring the PDF dash pattern. When the pattern is empty the
// path is drawn solid with the usual cap/join handling.
void StrokePathWithDash(PdfBitmap& bitmap, const std::vector<Subpath>& paths,
                        const double width, const PdfRgbaColor color,
                        const int lineCap, const int lineJoin,
                        const double miterLimit,
                        const std::vector<double>& pattern,
                        const double phase) {
    if (pattern.empty()) {
        StrokePath(bitmap, paths, width, color, lineCap, lineJoin, miterLimit);
        return;
    }
    for (const auto& path : paths) {
        StrokeDashedPath(bitmap, path, width, color, lineCap, pattern, phase);
    }
}

void FillPath(PdfBitmap& bitmap, const std::vector<Subpath>& paths,
              const PdfRgbaColor color, const bool evenOdd) {
    struct Crossing final { double x{}; int winding{}; };
    std::vector<Crossing> crossings;
    std::size_t estimatedCrossings = 0;
    double minimumY = static_cast<double>(bitmap.GetHeight());
    double maximumY = 0.0;
    for (const auto& path : paths) {
        estimatedCrossings += path.size();
        for (const auto& point : path) {
            minimumY = std::min(minimumY, point.y);
            maximumY = std::max(maximumY, point.y);
        }
    }
    const auto startY = std::max<std::int32_t>(0, static_cast<std::int32_t>(std::floor(minimumY)));
    const auto endY = std::min<std::int32_t>(
        static_cast<std::int32_t>(bitmap.GetHeight()) - 1,
        static_cast<std::int32_t>(std::ceil(maximumY)));
    if (startY > endY) return;
    crossings.reserve(std::min<std::size_t>(estimatedCrossings, 4096U));
    for (std::int32_t y = startY; y <= endY; ++y) {
        const double scanY = static_cast<double>(y) + 0.5;
        crossings.clear();
        for (const auto& path : paths) {
            if (path.size() < 3U) continue;
            for (std::size_t i = 0, j = path.size() - 1U; i < path.size(); j = i++) {
                const auto& first = path[j];
                const auto& second = path[i];
                if ((first.y > scanY) == (second.y > scanY)) continue;
                const double denominator = second.y - first.y;
                if (std::abs(denominator) < std::numeric_limits<double>::epsilon()) continue;
                crossings.push_back({
                    first.x + (scanY - first.y) * (second.x - first.x) / denominator,
                    second.y > first.y ? 1 : -1 });
            }
        }
        std::sort(crossings.begin(), crossings.end(),
            [](const Crossing& left, const Crossing& right) { return left.x < right.x; });
        if (evenOdd) {
            for (std::size_t i = 1; i < crossings.size(); i += 2U) {
                const auto startX = static_cast<std::int32_t>(std::ceil(crossings[i - 1U].x));
                const auto endX = static_cast<std::int32_t>(std::floor(crossings[i].x));
                for (std::int32_t x = startX; x <= endX; ++x) bitmap.BlendPixel(x, y, color);
            }
        } else {
            int winding = 0;
            for (std::size_t i = 0; i < crossings.size();) {
                const double boundary = crossings[i].x;
                const std::size_t begin = i;
                while (i < crossings.size() &&
                       std::abs(crossings[i].x - boundary) < 1.0e-9) {
                    winding += crossings[i].winding;
                    ++i;
                }
                if (i < crossings.size() && winding != 0) {
                    const auto startX = static_cast<std::int32_t>(std::ceil(boundary));
                    const auto endX = static_cast<std::int32_t>(std::floor(crossings[i].x));
                    for (std::int32_t x = startX; x <= endX; ++x) bitmap.BlendPixel(x, y, color);
                }
                if (i == begin) ++i;
            }
        }
    }
}

using ClipMask = std::vector<std::uint8_t>;

struct ClipRegion final {
    ClipMask mask;
    std::size_t minX{};
    std::size_t minY{};
    std::size_t maxX{};
    std::size_t maxY{};
    bool visible{};
};

void CompositeLayer(PdfBitmap& target, const PdfBitmap& layer, const ClipRegion& clip,
                    const PdfBlendMode blendMode = PdfBlendMode::SourceOver) {
    if (clip.mask.empty() || !clip.visible) return;
    for (std::size_t y = clip.minY; y <= clip.maxY; ++y) {
        for (std::size_t x = clip.minX; x <= clip.maxX; ++x) {
            const auto index = y * target.GetWidth() + x;
            if (clip.mask[index] == 0U) continue;
            const auto pixel = layer.GetPixel(x, y);
            if (pixel.alpha != 0U) target.BlendPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), pixel, blendMode);
        }
    }
}

PdfBlendMode BlendModeFromName(const std::string_view name) {
    if (name == "Multiply") return PdfBlendMode::Multiply;
    if (name == "Screen") return PdfBlendMode::Screen;
    if (name == "Darken") return PdfBlendMode::Darken;
    if (name == "Lighten") return PdfBlendMode::Lighten;
    if (name == "Overlay") return PdfBlendMode::Overlay;
    if (name == "Difference") return PdfBlendMode::Difference;
    if (name == "Exclusion") return PdfBlendMode::Exclusion;
    return PdfBlendMode::SourceOver;
}

// Composites a transparency-group layer into its parent target. The group's
// blend mode and alpha are applied per pixel within the saved clip region.
// Knockout groups clear the destination before each source mark so overlapping
// marks do not accumulate inside the group.
void CompositeGroupLayer(PdfBitmap& target, const PdfBitmap& layer, const ClipRegion& clip,
                         const PdfBlendMode blendMode, const double opacity, const bool knockout) {
    if (clip.mask.empty() || !clip.visible) return;
    for (std::size_t y = clip.minY; y <= clip.maxY; ++y) {
        for (std::size_t x = clip.minX; x <= clip.maxX; ++x) {
            const auto index = y * target.GetWidth() + x;
            if (clip.mask[index] == 0U) continue;
            auto pixel = layer.GetPixel(x, y);
            if (pixel.alpha == 0U) continue;
            if (knockout) target.SetPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), {0U, 0U, 0U, 0U});
            pixel.alpha = static_cast<std::uint8_t>(std::lround(
                static_cast<double>(pixel.alpha) * std::clamp(opacity, 0.0, 1.0)));
            target.BlendPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), pixel, blendMode);
        }
    }
}

// A transparency-group paint layer. While a group is open, painting targets its
// offscreen bitmap and the saved parent/clip/alpha state is restored on exit.
struct GroupLayer final {
    PdfBitmap bitmap;
    PdfBitmap* parent{};
    PdfBlendMode blendMode{PdfBlendMode::SourceOver};
    double alpha{1.0};
    bool isolated{};
    bool knockout{};
    ClipRegion clip;
    bool clipActive{};
    double strokeAlpha{1.0};
    double fillAlpha{1.0};
    PdfBlendMode innerBlend{PdfBlendMode::SourceOver};
    bool innerIsolated{};
    bool innerKnockout{};
    std::vector<double> dashPattern;
    double dashPhase{};
};

ClipRegion CreatePathMask(const std::size_t width, const std::size_t height,
                          const std::vector<Subpath>& paths, const bool evenOdd) {
    PdfBitmap maskBitmap(width, height, {0U, 0U, 0U, 255U});
    FillPath(maskBitmap, paths, {255U, 255U, 255U, 255U}, evenOdd);
    ClipRegion region;
    region.mask.resize(CheckedPixelCount(width, height), 0U);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (maskBitmap.GetPixel(x, y).red == 0U) continue;
            region.mask[y * width + x] = 1U;
            if (!region.visible) {
                region.minX = region.maxX = x;
                region.minY = region.maxY = y;
                region.visible = true;
            } else {
                region.minX = std::min(region.minX, x);
                region.maxX = std::max(region.maxX, x);
                region.minY = std::min(region.minY, y);
                region.maxY = std::max(region.maxY, y);
            }
        }
    }
    return region;
}

std::array<std::uint8_t, 7> Glyph(const char input) noexcept {
    const char c = input >= 'a' && input <= 'z' ? static_cast<char>(input - 'a' + 'A') : input;
    switch (c) {
    case 'A': return {14,17,17,31,17,17,17}; case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14}; case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31}; case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,15}; case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {31,4,4,4,4,4,31}; case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17}; case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17}; case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14}; case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13}; case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30}; case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14}; case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10}; case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4}; case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14}; case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31}; case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2}; case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14}; case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14}; case '9': return {14,17,17,15,1,1,14};
    case '.': return {0,0,0,0,0,12,12}; case ',': return {0,0,0,0,0,12,8};
    case '-': return {0,0,0,31,0,0,0}; case '_': return {0,0,0,0,0,0,31};
    case '/': return {1,2,2,4,8,8,16}; case ':': return {0,12,12,0,12,12,0};
    case '(': return {2,4,8,8,8,4,2}; case ')': return {8,4,2,2,2,4,8};
    case ' ': return {0,0,0,0,0,0,0};
    // Do not paint an opaque fallback box for glyphs that the lightweight
    // bitmap fallback cannot represent. Unsupported font encodings otherwise
    // turn an entire text run into large black rectangles.
    default: return {0,0,0,0,0,0,0};
    }
}

void AddTrueTypeGlyphPaths(std::vector<Subpath>&, const PdfTrueTypeGlyphOutline&,
                           double, double, double, double);

void DrawTrueTypeGlyph(PdfBitmap&, const PdfTrueTypeGlyphOutline&, double, double,
                       double, double, PdfRgbaColor, int);

void DrawCffGlyph(PdfBitmap&, const PdfCffGlyphOutline&, double, double,
                  double, double, PdfRgbaColor, int);

void DrawTextChunk(PdfBitmap& bitmap, const PdfTextChunk& chunk,
                   const CoordinateMapper& mapper, const PdfRgbaColor color,
                   const PdfTrueTypeFont* embeddedFont = nullptr,
                   const PdfFontResource* cffResource = nullptr) {
    if (chunk.utf8Text.empty() || chunk.boundingBox.empty()) return;
    if (chunk.renderingMode == 3 || chunk.renderingMode == 7) return;
    const auto topLeft = mapper.Map(chunk.boundingBox.left, chunk.boundingBox.top);
    const auto bottomRight = mapper.Map(chunk.boundingBox.right, chunk.boundingBox.bottom);
    const double left = std::min(topLeft.x, bottomRight.x);
    const double top = std::min(topLeft.y, bottomRight.y);
    const double width = std::max(1.0, std::abs(bottomRight.x - topLeft.x));
    const double height = std::max(1.0, std::abs(bottomRight.y - topLeft.y));
    const double right = left + width;
    const double bottom = top + height;
    if (right < 0.0 || bottom < 0.0 ||
        left >= static_cast<double>(bitmap.GetWidth()) ||
        top >= static_cast<double>(bitmap.GetHeight())) {
        return;
    }
    const auto codePointLength = [&](const std::size_t index) {
        const auto value = static_cast<unsigned char>(chunk.utf8Text[index]);
        if (value < 0x80U) return std::size_t{1};
        if ((value & 0xE0U) == 0xC0U && index + 1U < chunk.utf8Text.size()) return std::size_t{2};
        if ((value & 0xF0U) == 0xE0U && index + 2U < chunk.utf8Text.size()) return std::size_t{3};
        if ((value & 0xF8U) == 0xF0U && index + 3U < chunk.utf8Text.size()) return std::size_t{4};
        return std::size_t{1};
    };
    std::size_t characterCount = 0;
    for (std::size_t index = 0; index < chunk.utf8Text.size();) {
        index += codePointLength(index);
        ++characterCount;
    }
    characterCount = std::max<std::size_t>(1U, characterCount);
    const double cellWidth = width / static_cast<double>(characterCount);
    const double pixelScaleX = std::max(1.0, (cellWidth * 0.82) / 5.0);
    const double pixelScaleY = std::max(1.0, (height * 0.82) / 7.0);
    double totalEmbeddedAdvance = 0.0;
    if (embeddedFont) {
        for (const auto glyphId : chunk.glyphIds) {
            if (glyphId != std::numeric_limits<std::uint16_t>::max())
                totalEmbeddedAdvance += embeddedFont->GetAdvanceWidth(glyphId);
        }
    }
    double embeddedAdvance = 0.0;
    std::uint16_t previousGlyph = 0xFFFFU;
    std::size_t byteIndex = 0;
    for (std::size_t characterIndex = 0; byteIndex < chunk.utf8Text.size(); ++characterIndex) {
        if (cffResource && characterIndex < chunk.glyphIds.size() &&
            chunk.glyphIds[characterIndex] != std::numeric_limits<std::uint16_t>::max()) {
            const std::uint32_t glyphId = chunk.glyphIds[characterIndex];
            bool outlineRendered = false;
            try {
                const auto outline = cffResource->GetCffGlyphOutline(glyphId);
                if (!outline.IsEmpty()) {
                    const double glyphAdvance = chunk.characterCodes.size() > 0U
                        ? width / static_cast<double>(chunk.characterCodes.size()) : cellWidth;
                    DrawCffGlyph(bitmap, outline, left + embeddedAdvance, top,
                                 glyphAdvance, height, color, chunk.renderingMode);
                    outlineRendered = true;
                }
            } catch (const std::exception&) {
                // Fall back to the lightweight glyph below for malformed outlines.
            }
            embeddedAdvance += chunk.characterCodes.size() > 0U
                ? width / static_cast<double>(chunk.characterCodes.size()) : cellWidth;
            if (outlineRendered) {
                byteIndex += codePointLength(byteIndex);
                continue;
            }
        }
        if (embeddedFont && characterIndex < chunk.glyphIds.size() &&
            chunk.glyphIds[characterIndex] != std::numeric_limits<std::uint16_t>::max()) {
            const double glyphAdvance = totalEmbeddedAdvance > 0.0
                ? width * embeddedFont->GetAdvanceWidth(chunk.glyphIds[characterIndex]) /
                    totalEmbeddedAdvance : cellWidth;
            bool outlineRendered = false;
            try {
                const auto& outline = embeddedFont->GetGlyphOutlineCached(chunk.glyphIds[characterIndex]);
                DrawTrueTypeGlyph(bitmap, outline, left + embeddedAdvance, top,
                                  glyphAdvance, height, color, chunk.renderingMode);
                outlineRendered = !outline.contours.empty();
            } catch (const std::exception&) {
                // Fall back to the lightweight glyph below for malformed outlines.
            }
            // Apply kerning with the previous glyph when a kern pair is defined.
            if (previousGlyph != 0xFFFFU) {
                const double kern = embeddedFont->GetCachedKerning(
                    previousGlyph, chunk.glyphIds[characterIndex], 1.0) * cellWidth;
                embeddedAdvance += kern;
            }
                // GPOS mark-to-base or mark-to-mark attachment: when this glyph
                // is a combining mark whose anchor is defined over the previous
                // glyph, nudge it to sit on the anchor instead of the baseline.
                if (previousGlyph != 0xFFFFU) {
                    auto attachment = embeddedFont->GetMarkBasePosition(
                        chunk.glyphIds[characterIndex], previousGlyph);
                    if (!attachment) {
                        attachment = embeddedFont->GetMarkMarkPosition(
                            chunk.glyphIds[characterIndex], previousGlyph);
                    }
                    if (attachment && outlineRendered) {
                        const auto baseAdvance = embeddedFont->GetAdvanceWidth(previousGlyph);
                        const double unit = baseAdvance > 0
                            ? glyphAdvance / static_cast<double>(baseAdvance)
                            : cellWidth / 1000.0;
                        const double dx = static_cast<double>(attachment->baseX - attachment->markX) * unit;
                        const double dy = static_cast<double>(attachment->baseY - attachment->markY) * unit;
                        // Redraw the outline offset so the mark sits over the base.
                        try {
                            const auto& markOutline = embeddedFont->GetGlyphOutlineCached(
                                chunk.glyphIds[characterIndex]);
                            DrawTrueTypeGlyph(bitmap, markOutline,
                                              left + embeddedAdvance + dx, top - dy,
                                              glyphAdvance, height, color, chunk.renderingMode);
                        } catch (const std::exception&) {
                        }
                    }
                }
            previousGlyph = chunk.glyphIds[characterIndex];
            embeddedAdvance += glyphAdvance;
            if (outlineRendered) {
                byteIndex += codePointLength(byteIndex);
                continue;
            }
        }
        const unsigned char raw = static_cast<unsigned char>(chunk.utf8Text[byteIndex]);
        const auto glyph = raw < 0x80U && raw >= 0x20U
            ? Glyph(static_cast<char>(raw)) : Glyph(' ');
        const double originX = left + (embeddedFont || cffResource ? embeddedAdvance :
            static_cast<double>(characterIndex) * cellWidth);
        byteIndex += codePointLength(byteIndex);
        const double originY = top + (height - 7.0 * pixelScaleY) * 0.5;
        for (std::size_t row = 0; row < glyph.size(); ++row) {
            for (std::size_t column = 0; column < 5U; ++column) {
                if ((glyph[row] & (1U << (4U - column))) == 0U) continue;
                const auto startX = static_cast<std::int32_t>(std::floor(originX + column * pixelScaleX));
                const auto startY = static_cast<std::int32_t>(std::floor(originY + row * pixelScaleY));
                const auto endX = static_cast<std::int32_t>(std::ceil(originX + (column + 1U) * pixelScaleX));
                const auto endY = static_cast<std::int32_t>(std::ceil(originY + (row + 1U) * pixelScaleY));
                const auto clippedStartX = std::max<std::int32_t>(0, startX);
                const auto clippedEndX = std::min<std::int32_t>(
                    static_cast<std::int32_t>(bitmap.GetWidth()), endX);
                const auto clippedStartY = std::max<std::int32_t>(0, startY);
                const auto clippedEndY = std::min<std::int32_t>(
                    static_cast<std::int32_t>(bitmap.GetHeight()), endY);
                for (std::int32_t y = clippedStartY; y < clippedEndY; ++y) {
                    for (std::int32_t x = clippedStartX; x < clippedEndX; ++x) bitmap.BlendPixel(x, y, color);
                }
            }
        }
    }
}

void DrawTrueTypeGlyph(PdfBitmap& bitmap, const PdfTrueTypeGlyphOutline& outline,
                       const double left, const double top, const double width,
                       const double height, const PdfRgbaColor color,
                       const int renderingMode) {
    if (outline.contours.empty() || outline.xMax <= outline.xMin || outline.yMax <= outline.yMin) return;
    if (left + width < 0.0 || top + height < 0.0 ||
        left >= static_cast<double>(bitmap.GetWidth()) ||
        top >= static_cast<double>(bitmap.GetHeight())) return;
    std::vector<Subpath> paths;
    AddTrueTypeGlyphPaths(paths, outline, left, top, width, height);
    const bool fill = renderingMode == 0 || renderingMode == 2 ||
                      renderingMode == 4 || renderingMode == 6;
    const bool stroke = renderingMode == 1 || renderingMode == 2 ||
                        renderingMode == 5 || renderingMode == 6;
    if (fill) FillPath(bitmap, paths, color, false);
    if (stroke) StrokePath(bitmap, paths, std::max(1.0, height / 64.0), color, 2, 0);
}

void AddTrueTypeGlyphPaths(std::vector<Subpath>& paths,
                           const PdfTrueTypeGlyphOutline& outline,
                           const double left, const double top,
                           const double width, const double height) {
    if (outline.contours.empty() || outline.xMax <= outline.xMin || outline.yMax <= outline.yMin) return;
    const double scaleX = width / static_cast<double>(outline.xMax - outline.xMin);
    const double scaleY = height / static_cast<double>(outline.yMax - outline.yMin);
    for (const auto& source : outline.contours) {
        if (source.size() < 2U) continue;
        std::vector<PdfTrueTypePoint> points;
        points.reserve(source.size() * 2U);
        for (std::size_t i = 0; i < source.size(); ++i) {
            const auto& point = source[i];
            const auto& next = source[(i + 1U) % source.size()];
            points.push_back(point);
            if (!point.onCurve && !next.onCurve) {
                points.push_back({static_cast<std::int16_t>((point.x + next.x) / 2),
                                  static_cast<std::int16_t>((point.y + next.y) / 2), true});
            }
        }
        if (!points.front().onCurve) {
            const auto& last = points.back();
            points.insert(points.begin(), last.onCurve ? last : PdfTrueTypePoint{
                static_cast<std::int16_t>((points.front().x + last.x) / 2),
                static_cast<std::int16_t>((points.front().y + last.y) / 2), true});
        }
        const auto map = [&](const PdfTrueTypePoint& value) {
            return DevicePoint{left + (value.x - outline.xMin) * scaleX,
                                top + (outline.yMax - value.y) * scaleY};
        };
        Subpath path{map(points.front())};
        for (std::size_t i = 1; i <= points.size(); ++i) {
            const auto& point = points[i % points.size()];
            const auto& next = points[(i + 1U) % points.size()];
            if (point.onCurve) {
                path.push_back(map(point));
                continue;
            }
            const auto control = map(point);
            const auto end = map(next);
            const auto start = path.back();
            const int steps = std::clamp(static_cast<int>(std::ceil(std::max(
                std::hypot(control.x - start.x, control.y - start.y),
                std::hypot(end.x - control.x, end.y - control.y)) / 2.0)), 4, 32);
            for (int step = 1; step <= steps; ++step) {
                const double t = static_cast<double>(step) / steps;
                const double u = 1.0 - t;
                path.push_back({u*u*start.x + 2*u*t*control.x + t*t*end.x,
                                u*u*start.y + 2*u*t*control.y + t*t*end.y});
            }
        }
        if (path.size() >= 3U) {
            if (path.front().x != path.back().x || path.front().y != path.back().y) path.push_back(path.front());
            paths.push_back(std::move(path));
        }
    }
}

void AddCffGlyphPaths(std::vector<Subpath>& paths, const PdfCffGlyphOutline& outline,
                      const double left, const double top,
                      const double width, const double height) {
    if (outline.IsEmpty() || outline.xMax <= outline.xMin || outline.yMax <= outline.yMin) return;
    const double scaleX = width / (outline.xMax - outline.xMin);
    const double scaleY = height / (outline.yMax - outline.yMin);
    const auto map = [&](const double x, const double y) {
        return DevicePoint{left + (x - outline.xMin) * scaleX,
                           top + (outline.yMax - y) * scaleY};
    };
    Subpath path;
    for (const auto& segment : outline.segments) {
        if (segment.type == PdfCffOutlineSegment::Type::Move) {
            if (!path.empty() && path.size() >= 3U) {
                if (path.front().x != path.back().x || path.front().y != path.back().y) path.push_back(path.front());
                paths.push_back(std::move(path));
            }
            path.clear();
            path.push_back(map(segment.x1, segment.y1));
            continue;
        }
        if (segment.type == PdfCffOutlineSegment::Type::Line) {
            if (!path.empty()) path.push_back(map(segment.x1, segment.y1));
            continue;
        }
        // Cubic Bezier flattening: start is the last point on the path.
        if (path.empty()) continue;
        const auto start = path.back();
        const auto c1 = map(segment.x1, segment.y1);
        const auto c2 = map(segment.x2, segment.y2);
        const auto end = map(segment.x3, segment.y3);
        const int steps = std::clamp(static_cast<int>(std::ceil(std::max(
            std::hypot(c1.x - start.x, c1.y - start.y) +
            std::hypot(c2.x - c1.x, c2.y - c1.y),
            std::hypot(end.x - c2.x, end.y - c2.y)) / 3.0)), 4, 48);
        for (int step = 1; step <= steps; ++step) {
            const double t = static_cast<double>(step) / steps;
            const double u = 1.0 - t;
            path.push_back({u*u*u*start.x + 3*u*u*t*c1.x + 3*u*t*t*c2.x + t*t*t*end.x,
                            u*u*u*start.y + 3*u*u*t*c1.y + 3*u*t*t*c2.y + t*t*t*end.y});
        }
    }
    if (path.size() >= 3U) {
        if (path.front().x != path.back().x || path.front().y != path.back().y) path.push_back(path.front());
        paths.push_back(std::move(path));
    }
}

void DrawCffGlyph(PdfBitmap& bitmap, const PdfCffGlyphOutline& outline,
                  const double left, const double top, const double width,
                  const double height, const PdfRgbaColor color,
                  const int renderingMode) {
    if (outline.IsEmpty() || outline.xMax <= outline.xMin || outline.yMax <= outline.yMin) return;
    if (left + width < 0.0 || top + height < 0.0 ||
        left >= static_cast<double>(bitmap.GetWidth()) ||
        top >= static_cast<double>(bitmap.GetHeight())) return;
    std::vector<Subpath> paths;
    AddCffGlyphPaths(paths, outline, left, top, width, height);
    const bool fill = renderingMode == 0 || renderingMode == 2 ||
                      renderingMode == 4 || renderingMode == 6;
    const bool stroke = renderingMode == 1 || renderingMode == 2 ||
                        renderingMode == 5 || renderingMode == 6;
    if (fill) FillPath(bitmap, paths, color, false);
    if (stroke) StrokePath(bitmap, paths, std::max(1.0, height / 64.0), color, 2, 0);
}

void IntersectClip(PdfBitmap& bitmap, ClipRegion& clip,
                   const std::vector<Subpath>& paths, const bool evenOdd) {
    const auto newMask = CreatePathMask(bitmap.GetWidth(), bitmap.GetHeight(), paths, evenOdd);
    if (!newMask.visible) {
        clip.visible = false;
        return;
    }
    const auto minX = std::max(clip.minX, newMask.minX);
    const auto minY = std::max(clip.minY, newMask.minY);
    const auto maxX = std::min(clip.maxX, newMask.maxX);
    const auto maxY = std::min(clip.maxY, newMask.maxY);
    if (minX > maxX || minY > maxY) {
        clip.visible = false;
        return;
    }
    for (std::size_t y = minY; y <= maxY; ++y) {
        for (std::size_t x = minX; x <= maxX; ++x) {
            const auto index = y * bitmap.GetWidth() + x;
            clip.mask[index] = static_cast<std::uint8_t>(clip.mask[index] != 0U && newMask.mask[index] != 0U);
        }
    }
    clip.minX = minX;
    clip.minY = minY;
    clip.maxX = maxX;
    clip.maxY = maxY;
    clip.visible = true;
}


PdfRgbaColor ImagePixel(const PdfExtractedImage& image, const std::size_t x, const std::size_t y,
                        const double u = 0.0, const double v = 0.0) {
    if (!image.info.decoded || image.info.width == 0U || image.info.height == 0U) return {};
    const auto pixelIndex = y * static_cast<std::size_t>(image.info.width) + x;
    PdfRgbaColor color{};
    if (image.info.colorSpace == PdfImageColorSpace::Indexed) {
        const auto bits = image.info.bitsPerComponent;
        const auto packed = image.decodedBytes;
        std::size_t index = 0U;
        if (bits == 8U) {
            if (pixelIndex >= packed.size()) return {};
            index = std::to_integer<std::uint8_t>(packed[pixelIndex]);
        } else if (bits == 1U || bits == 2U || bits == 4U) {
            const auto samplesPerByte = 8U / bits;
            const auto byteIndex = pixelIndex / samplesPerByte;
            if (byteIndex >= packed.size()) return {};
            const auto shift = (samplesPerByte - 1U - (pixelIndex % samplesPerByte)) * bits;
            index = (std::to_integer<std::uint8_t>(packed[byteIndex]) >> shift) & ((1U << bits) - 1U);
        } else return {};
        index = std::min<std::size_t>(index, image.info.colorSpaceHighValue);
        const auto offset = index * 3U;
        if (offset + 2U >= image.info.colorSpaceData.size()) return {};
        color = {std::to_integer<std::uint8_t>(image.info.colorSpaceData[offset]),
                 std::to_integer<std::uint8_t>(image.info.colorSpaceData[offset + 1U]),
                 std::to_integer<std::uint8_t>(image.info.colorSpaceData[offset + 2U]), 255U};
    } else if (image.info.colorSpace == PdfImageColorSpace::DeviceGray) {
        if (pixelIndex >= image.decodedBytes.size()) return {};
        const auto value = std::to_integer<std::uint8_t>(image.decodedBytes[pixelIndex]);
        color = {value, value, value, 255U};
    } else if (image.info.colorSpace == PdfImageColorSpace::DeviceRGB) {
        const auto offset = pixelIndex * 3U;
        if (offset + 2U >= image.decodedBytes.size()) return {};
        color = {
            std::to_integer<std::uint8_t>(image.decodedBytes[offset]),
            std::to_integer<std::uint8_t>(image.decodedBytes[offset + 1U]),
            std::to_integer<std::uint8_t>(image.decodedBytes[offset + 2U]),
            255U
        };
    } else if (image.info.colorSpace == PdfImageColorSpace::DeviceCMYK) {
        const auto offset = pixelIndex * 4U;
        if (offset + 3U >= image.decodedBytes.size()) return {};
        const double c = std::to_integer<std::uint8_t>(image.decodedBytes[offset]) / 255.0;
        const double m = std::to_integer<std::uint8_t>(image.decodedBytes[offset + 1U]) / 255.0;
        const double yv = std::to_integer<std::uint8_t>(image.decodedBytes[offset + 2U]) / 255.0;
        const double k = std::to_integer<std::uint8_t>(image.decodedBytes[offset + 3U]) / 255.0;
        const auto channel = [](const double value) {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
        };
        color = {channel((1.0 - c) * (1.0 - k)), channel((1.0 - m) * (1.0 - k)),
                 channel((1.0 - yv) * (1.0 - k)), 255U};
    } else if (image.info.colorSpace == PdfImageColorSpace::ICCBased) {
        // ICCBased images render through an identity transform on the profile's
        // component count: 1 = gray, 3 = RGB, 4 = CMYK. Full ICC profile
        // transforms are not yet implemented; the identity fallback keeps the
        // image visible and color-reasonable for common profiles.
        const auto components = image.info.colorSpaceComponents;
        if (components == 1U) {
            if (pixelIndex >= image.decodedBytes.size()) return {};
            const auto value = std::to_integer<std::uint8_t>(image.decodedBytes[pixelIndex]);
            color = {value, value, value, 255U};
        } else if (components == 3U) {
            const auto offset = pixelIndex * 3U;
            if (offset + 2U >= image.decodedBytes.size()) return {};
            // Detect a standard sRGB ICC profile and apply its transfer curve so
            // ICCBased RGB images render with proper gamma instead of a raw pass.
            const double gamma = sRgbGammaOfProfile(image.info.iccProfileBytes);
            if (gamma > 0.0) {
                const auto srgb = [&](const std::uint8_t channel) {
                    // ICC values are already in the profile's linear space; the
                    // sRGB profile stores an sRGB TRC, so re-encode to display.
                    const double linear = channel / 255.0;
                    double encoded = std::pow(linear, 1.0 / gamma);
                    return static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0, 1.0) * 255.0));
                };
                color = {srgb(std::to_integer<std::uint8_t>(image.decodedBytes[offset])),
                         srgb(std::to_integer<std::uint8_t>(image.decodedBytes[offset + 1U])),
                         srgb(std::to_integer<std::uint8_t>(image.decodedBytes[offset + 2U])), 255U};
            } else {
                color = {std::to_integer<std::uint8_t>(image.decodedBytes[offset]),
                         std::to_integer<std::uint8_t>(image.decodedBytes[offset + 1U]),
                         std::to_integer<std::uint8_t>(image.decodedBytes[offset + 2U]), 255U};
            }
        } else if (components == 4U) {
            const auto offset = pixelIndex * 4U;
            if (offset + 3U >= image.decodedBytes.size()) return {};
            const double c = std::to_integer<std::uint8_t>(image.decodedBytes[offset]) / 255.0;
            const double m = std::to_integer<std::uint8_t>(image.decodedBytes[offset + 1U]) / 255.0;
            const double yv = std::to_integer<std::uint8_t>(image.decodedBytes[offset + 2U]) / 255.0;
            const double k = std::to_integer<std::uint8_t>(image.decodedBytes[offset + 3U]) / 255.0;
            const auto channel = [](const double value) {
                return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
            };
            color = {channel((1.0 - c) * (1.0 - k)), channel((1.0 - m) * (1.0 - k)),
                     channel((1.0 - yv) * (1.0 - k)), 255U};
        } else {
            return {};
        }
    } else if ((image.info.colorSpace == PdfImageColorSpace::Separation ||
                image.info.colorSpace == PdfImageColorSpace::DeviceN) &&
               image.info.hasSeparationAlternate) {
        // Both Separation and DeviceN paint through a tint transform into an
        // alternate (Gray/RGB/CMYK) space. The first component value drives the
        // shared exponential function; DeviceN with a per-component function
        // array falls back to the alternate color at the first tint.
        const auto alternate = image.info.separationAlternate[0];
        const double tint = pixelIndex < image.decodedBytes.size()
            ? std::to_integer<std::uint8_t>(image.decodedBytes[pixelIndex]) / 255.0
            : 1.0;
        std::vector<double> transformed;
        if (image.info.hasSeparationFunction) {
            transformed = PdfExponentialFunction(image.info.separationC0,
                image.info.separationC1, image.info.separationExponent).Evaluate(tint);
        }
        const auto channel = [](const double value) {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
        };
        if (alternate == 1U) {
            // DeviceGray alternate: gray = transformed value (or tint fallback).
            const double gray = transformed.empty() ? tint : transformed[0];
            color = {channel(gray), channel(gray), channel(gray), 255U};
        } else if (alternate == 3U) {
            if (transformed.size() >= 3U) {
                color = {channel(transformed[0]), channel(transformed[1]), channel(transformed[2]), 255U};
            } else {
                color = {channel(1.0 - tint), channel(1.0 - tint), channel(1.0 - tint), 255U};
            }
        } else if (alternate == 4U) {
            // DeviceCMYK alternate: convert using the transformed (or tint)
            // values as CMYK.
            const double c = transformed.size() >= 4U ? transformed[0] : 0.0;
            const double m = transformed.size() >= 4U ? transformed[1] : 0.0;
            const double yv = transformed.size() >= 4U ? transformed[2] : 0.0;
            const double k = transformed.size() >= 4U ? transformed[3] : (1.0 - tint);
            color = {channel((1.0 - c) * (1.0 - k)), channel((1.0 - m) * (1.0 - k)),
                     channel((1.0 - yv) * (1.0 - k)), 255U};
        } else {
            return {};
        }
    } else {
        return {};
    }
    if (!image.alphaBytes.empty()) {
        const auto maskAlpha = [&]() {
            const std::size_t maskWidth = image.info.softMaskWidth != 0U ? image.info.softMaskWidth : image.info.width;
            const std::size_t maskHeight = image.info.softMaskHeight != 0U ? image.info.softMaskHeight : image.info.height;
            if (maskWidth == image.info.width && maskHeight == image.info.height && pixelIndex < image.alphaBytes.size()) {
                return std::to_integer<std::uint8_t>(image.alphaBytes[pixelIndex]);
            }
            if (maskWidth == 0U || maskHeight == 0U || image.alphaBytes.size() < maskWidth * maskHeight) return std::uint8_t{255U};
            const std::size_t maskX = std::min<std::size_t>(maskWidth - 1U,
                static_cast<std::size_t>(std::lround(std::clamp(u, 0.0, 1.0) * static_cast<double>(maskWidth - 1U))));
            const std::size_t maskY = std::min<std::size_t>(maskHeight - 1U,
                static_cast<std::size_t>(std::lround(std::clamp(1.0 - v, 0.0, 1.0) * static_cast<double>(maskHeight - 1U))));
            return std::to_integer<std::uint8_t>(image.alphaBytes[maskY * maskWidth + maskX]);
        }();
        color.alpha = static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(color.alpha) * maskAlpha + 127U) / 255U);
    }
    return color;
}

PdfRgbaColor SampleImage(const PdfExtractedImage& image, const double u, const double v, const bool interpolate) {
    const double sourceX = std::clamp(u, 0.0, 1.0) * static_cast<double>(image.info.width - 1U);
    const double sourceY = std::clamp(1.0 - v, 0.0, 1.0) * static_cast<double>(image.info.height - 1U);
    if (!interpolate) {
        return ImagePixel(image, static_cast<std::size_t>(std::lround(sourceX)),
                          static_cast<std::size_t>(std::lround(sourceY)), u, v);
    }
    const auto x0 = static_cast<std::size_t>(std::floor(sourceX));
    const auto y0 = static_cast<std::size_t>(std::floor(sourceY));
    const auto x1 = std::min<std::size_t>(x0 + 1U, image.info.width - 1U);
    const auto y1 = std::min<std::size_t>(y0 + 1U, image.info.height - 1U);
    const double tx = sourceX - static_cast<double>(x0);
    const double ty = sourceY - static_cast<double>(y0);
    const auto p00 = ImagePixel(image, x0, y0, u, v);
    const auto p10 = ImagePixel(image, x1, y0, u, v);
    const auto p01 = ImagePixel(image, x0, y1, u, v);
    const auto p11 = ImagePixel(image, x1, y1, u, v);
    const auto blend = [&](const double a, const double b, const double c, const double d) {
        const double top = static_cast<double>(a) + (static_cast<double>(b) - a) * tx;
        const double bottom = static_cast<double>(c) + (static_cast<double>(d) - c) * tx;
        return top + (bottom - top) * ty;
    };
    const auto alpha = blend(p00.alpha, p10.alpha, p01.alpha, p11.alpha);
    const auto red = blend(p00.red * p00.alpha, p10.red * p10.alpha,
                           p01.red * p01.alpha, p11.red * p11.alpha);
    const auto green = blend(p00.green * p00.alpha, p10.green * p10.alpha,
                             p01.green * p01.alpha, p11.green * p11.alpha);
    const auto blue = blend(p00.blue * p00.alpha, p10.blue * p10.alpha,
                            p01.blue * p01.alpha, p11.blue * p11.alpha);
    if (alpha <= 0.0) return {};
    const auto unpremultiply = [alpha](const double value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value / alpha, 0.0, 255.0)));
    };
    return {unpremultiply(red), unpremultiply(green), unpremultiply(blue),
            static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.0, 255.0)))};
}

void DrawImage(PdfBitmap& bitmap, const PdfExtractedImage& image, const CoordinateMapper& mapper,
               const bool interpolate, const double alpha = 1.0) {
    if (!image.info.decoded || image.info.boundingBox.empty() || image.info.width == 0U || image.info.height == 0U) return;
    const auto first = mapper.Map(image.info.boundingBox.left, image.info.boundingBox.top);
    const auto second = mapper.Map(image.info.boundingBox.right, image.info.boundingBox.bottom);
    const double left = std::min(first.x, second.x);
    const double right = std::max(first.x, second.x);
    const double top = std::min(first.y, second.y);
    const double bottom = std::max(first.y, second.y);
    const auto startX = std::max<std::int32_t>(0, static_cast<std::int32_t>(std::floor(left)));
    const auto endX = std::min<std::int32_t>(static_cast<std::int32_t>(bitmap.GetWidth()) - 1,
                                             static_cast<std::int32_t>(std::ceil(right)));
    const auto startY = std::max<std::int32_t>(0, static_cast<std::int32_t>(std::floor(top)));
    const auto endY = std::min<std::int32_t>(static_cast<std::int32_t>(bitmap.GetHeight()) - 1,
                                             static_cast<std::int32_t>(std::ceil(bottom)));
    const double width = std::max(1.0, right - left);
    const double height = std::max(1.0, bottom - top);
    for (std::int32_t y = startY; y <= endY; ++y) {
        const double v = (static_cast<double>(y) + 0.5 - top) / height;
        for (std::int32_t x = startX; x <= endX; ++x) {
            const double u = (static_cast<double>(x) + 0.5 - left) / width;
            auto pixel = SampleImage(image, u, v, interpolate);
            pixel.alpha = static_cast<std::uint8_t>(std::lround(
                static_cast<double>(pixel.alpha) * std::clamp(alpha, 0.0, 1.0)));
            bitmap.BlendPixelInBounds(static_cast<std::size_t>(x), static_cast<std::size_t>(y), pixel);
        }
    }
}

PdfBitmap Downsample(const PdfBitmap& source, const std::size_t samples, const PdfRgbaColor background) {
    if (samples <= 1U) return source;
    // Use ceiling division so tiny pages never collapse to a zero-sized bitmap
    // when supersampling is enabled. Clamp the sample footprint at the source
    // edge because the final block can be smaller than the full sample square.
    const std::size_t width = (source.GetWidth() + samples - 1U) / samples;
    const std::size_t height = (source.GetHeight() + samples - 1U) / samples;
    PdfBitmap target(width, height, background);
    const auto average = [&](const std::size_t x, const std::size_t y) {
        std::uint64_t red{}, green{}, blue{}, alpha{};
        std::uint64_t count{};
        for (std::size_t sy = 0; sy < samples; ++sy) {
            for (std::size_t sx = 0; sx < samples; ++sx) {
                const std::size_t sourceX = x * samples + sx;
                const std::size_t sourceY = y * samples + sy;
                if (sourceX >= source.GetWidth() || sourceY >= source.GetHeight()) continue;
                const auto pixel = source.GetPixel(sourceX, sourceY);
                red += static_cast<std::uint64_t>(pixel.red) * pixel.alpha;
                green += static_cast<std::uint64_t>(pixel.green) * pixel.alpha;
                blue += static_cast<std::uint64_t>(pixel.blue) * pixel.alpha;
                alpha += pixel.alpha;
                ++count;
            }
        }
        if (count == 0U) return background;
        const auto outputAlpha = alpha / count;
        if (outputAlpha == 0U) return PdfRgbaColor{0U, 0U, 0U, 0U};
        const auto unpremultiply = [alpha](const std::uint64_t value) {
            return static_cast<std::uint8_t>(std::min<std::uint64_t>(255U,
                (value * 255U + alpha / 2U) / alpha));
        };
        return PdfRgbaColor{unpremultiply(red), unpremultiply(green),
                            unpremultiply(blue), static_cast<std::uint8_t>(outputAlpha)};
    };
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) target.SetPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), average(x,y));
    }
    return target;
}

void CloseSubpath(Subpath& path) {
    if (path.size() > 1U && (path.front().x != path.back().x || path.front().y != path.back().y)) {
        path.push_back(path.front());
    }
}

void PaintAxialShading(PdfBitmap& bitmap, const PdfAxialShading& shading,
                       const CoordinateMapper& mapper, const double alpha) {
    for (std::size_t y = 0; y < bitmap.GetHeight(); ++y) {
        for (std::size_t x = 0; x < bitmap.GetWidth(); ++x) {
            const auto point = mapper.Map(static_cast<double>(x), static_cast<double>(bitmap.GetHeight() - y));
            const auto values = shading.Sample(point.x, point.y);
            if (!values || values->size() < 3U) continue;
            const auto channel = [alpha](const double value) {
                return static_cast<std::uint8_t>(std::lround(std::clamp(value * alpha, 0.0, 1.0) * 255.0));
            };
            bitmap.BlendPixelInBounds(static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                                      {channel((*values)[0]), channel((*values)[1]),
                                       channel((*values)[2]), 255U});
        }
    }
}

void PaintRadialShading(PdfBitmap& bitmap, const PdfRadialShading& shading,
                        const CoordinateMapper& mapper, const double alpha) {
    for (std::size_t y = 0; y < bitmap.GetHeight(); ++y) {
        for (std::size_t x = 0; x < bitmap.GetWidth(); ++x) {
            const auto point = mapper.Map(static_cast<double>(x), static_cast<double>(bitmap.GetHeight() - y));
            const auto values = shading.Sample(point.x, point.y);
            if (!values || values->size() < 3U) continue;
            const auto channel = [alpha](const double value) {
                return static_cast<std::uint8_t>(std::lround(std::clamp(value * alpha, 0.0, 1.0) * 255.0));
            };
            bitmap.BlendPixelInBounds(static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                                      {channel((*values)[0]), channel((*values)[1]),
                                       channel((*values)[2]), 255U});
        }
    }
}

// Computes the device-space bounding box that a set of subpaths covers.
void PathBounds(const std::vector<Subpath>& paths, double& minX, double& minY,
                double& maxX, double& maxY) {
    minX = std::numeric_limits<double>::max();
    minY = std::numeric_limits<double>::max();
    maxX = -std::numeric_limits<double>::max();
    maxY = -std::numeric_limits<double>::max();
    bool any = false;
    for (const auto& path : paths) {
        for (const auto& point : path) {
            minX = std::min(minX, point.x); minY = std::min(minY, point.y);
            maxX = std::max(maxX, point.x); maxY = std::max(maxY, point.y);
            any = true;
        }
    }
    if (!any) { minX = 0.0; minY = 0.0; maxX = 0.0; maxY = 0.0; }
}

// Renders a tiling pattern by replaying the tile content stream for every tile
// that intersects the filled region. The tile's /Matrix and the pattern color
// space transform map pattern coordinates into user space, then the region's
// CTM (already folded into `paths` by the caller) positions them on the page.
void PaintTilingPattern(PdfBitmap& bitmap, const PdfResolvedPattern& pattern,
                        const std::vector<Subpath>& paths, const CoordinateMapper& mapper,
                        const ClipRegion& clip, const std::array<double, 6>& ctm,
                        const double alpha, const bool renderPaths) {
    if (!renderPaths) return;
    if (pattern.patternType != 1U) return;
    double minX, minY, maxX, maxY;
    PathBounds(paths, minX, minY, maxX, maxY);
    // Shrink to the visible bitmap region.
    minX = std::max(minX, 0.0); minY = std::max(minY, 0.0);
    maxX = std::min(maxX, static_cast<double>(bitmap.GetWidth()) - 1.0);
    maxY = std::min(maxY, static_cast<double>(bitmap.GetHeight()) - 1.0);
    if (minX > maxX || minY > maxY) return;

    // Parse the tile content once into a display list.
    PdfContentProcessor processor;
    PdfDisplayList tile;
    processor.SetHandler([&](const PdfContentEvent& event) { tile.Add(event); });
    try {
        processor.Process(pattern.tiling.content);
    } catch (const PdfException&) {
        return;
    }
    if (tile.Empty()) return;

    const PdfRectangle& bbox = pattern.tiling.boundingBox;
    const double tileWidth = std::max(0.01, bbox.width());
    const double tileHeight = std::max(0.01, bbox.height());
    const double xStep = pattern.tiling.xStep > 0.0 ? pattern.tiling.xStep : tileWidth;
    const double yStep = pattern.tiling.yStep > 0.0 ? pattern.tiling.yStep : tileHeight;

    // Pattern-to-device transform: device = ctm * patternMatrix * tilePoint.
    const std::array<double, 6>& p = pattern.tiling.matrix;
    const double originUserX = ctm[0] * p[4] + ctm[2] * p[5] + ctm[4];
    const double originUserY = ctm[1] * p[4] + ctm[3] * p[5] + ctm[5];
    const auto originDevice = mapper.Map(originUserX, originUserY);

    const double stepXDevice = std::hypot(ctm[0] * p[0] * xStep, ctm[1] * p[0] * xStep);
    const double stepYDevice = std::hypot(ctm[2] * p[3] * yStep, ctm[3] * p[3] * yStep);

    // Determine the tile range that covers the region.
    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    const std::int32_t startTileX = static_cast<std::int32_t>(
        std::floor((minX - originDevice.x) / std::max(stepXDevice, 1.0)));
    const std::int32_t startTileY = static_cast<std::int32_t>(
        std::floor((minY - originDevice.y) / std::max(stepYDevice, 1.0)));
    const std::int32_t tileCountX = static_cast<std::int32_t>(
        std::ceil(spanX / std::max(stepXDevice, 1.0))) + 2;
    const std::int32_t tileCountY = static_cast<std::int32_t>(
        std::ceil(spanY / std::max(stepYDevice, 1.0))) + 2;

    for (std::int32_t ty = startTileY; ty < startTileY + tileCountY; ++ty) {
        for (std::int32_t tx = startTileX; tx < startTileX + tileCountX; ++tx) {
            const double tileOriginDeviceX = originDevice.x + static_cast<double>(tx) * stepXDevice;
            const double tileOriginDeviceY = originDevice.y + static_cast<double>(ty) * stepYDevice;
            // Skip tiles that miss the region entirely.
            if (tileOriginDeviceX > maxX || tileOriginDeviceY > maxY ||
                tileOriginDeviceX + stepXDevice < minX || tileOriginDeviceY + stepYDevice < minY) {
                continue;
            }
            // Replay the tile drawing with the tile origin offset applied.
            std::vector<Subpath> tilePaths;
            PdfRgbaColor tileFill{0U, 0U, 0U, 255U};
            tile.Replay([&](const PdfContentEvent& event) {
                if (event.type == PdfContentEventType::SetFillColor) {
                    const auto color = ToColor(event.textState.fillColor);
                    tileFill = {color.red, color.green, color.blue, 255U};
                }
            });
            tile.Replay([&](const PdfContentEvent& event) {
                if (event.type != PdfContentEventType::RenderPath) return;
                if (event.operation == "m" && event.numbers.size() >= 2U) {
                    tilePaths.emplace_back();
                    const auto point = Transform(event.textState.currentTransformationMatrix,
                                                 event.numbers[0], event.numbers[1]);
                    tilePaths.back().push_back({
                        tileOriginDeviceX + point[0],
                        tileOriginDeviceY + point[1]});
                } else if ((event.operation == "l" || event.operation == "m") && event.numbers.size() >= 2U && !tilePaths.empty()) {
                    const auto point = Transform(event.textState.currentTransformationMatrix,
                                                 event.numbers[0], event.numbers[1]);
                    tilePaths.back().push_back({
                        tileOriginDeviceX + point[0],
                        tileOriginDeviceY + point[1]});
                } else if (event.operation == "re" && event.numbers.size() >= 4U) {
                    tilePaths.emplace_back();
                    const auto origin = Transform(event.textState.currentTransformationMatrix,
                                                  event.numbers[0], event.numbers[1]);
                    const double w = event.numbers[2];
                    const double h = event.numbers[3];
                    tilePaths.back().push_back({tileOriginDeviceX + origin[0], tileOriginDeviceY + origin[1]});
                    tilePaths.back().push_back({tileOriginDeviceX + origin[0] + w, tileOriginDeviceY + origin[1]});
                    tilePaths.back().push_back({tileOriginDeviceX + origin[0] + w, tileOriginDeviceY + origin[1] + h});
                    tilePaths.back().push_back({tileOriginDeviceX + origin[0], tileOriginDeviceY + origin[1] + h});
                    tilePaths.back().push_back(tilePaths.back().front());
                }
            });
            if (!tilePaths.empty()) {
                // Fill the tile with the color from the tile content, blended at
                // the requested opacity.
                FillPath(bitmap, tilePaths, {tileFill.red, tileFill.green, tileFill.blue,
                    static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0))}, false);
            }
        }
    }
}

// Returns the display gamma (2.2 for sRGB TRC) encoded in a standard sRGB ICC
// profile, or 0.0 when the profile is not a recognized sRGB RGB profile.
double sRgbGammaOfProfile(const std::vector<std::byte>& profile) {
    if (profile.size() < 132U) return 0.0;
    // ICC header: color space signature at offset 16 (4 bytes).
    const auto fourCc = [&](const std::size_t offset) -> std::uint32_t {
        if (offset + 4U > profile.size()) return std::uint32_t{0};
        return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[offset])) << 24U) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[offset + 1U])) << 16U) |
               (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[offset + 2U])) << 8U) |
               static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[offset + 3U]));
    };
    constexpr std::uint32_t kSrgb = 0x73524742; // 'sRGB'
    constexpr std::uint32_t kRgb = 0x52474220; // 'RGB '
    if (fourCc(0U) != kSrgb && fourCc(20U) != kSrgb) return 0.0;
    if (fourCc(12U) != kRgb && fourCc(16U) != kRgb) return 0.0;
    // Tag table: count at offset 128, then 12-byte entries {sig, offset, size}.
    const std::uint32_t tagCount = fourCc(128U);
    for (std::uint32_t t = 0; t < tagCount; ++t) {
        const std::size_t entry = 132U + std::size_t(t) * 12U;
        if (entry + 12U > profile.size()) break;
        // 'rTRC' = red tone reproduction curve.
        if (fourCc(entry) != 0x72545243U) continue;
        const std::uint32_t curveOffset = fourCc(entry + 4U);
        const std::uint32_t curveSize = fourCc(entry + 8U);
        if (curveSize < 12U || curveOffset + curveSize > profile.size()) continue;
        if (fourCc(curveOffset) != 0x63757276U) continue;
        const std::uint32_t entryCount =
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[curveOffset + 4U])) << 24U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[curveOffset + 5U])) << 16U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[curveOffset + 6U])) << 8U) |
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[curveOffset + 7U]));
        if (entryCount == 0U && curveOffset + 8U + 4U <= profile.size()) {
            // u8Fixed8 gamma (e.g. 0x0233 = 2.2).
            const std::uint32_t raw =
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[curveOffset + 8U])) << 8U) |
                static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(profile[curveOffset + 9U]));
            const double gamma = raw / 256.0;
            if (gamma > 0.5 && gamma < 4.0) return gamma;
        }
    }
    return 0.0;
}

} // namespace

PdfBitmap PdfPageRenderer::Render(
    const PdfDocument& document,
    const std::size_t pageIndex,
    const PdfRenderOptions& options) {
    if (!std::isfinite(options.dpi) || options.dpi <= 0.0) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Render DPI must be a finite positive value.");
    }
    const auto page = document.GetPage(pageIndex);
    const PdfRectangle cropBox = page.GetCropBox();
    const PdfRectangle mediaBox = page.GetMediaBox();
    const PdfRectangle box = options.honorCropBox && !cropBox.empty() ? cropBox : mediaBox;
    if (box.empty()) throw PdfException(PdfErrorCode::InvalidPageTree, "Page render box is empty.");
    if (options.antiAliasSamples == 0U || options.antiAliasSamples > 4U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Anti-alias sample count must be between 1 and 4.");
    }
    const double scale = (options.dpi / 72.0) * static_cast<double>(options.antiAliasSamples);
    const int rotation = ((page.GetRotation() % 360) + 360) % 360;
    const double pointWidth = rotation == 90 || rotation == 270 ? box.height() : box.width();
    const double pointHeight = rotation == 90 || rotation == 270 ? box.width() : box.height();
    const auto width = static_cast<std::size_t>(std::max(1.0, std::ceil(pointWidth * scale)));
    const auto height = static_cast<std::size_t>(std::max(1.0, std::ceil(pointHeight * scale)));
    const auto finalWidth = width / options.antiAliasSamples;
    const auto finalHeight = height / options.antiAliasSamples;
    if (finalWidth > options.maximumDimension || finalHeight > options.maximumDimension ||
        width > options.maximumDimension * options.antiAliasSamples ||
        height > options.maximumDimension * options.antiAliasSamples) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Rendered page exceeds the configured maximum dimension.");
    }

    PdfBitmap bitmap(width, height, options.background);
    const CoordinateMapper mapper(box, rotation, scale);
    const auto displayList = document.BuildPageDisplayList(pageIndex);


    // Unified replay that keeps content order between vector paths and images
    // and applies transparency-group compositing through an offscreen layer
    // stack. Text remains a separate pass driven by extracted geometry.
    if (options.renderPaths || options.renderImages) {
        std::vector<Subpath> paths;
        Subpath* current{};
        bool pendingClip{};
        bool pendingClipEvenOdd{};
        bool clipActive{};
        double strokeAlpha = 1.0;
        double fillAlpha = 1.0;
        PdfBlendMode blendMode = PdfBlendMode::SourceOver;
        bool transparencyIsolated = false;
        bool transparencyKnockout = false;
        std::vector<double> dashPattern;
        double dashPhase = 0.0;
        std::string fillPatternName;
        std::string strokePatternName;
        std::unordered_map<std::uint32_t, PdfExtractedImage> imageCache;
        ClipRegion clip;
        clip.mask.assign(CheckedPixelCount(width, height), 1U);
        clip.maxX = width - 1U;
        clip.maxY = height - 1U;
        clip.visible = width != 0U && height != 0U;
        struct ClipState final {
            ClipRegion region;
            double strokeAlpha{1.0};
            double fillAlpha{1.0};
            PdfBlendMode blendMode{PdfBlendMode::SourceOver};
            bool transparencyIsolated{};
            bool transparencyKnockout{};
            std::vector<double> dashPattern;
            double dashPhase{};
            std::string fillPatternName;
            std::string strokePatternName;
        };
        std::vector<ClipState> clipStack;
        std::vector<GroupLayer> groupStack;
        PdfBitmap* target = &bitmap;
        const auto paintPaths = [&](const PdfContentEvent& event, const bool fill, const bool stroke, const bool evenOdd) {
            if (fill) {
                if (!event.textState.fillPatternName.empty()) {
                    const auto pattern = document.ResolveTilingPattern(
                        pageIndex, event.resourceObjectNumber, event.textState.fillPatternName);
                    if (pattern) {
                        PaintTilingPattern(*target, *pattern, paths, mapper, clipActive ? clip : ClipRegion{},
                                           event.textState.currentTransformationMatrix, fillAlpha, options.renderPaths);
                    }
                } else {
                    FillPath(*target, paths, WithAlpha(ToColor(event.textState.fillColor), fillAlpha), evenOdd);
                }
            }
            if (stroke) {
                if (!event.textState.strokePatternName.empty()) {
                    const auto pattern = document.ResolveTilingPattern(
                        pageIndex, event.resourceObjectNumber, event.textState.strokePatternName);
                    if (pattern) {
                        PaintTilingPattern(*target, *pattern, paths, mapper, clipActive ? clip : ClipRegion{},
                                           event.textState.currentTransformationMatrix, strokeAlpha, options.renderPaths);
                    }
                } else {
                    StrokePathWithDash(*target, paths,
                                   std::max(1.0, event.textState.lineWidth * scale),
                                   WithAlpha(ToColor(event.textState.strokeColor), strokeAlpha),
                                   event.textState.lineCap, event.textState.lineJoin,
                                   event.textState.miterLimit,
                                   dashPattern, dashPhase);
                }
            }
        };
        const PdfDocument::PdfContentEventHandler pathHandler = [&](const PdfContentEvent& event) {
            if (event.type == PdfContentEventType::BeginTransparencyGroup) {
                GroupLayer layer;
                layer.bitmap = PdfBitmap(width, height, {0U, 0U, 0U, 0U});
                layer.parent = target;
                layer.blendMode = BlendModeFromName(event.transparencyGroup.blendMode);
                layer.alpha = std::clamp(event.transparencyGroup.alpha, 0.0, 1.0);
                layer.isolated = event.transparencyGroup.isolated;
                layer.knockout = event.transparencyGroup.knockout;
                layer.clip = clip;
                layer.clipActive = clipActive;
                layer.strokeAlpha = strokeAlpha;
                layer.fillAlpha = fillAlpha;
                layer.innerBlend = blendMode;
                layer.innerIsolated = transparencyIsolated;
                layer.innerKnockout = transparencyKnockout;
                layer.dashPattern = dashPattern;
                layer.dashPhase = dashPhase;
                groupStack.push_back(std::move(layer));
                target = &groupStack.back().bitmap;
                return;
            }
            if (event.type == PdfContentEventType::EndTransparencyGroup) {
                if (groupStack.empty()) return;
                GroupLayer layer = std::move(groupStack.back());
                groupStack.pop_back();
                target = layer.parent;
                clip = layer.clip;
                clipActive = layer.clipActive;
                strokeAlpha = layer.strokeAlpha;
                fillAlpha = layer.fillAlpha;
                blendMode = layer.innerBlend;
                transparencyIsolated = layer.innerIsolated;
                transparencyKnockout = layer.innerKnockout;
                dashPattern = layer.dashPattern;
                dashPhase = layer.dashPhase;
                CompositeGroupLayer(*target, layer.bitmap, layer.clip,
                                    layer.blendMode, layer.alpha, layer.knockout);
                return;
            }
            if (event.type == PdfContentEventType::SaveState) {
                clipStack.push_back({clip, strokeAlpha, fillAlpha, blendMode, transparencyIsolated, transparencyKnockout, dashPattern, dashPhase});
                return;
            }
            if (event.type == PdfContentEventType::RestoreState) {
                if (!clipStack.empty()) {
                    clip = std::move(clipStack.back().region);
                    clipActive = clip.visible;
                    strokeAlpha = clipStack.back().strokeAlpha;
                    fillAlpha = clipStack.back().fillAlpha;
                    blendMode = clipStack.back().blendMode;
                    transparencyIsolated = clipStack.back().transparencyIsolated;
                    transparencyKnockout = clipStack.back().transparencyKnockout;
                    dashPattern = clipStack.back().dashPattern;
                    dashPhase = clipStack.back().dashPhase;
                    clipStack.pop_back();
                }
                return;
            }
            if (event.type == PdfContentEventType::SetDashPattern) {
                dashPattern = event.numbers;
                dashPhase = event.textState.dashPhase;
                return;
            }
            if (event.operation == "gs" && event.type == PdfContentEventType::UnknownOperator) {
                const auto state = document.ResolveExtGState(
                    pageIndex, event.resourceObjectNumber, event.text);
                strokeAlpha = state.first[0];
                fillAlpha = state.first[1];
                blendMode = state.second;
                const auto flags = document.ResolveTransparencyFlags(pageIndex, event.resourceObjectNumber, event.text);
                transparencyIsolated = flags.first;
                transparencyKnockout = flags.second;
                return;
            }
            if (event.type == PdfContentEventType::PaintShading) {
                const auto shading = document.ResolveAxialShading(
                    pageIndex, event.resourceObjectNumber, event.text);
                if (shading) {
                    PaintAxialShading(*target, shading->axial, mapper, fillAlpha);
                } else if (const auto radial = document.ResolveRadialShading(
                               pageIndex, event.resourceObjectNumber, event.text)) {
                    PaintRadialShading(*target, radial->radial, mapper, fillAlpha);
                }
                return;
            }
            if ((event.type == PdfContentEventType::InvokeXObject ||
                 event.type == PdfContentEventType::RenderInlineImage) && options.renderImages) {
                try {
                    // Cache decoded images per object number so repeated uses
                    // (or inline images referenced by multiple forms) do not
                    // decode the stream more than once per render pass.
                    const std::uint32_t cacheKey = event.resourceObjectNumber != 0U
                        ? event.resourceObjectNumber
                        : static_cast<std::uint32_t>(imageCache.size() + 1U);
                    PdfExtractedImage* resolved = nullptr;
                    const auto cached = imageCache.find(cacheKey);
                    if (cached != imageCache.end()) {
                        resolved = &cached->second;
                    } else {
                        if (const auto image = displayList.ResolveImage(event)) {
                            resolved = &imageCache.emplace(cacheKey, *image).first->second;
                        }
                    }
                    if (resolved != nullptr) {
                        DrawImage(*target, *resolved, mapper, options.interpolateImages, resolved->info.fillAlpha);
                    }
                } catch (const PdfException&) {
                    // Continue with the next content event when one malformed
                    // image stream is present in an otherwise readable page.
                }
                return;
            }
            if (event.type != PdfContentEventType::RenderPath || !options.renderPaths) return;
            const auto mapPoint = [&](const double x, const double y) {
                const auto transformed = Transform(event.textState.currentTransformationMatrix, x, y);
                return mapper.Map(transformed[0], transformed[1]);
            };
            const auto& op = event.operation;
            if (op == "m" && event.numbers.size() >= 2U) {
                paths.emplace_back();
                current = &paths.back();
                current->push_back(mapPoint(event.numbers[0], event.numbers[1]));
            } else if (op == "l" && current != nullptr && event.numbers.size() >= 2U) {
                current->push_back(mapPoint(event.numbers[0], event.numbers[1]));
            } else if (op == "re" && event.numbers.size() >= 4U) {
                paths.emplace_back();
                current = &paths.back();
                const double x = event.numbers[0];
                const double y = event.numbers[1];
                const double w = event.numbers[2];
                const double h = event.numbers[3];
                current->push_back(mapPoint(x, y)); current->push_back(mapPoint(x + w, y));
                current->push_back(mapPoint(x + w, y + h)); current->push_back(mapPoint(x, y + h));
                current->push_back(current->front());
            } else if (op == "h" && current != nullptr) {
                CloseSubpath(*current);
            } else if ((op == "c" || op == "v" || op == "y") && current != nullptr) {
                if (current->empty()) return;
                DevicePoint p0 = current->back();
                DevicePoint p1 = p0;
                DevicePoint p2 = p0;
                DevicePoint p3 = p0;
                if (op == "c" && event.numbers.size() >= 6U) {
                    p1 = mapPoint(event.numbers[0], event.numbers[1]);
                    p2 = mapPoint(event.numbers[2], event.numbers[3]);
                    p3 = mapPoint(event.numbers[4], event.numbers[5]);
                } else if (op == "v" && event.numbers.size() >= 4U) {
                    p1 = p0;
                    p2 = mapPoint(event.numbers[0], event.numbers[1]);
                    p3 = mapPoint(event.numbers[2], event.numbers[3]);
                } else if (op == "y" && event.numbers.size() >= 4U) {
                    p1 = mapPoint(event.numbers[0], event.numbers[1]);
                    p2 = mapPoint(event.numbers[2], event.numbers[3]);
                    p3 = p2;
                } else return;
                for (int step = 1; step <= 16; ++step) {
                    const double t = static_cast<double>(step) / 16.0;
                    const double u = 1.0 - t;
                    current->push_back({
                        u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
                        u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y
                    });
                }
            } else if (op == "W" || op == "W*") {
                pendingClip = options.honorClippingPaths;
                pendingClipEvenOdd = op == "W*";
            } else {
                const bool close = op == "s" || op == "b" || op == "b*";
                if (close && current != nullptr) CloseSubpath(*current);
                const bool fill = op == "f" || op == "F" || op == "f*" || op == "B" || op == "B*" || op == "b" || op == "b*";
                const bool stroke = op == "S" || op == "s" || op == "B" || op == "B*" || op == "b" || op == "b*";
                if (pendingClip && (fill || stroke || op == "n")) {
                    IntersectClip(*target, clip, paths, pendingClipEvenOdd);
                    const bool visible = clip.visible;
                    clipActive = visible;
                    pendingClip = false;
                }
                if (fill || stroke) {
                    if (!clipActive) {
                        // The common case has no clipping path. Paint directly
                        // into the current target instead of allocating and
                        // compositing a full-page temporary bitmap per path.
                        paintPaths(event, fill, stroke, op == "f*" || op == "B*" || op == "b*");
                    } else {
                        PdfBitmap layer(width, height, {0U, 0U, 0U, 0U});
                        PdfBitmap* savedTarget = target;
                        target = &layer;
                        paintPaths(event, fill, stroke, op == "f*" || op == "B*" || op == "b*");
                        target = savedTarget;
                        CompositeLayer(*target, layer, clip, blendMode);
                    }
                }
                if (fill || stroke || op == "n") {
                    paths.clear();
                    current = nullptr;
                }
            }
        };
        // Process the page as one continuous content sequence and recurse
        // into Form XObjects so vector content is not silently omitted.
        displayList.Replay(pathHandler);
    }

    if (options.renderText) {
        try {
            const auto chunks = document.ExtractTextChunks(pageIndex);
            std::unordered_map<std::uint64_t, std::shared_ptr<const PdfFontResource>> fonts;
            for (const auto& chunk : chunks) {
                const std::uint64_t key = (static_cast<std::uint64_t>(chunk.resourceObjectNumber) << 32U) ^
                    std::hash<std::string>{}(chunk.fontResource);
                auto& font = fonts[key];
                if (!font) font = document.ResolveFont(pageIndex, chunk.resourceObjectNumber, chunk.fontResource);
                const auto* embedded = font ? font->GetEmbeddedTrueTypeFont() : nullptr;
                DrawTextChunk(bitmap, chunk,
                              mapper,
                              WithAlpha(PdfRgbaColor::Black(), chunk.fillAlpha),
                              embedded,
                              font.get());
            }
        } catch (const PdfException&) {
            // Text extraction is best-effort during rendering. A malformed
            // text stream should not discard already rendered page content.
        }
    }
    return Downsample(bitmap, options.antiAliasSamples, options.background);
}

std::vector<PdfRenderResult> PdfPageRenderer::RenderAllPagesParallel(
    const std::filesystem::path& path,
    const PdfRenderOptions& options,
    const std::size_t maxConcurrency) {
    PdfDocument document = PdfDocument::Open(path);
    const std::size_t count = document.GetPageCount();
    if (count == 0U) return {};
    std::vector<PdfRenderResult> results(count);
    if (count == 1U || maxConcurrency == 1U) {
        for (std::size_t i = 0U; i < count; ++i) {
            results[i].pageIndex = i;
            results[i].bitmap = Render(document, i, options);
        }
        return results;
    }

    std::size_t concurrency = maxConcurrency;
    if (concurrency == 0U) {
        concurrency = static_cast<std::size_t>(std::thread::hardware_concurrency());
        if (concurrency == 0U) concurrency = 2U;
    }

    std::atomic<std::size_t> next{0U};
    std::vector<std::thread> workers;
    std::mutex errorMutex;
    std::exception_ptr firstError;
    for (std::size_t t = 0U; t < concurrency; ++t) {
        workers.emplace_back([&, t] {
            (void)t;
            for (;;) {
                const std::size_t index = next.fetch_add(1U);
                if (index >= count) break;
                try {
                    // Independent document instance per render avoids sharing
                    // mutable resolver/cache state across workers.
                    PdfDocument workerDocument = PdfDocument::Open(path);
                    results[index].pageIndex = index;
                    results[index].bitmap = Render(workerDocument, index, options);
                } catch (...) {
                    std::lock_guard<std::mutex> guard(errorMutex);
                    if (!firstError) firstError = std::current_exception();
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (firstError) std::rethrow_exception(firstError);
    return results;
}

} // namespace CPPPdf

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
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <vector>

namespace CPPPdf {
namespace {

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

void DrawTextChunk(PdfBitmap& bitmap, const PdfTextChunk& chunk,
                   const CoordinateMapper& mapper, const PdfRgbaColor color,
                   const PdfTrueTypeFont* embeddedFont = nullptr) {
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
    std::size_t byteIndex = 0;
    for (std::size_t characterIndex = 0; byteIndex < chunk.utf8Text.size(); ++characterIndex) {
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
            embeddedAdvance += glyphAdvance;
            if (outlineRendered) {
                byteIndex += codePointLength(byteIndex);
                continue;
            }
        }
        const unsigned char raw = static_cast<unsigned char>(chunk.utf8Text[byteIndex]);
        const auto glyph = raw < 0x80U && raw >= 0x20U
            ? Glyph(static_cast<char>(raw)) : Glyph(' ');
        const double originX = left + (embeddedFont ? embeddedAdvance :
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


PdfRgbaColor ImagePixel(const PdfExtractedImage& image, const std::size_t x, const std::size_t y) {
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
    } else if (image.info.colorSpace == PdfImageColorSpace::Separation &&
               image.info.hasSeparationAlternate) {
        if (pixelIndex >= image.decodedBytes.size()) return {};
        const double tint = std::to_integer<std::uint8_t>(image.decodedBytes[pixelIndex]) / 255.0;
        std::vector<double> transformed;
        if (image.info.hasSeparationFunction) {
            transformed = PdfExponentialFunction(image.info.separationC0,
                image.info.separationC1, image.info.separationExponent).Evaluate(tint);
        }
        const auto channel = [tint](const double value) {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value * tint, 0.0, 1.0) * 255.0));
        };
        const auto components = image.info.separationAlternate[0];
        if (components == 1U) color = {channel(transformed.empty() ? 1.0 : transformed[0]), channel(transformed.empty() ? 1.0 : transformed[0]), channel(transformed.empty() ? 1.0 : transformed[0]), 255U};
        else if (components == 3U && transformed.size() >= 3U) color = {channel(transformed[0]), channel(transformed[1]), channel(transformed[2]), 255U};
        else if (components == 4U && transformed.size() >= 4U) color = {channel(transformed[0]), channel(transformed[1]), channel(transformed[2]), 255U};
        else if (components == 3U) color = {channel(1.0), channel(0.0), channel(0.0), 255U};
        else if (components == 4U) color = {channel(0.0), channel(0.0), channel(0.0), 255U};
        else return {};
    } else {
        return {};
    }
    if (!image.alphaBytes.empty() && pixelIndex < image.alphaBytes.size()) {
        const auto maskAlpha = std::to_integer<std::uint8_t>(image.alphaBytes[pixelIndex]);
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
                          static_cast<std::size_t>(std::lround(sourceY)));
    }
    const auto x0 = static_cast<std::size_t>(std::floor(sourceX));
    const auto y0 = static_cast<std::size_t>(std::floor(sourceY));
    const auto x1 = std::min<std::size_t>(x0 + 1U, image.info.width - 1U);
    const auto y1 = std::min<std::size_t>(y0 + 1U, image.info.height - 1U);
    const double tx = sourceX - static_cast<double>(x0);
    const double ty = sourceY - static_cast<double>(y0);
    const auto p00 = ImagePixel(image, x0, y0);
    const auto p10 = ImagePixel(image, x1, y0);
    const auto p01 = ImagePixel(image, x0, y1);
    const auto p11 = ImagePixel(image, x1, y1);
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
            bitmap.SetPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                            {channel((*values)[0]), channel((*values)[1]), channel((*values)[2]), 255U});
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
            bitmap.SetPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                            {channel((*values)[0]), channel((*values)[1]), channel((*values)[2]), 255U});
        }
    }
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


    if (options.renderPaths) {
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
        };
        std::vector<ClipState> clipStack;
        const PdfDocument::PdfContentEventHandler pathHandler = [&](const PdfContentEvent& event) {
            if (event.type == PdfContentEventType::SaveState) {
                clipStack.push_back({clip, strokeAlpha, fillAlpha, blendMode, transparencyIsolated, transparencyKnockout});
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
                    clipStack.pop_back();
                }
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
                    PaintAxialShading(bitmap, shading->axial, mapper, fillAlpha);
                } else if (const auto radial = document.ResolveRadialShading(
                               pageIndex, event.resourceObjectNumber, event.text)) {
                    PaintRadialShading(bitmap, radial->radial, mapper, fillAlpha);
                }
                return;
            }
            if (event.type != PdfContentEventType::RenderPath) return;
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
                    IntersectClip(bitmap, clip, paths, pendingClipEvenOdd);
                    const bool visible = clip.visible;
                    clipActive = visible;
                    pendingClip = false;
                }
                if (fill || stroke) {
                    if (!clipActive) {
                        // The common case has no clipping path. Paint
                        // directly into the page instead of allocating and
                        // compositing a full-page temporary bitmap per path.
                         if (fill) FillPath(bitmap, paths, WithAlpha(ToColor(event.textState.fillColor), fillAlpha), op == "f*" || op == "B*" || op == "b*");
                         if (stroke) StrokePath(bitmap, paths,
                                                std::max(1.0, event.textState.lineWidth * scale),
                                                WithAlpha(ToColor(event.textState.strokeColor), strokeAlpha),
                                                event.textState.lineCap, event.textState.lineJoin,
                                                event.textState.miterLimit);
                    } else {
                        PdfBitmap layer(width, height, {0U, 0U, 0U, 0U});
                         if (fill) FillPath(layer, paths, WithAlpha(ToColor(event.textState.fillColor), fillAlpha), op == "f*" || op == "B*" || op == "b*");
                         if (stroke) StrokePath(layer, paths,
                                                std::max(1.0, event.textState.lineWidth * scale),
                                                WithAlpha(ToColor(event.textState.strokeColor), strokeAlpha),
                                                event.textState.lineCap, event.textState.lineJoin,
                                                event.textState.miterLimit);
                        CompositeLayer(bitmap, layer, clip, blendMode);
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

    if (options.renderImages) {
        try {
            displayList.ReplayImages([&](const PdfContentEvent&, const PdfExtractedImage& image) {
                DrawImage(bitmap, image, mapper, options.interpolateImages, image.info.fillAlpha);
            });
        } catch (const PdfException&) {
            // Continue with paths/text when one malformed image stream is
            // present in an otherwise readable page.
        }
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
                              embedded);
            }
        } catch (const PdfException&) {
            // Text extraction is best-effort during rendering. A malformed
            // text stream should not discard already rendered page content.
        }
    }
    return Downsample(bitmap, options.antiAliasSamples, options.background);
}

} // namespace CPPPdf

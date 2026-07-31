#include <CPPPdf/Rendering/PdfPageRenderer.hpp>

#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Document/PdfPage.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace CPPPdf {
namespace {

struct DevicePoint final { double x{}; double y{}; };
using Subpath = std::vector<DevicePoint>;

PdfRgbaColor ToColor(const std::array<double, 3>& color) {
    const auto channel = [](const double value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };
    return {channel(color[0]), channel(color[1]), channel(color[2]), 255U};
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
              const double width, const PdfRgbaColor color) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const auto steps = static_cast<std::int32_t>(std::ceil(std::max(std::abs(dx), std::abs(dy))));
    const auto radius = std::max<std::int32_t>(0, static_cast<std::int32_t>(std::lround(width * 0.5)));
    if (steps <= 0) {
        DrawDisk(bitmap, static_cast<std::int32_t>(std::lround(a.x)),
                 static_cast<std::int32_t>(std::lround(a.y)), radius, color);
        return;
    }
    const double incrementX = dx / static_cast<double>(steps);
    const double incrementY = dy / static_cast<double>(steps);
    for (std::int32_t i = 0; i <= steps; ++i) {
        DrawDisk(bitmap, static_cast<std::int32_t>(std::lround(a.x)),
                 static_cast<std::int32_t>(std::lround(a.y)), radius, color);
        a.x += incrementX;
        a.y += incrementY;
    }
}

void StrokePath(PdfBitmap& bitmap, const std::vector<Subpath>& paths,
                const double width, const PdfRgbaColor color) {
    for (const auto& path : paths) {
        for (std::size_t i = 1; i < path.size(); ++i) {
            DrawLine(bitmap, path[i - 1U], path[i], width, color);
        }
    }
}

void FillPolygon(PdfBitmap& bitmap, const Subpath& polygon, const PdfRgbaColor color) {
    if (polygon.size() < 3U) return;
    double minimumY = polygon.front().y;
    double maximumY = polygon.front().y;
    for (const auto& point : polygon) {
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
    }
    const auto startY = std::max<std::int32_t>(0, static_cast<std::int32_t>(std::floor(minimumY)));
    const auto endY = std::min<std::int32_t>(
        static_cast<std::int32_t>(bitmap.GetHeight()) - 1,
        static_cast<std::int32_t>(std::ceil(maximumY)));
    std::vector<double> intersections;
    intersections.reserve(polygon.size());
    for (std::int32_t y = startY; y <= endY; ++y) {
        intersections.clear();
        const double scanY = static_cast<double>(y) + 0.5;
        for (std::size_t i = 0, j = polygon.size() - 1U; i < polygon.size(); j = i++) {
            const auto& first = polygon[j];
            const auto& second = polygon[i];
            if ((first.y > scanY) == (second.y > scanY)) continue;
            const double denominator = second.y - first.y;
            if (std::abs(denominator) < std::numeric_limits<double>::epsilon()) continue;
            intersections.push_back(first.x + (scanY - first.y) * (second.x - first.x) / denominator);
        }
        std::sort(intersections.begin(), intersections.end());
        for (std::size_t i = 1; i < intersections.size(); i += 2U) {
            const auto startX = static_cast<std::int32_t>(std::ceil(intersections[i - 1U]));
            const auto endX = static_cast<std::int32_t>(std::floor(intersections[i]));
            for (std::int32_t x = startX; x <= endX; ++x) bitmap.BlendPixel(x, y, color);
        }
    }
}

void FillPath(PdfBitmap& bitmap, const std::vector<Subpath>& paths, const PdfRgbaColor color) {
    for (const auto& path : paths) FillPolygon(bitmap, path, color);
}

using ClipMask = std::vector<std::uint8_t>;

void CompositeLayer(PdfBitmap& target, const PdfBitmap& layer, const ClipMask& clip) {
    for (std::size_t y = 0; y < target.GetHeight(); ++y) {
        for (std::size_t x = 0; x < target.GetWidth(); ++x) {
            const auto index = y * target.GetWidth() + x;
            if (!clip.empty() && clip[index] == 0U) continue;
            const auto pixel = layer.GetPixel(x, y);
            if (pixel.alpha != 0U) target.BlendPixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), pixel);
        }
    }
}

ClipMask CreatePathMask(const std::size_t width, const std::size_t height,
                        const std::vector<Subpath>& paths) {
    PdfBitmap maskBitmap(width, height, {0U, 0U, 0U, 255U});
    FillPath(maskBitmap, paths, {255U, 255U, 255U, 255U});
    ClipMask mask(width * height, 0U);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            mask[y * width + x] = maskBitmap.GetPixel(x, y).red > 0U ? 1U : 0U;
        }
    }
    return mask;
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
    default: return {31,17,17,17,17,17,31};
    }
}

void DrawTextChunk(PdfBitmap& bitmap, const PdfTextChunk& chunk,
                   const CoordinateMapper& mapper, const PdfRgbaColor color) {
    if (chunk.utf8Text.empty() || chunk.boundingBox.empty()) return;
    const auto topLeft = mapper.Map(chunk.boundingBox.left, chunk.boundingBox.top);
    const auto bottomRight = mapper.Map(chunk.boundingBox.right, chunk.boundingBox.bottom);
    const double left = std::min(topLeft.x, bottomRight.x);
    const double top = std::min(topLeft.y, bottomRight.y);
    const double width = std::max(1.0, std::abs(bottomRight.x - topLeft.x));
    const double height = std::max(1.0, std::abs(bottomRight.y - topLeft.y));
    const std::size_t characterCount = std::max<std::size_t>(1U, chunk.utf8Text.size());
    const double cellWidth = width / static_cast<double>(characterCount);
    const double pixelScaleX = std::max(1.0, (cellWidth * 0.82) / 5.0);
    const double pixelScaleY = std::max(1.0, (height * 0.82) / 7.0);
    for (std::size_t characterIndex = 0; characterIndex < chunk.utf8Text.size(); ++characterIndex) {
        const unsigned char raw = static_cast<unsigned char>(chunk.utf8Text[characterIndex]);
        if (raw >= 0x80U) continue;
        const auto glyph = Glyph(static_cast<char>(raw));
        const double originX = left + static_cast<double>(characterIndex) * cellWidth;
        const double originY = top + (height - 7.0 * pixelScaleY) * 0.5;
        for (std::size_t row = 0; row < glyph.size(); ++row) {
            for (std::size_t column = 0; column < 5U; ++column) {
                if ((glyph[row] & (1U << (4U - column))) == 0U) continue;
                const auto startX = static_cast<std::int32_t>(std::floor(originX + column * pixelScaleX));
                const auto startY = static_cast<std::int32_t>(std::floor(originY + row * pixelScaleY));
                const auto endX = static_cast<std::int32_t>(std::ceil(originX + (column + 1U) * pixelScaleX));
                const auto endY = static_cast<std::int32_t>(std::ceil(originY + (row + 1U) * pixelScaleY));
                for (std::int32_t y = startY; y < endY; ++y) {
                    for (std::int32_t x = startX; x < endX; ++x) bitmap.BlendPixel(x, y, color);
                }
            }
        }
    }
}


PdfRgbaColor ImagePixel(const PdfExtractedImage& image, const std::size_t x, const std::size_t y) {
    if (!image.info.decoded || image.info.width == 0U || image.info.height == 0U) return {};
    const auto pixelIndex = y * static_cast<std::size_t>(image.info.width) + x;
    PdfRgbaColor color{};
    if (image.info.colorSpace == PdfImageColorSpace::DeviceGray) {
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
    } else {
        return {};
    }
    if (!image.alphaBytes.empty() && pixelIndex < image.alphaBytes.size()) {
        color.alpha = std::to_integer<std::uint8_t>(image.alphaBytes[pixelIndex]);
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
    const auto blend = [&](const std::uint8_t a, const std::uint8_t b, const std::uint8_t c, const std::uint8_t d) {
        const double top = static_cast<double>(a) + (static_cast<double>(b) - a) * tx;
        const double bottom = static_cast<double>(c) + (static_cast<double>(d) - c) * tx;
        return static_cast<std::uint8_t>(std::lround(top + (bottom - top) * ty));
    };
    return {blend(p00.red,p10.red,p01.red,p11.red), blend(p00.green,p10.green,p01.green,p11.green),
            blend(p00.blue,p10.blue,p01.blue,p11.blue), blend(p00.alpha,p10.alpha,p01.alpha,p11.alpha)};
}

void DrawImage(PdfBitmap& bitmap, const PdfExtractedImage& image, const CoordinateMapper& mapper,
               const bool interpolate) {
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
            bitmap.BlendPixel(x, y, SampleImage(image, u, v, interpolate));
        }
    }
}

PdfBitmap Downsample(const PdfBitmap& source, const std::size_t samples, const PdfRgbaColor background) {
    if (samples <= 1U) return source;
    const std::size_t width = source.GetWidth() / samples;
    const std::size_t height = source.GetHeight() / samples;
    PdfBitmap target(width, height, background);
    const auto average = [&](const std::size_t x, const std::size_t y) {
        std::uint64_t red{}, green{}, blue{}, alpha{};
        for (std::size_t sy = 0; sy < samples; ++sy) {
            for (std::size_t sx = 0; sx < samples; ++sx) {
                const auto pixel = source.GetPixel(x * samples + sx, y * samples + sy);
                red += pixel.red; green += pixel.green; blue += pixel.blue; alpha += pixel.alpha;
            }
        }
        const auto count = static_cast<std::uint64_t>(samples * samples);
        return PdfRgbaColor{static_cast<std::uint8_t>(red / count), static_cast<std::uint8_t>(green / count),
                            static_cast<std::uint8_t>(blue / count), static_cast<std::uint8_t>(alpha / count)};
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

    if (options.renderPaths) {
        std::vector<Subpath> paths;
        Subpath* current{};
        bool pendingClip{};
        ClipMask clip(width * height, 1U);
        std::vector<ClipMask> clipStack;
        PdfContentProcessor processor;
        processor.SetHandler([&](const PdfContentEvent& event) {
            if (event.type == PdfContentEventType::SaveState) {
                clipStack.push_back(clip);
                return;
            }
            if (event.type == PdfContentEventType::RestoreState) {
                if (!clipStack.empty()) { clip = std::move(clipStack.back()); clipStack.pop_back(); }
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
            } else {
                const bool close = op == "s" || op == "b" || op == "b*";
                if (close && current != nullptr) CloseSubpath(*current);
                const bool fill = op == "f" || op == "F" || op == "f*" || op == "B" || op == "B*" || op == "b" || op == "b*";
                const bool stroke = op == "S" || op == "s" || op == "B" || op == "B*" || op == "b" || op == "b*";
                if (pendingClip && (fill || stroke || op == "n")) {
                    auto newMask = CreatePathMask(width, height, paths);
                    for (std::size_t i = 0; i < clip.size(); ++i) clip[i] = static_cast<std::uint8_t>(clip[i] != 0U && newMask[i] != 0U);
                    pendingClip = false;
                }
                if (fill || stroke) {
                    PdfBitmap layer(width, height, {0U, 0U, 0U, 0U});
                    if (fill) FillPath(layer, paths, ToColor(event.textState.fillColor));
                    if (stroke) StrokePath(layer, paths, std::max(1.0, event.textState.lineWidth * scale), ToColor(event.textState.strokeColor));
                    CompositeLayer(bitmap, layer, clip);
                }
                if (fill || stroke || op == "n") {
                    paths.clear();
                    current = nullptr;
                }
            }
        });
        for (const auto& content : page.GetContentStreams()) processor.Process(content);
    }

    if (options.renderImages) {
        PdfImageExtractionOptions extractionOptions;
        extractionOptions.keepEncodedBytes = false;
        extractionOptions.decodeSupportedFilters = true;
        for (const auto& image : document.ExtractImages(pageIndex, extractionOptions)) {
            DrawImage(bitmap, image, mapper, options.interpolateImages);
        }
    }

    if (options.renderText) {
        const auto chunks = document.ExtractTextChunks(pageIndex);
        for (const auto& chunk : chunks) DrawTextChunk(bitmap, chunk, mapper, PdfRgbaColor::Black());
    }
    return Downsample(bitmap, options.antiAliasSamples, options.background);
}

} // namespace CPPPdf

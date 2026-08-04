#pragma once
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/Rendering/PdfBitmap.hpp>
#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <CPPPdf/Fonts/PdfType1Font.hpp>
#include <CPPPdf/Fonts/PdfCff.hpp>
#include <memory>
#include <span>
#include <string>
#include <cstdint>

namespace CPPPdf {
namespace Internal { struct PdfWriterState; }

struct PdfColor final {
    double r{}, g{}, b{};
    static PdfColor Black() noexcept { return {}; }
    static PdfColor White() noexcept { return {1,1,1}; }
    static PdfColor Red() noexcept { return {1,0,0}; }
    static PdfColor Green() noexcept { return {0,1,0}; }
    static PdfColor Blue() noexcept { return {0,0,1}; }
    static PdfColor Gray(double value) noexcept { return {value,value,value}; }
    static PdfColor FromRgb(double red, double green, double blue) noexcept { return {red,green,blue}; }
};

enum class PdfLineCap { Butt = 0, Round = 1, ProjectingSquare = 2 };
enum class PdfLineJoin { Miter = 0, Round = 1, Bevel = 2 };
enum class PdfTextAlignment { Left, Center, Right };

struct PdfTextLayoutOptions final {
    PdfRectangle box{};
    double fontSize{12.0};
    double lineSpacing{1.2};
    PdfTextAlignment alignment{PdfTextAlignment::Left};
    bool wrap{true};
};

struct PdfTextLayoutResult final {
    std::size_t lineCount{};
    double width{};
    double height{};
};

class PdfCanvas final {
public:
    PdfCanvas() = default;
    PdfCanvas& SaveState();
    PdfCanvas& RestoreState();
    PdfCanvas& SetStrokeColor(PdfColor color);
    PdfCanvas& SetFillColor(PdfColor color);
    PdfCanvas& SetStrokeOpacity(double opacity);
    PdfCanvas& SetFillOpacity(double opacity);
    // Selects a tiling pattern (registered via PdfWriter::AddTilingPattern) for
    // subsequent fill and/or stroke operations.
    PdfCanvas& SetPattern(std::string patternName, bool applyToFill = true, bool applyToStroke = true);
    PdfCanvas& SetOpacity(double opacity);
    PdfCanvas& SetBlendMode(PdfBlendMode mode);
    PdfCanvas& SetLineWidth(double width);
    PdfCanvas& SetLineCap(PdfLineCap cap);
    PdfCanvas& SetLineJoin(PdfLineJoin join);
    PdfCanvas& SetMiterLimit(double limit);
    PdfCanvas& SetDashPattern(std::span<const double> pattern, double phase = 0.0);
    PdfCanvas& ClearDashPattern();
    PdfCanvas& ConcatenateMatrix(double a, double b, double c, double d, double e, double f);
    PdfCanvas& MoveTo(double x, double y);
    PdfCanvas& LineTo(double x, double y);
    PdfCanvas& CurveTo(double x1, double y1, double x2, double y2, double x3, double y3);
    PdfCanvas& ClosePath();
    PdfCanvas& Rectangle(double x, double y, double width, double height);
    PdfCanvas& DrawLine(double x1, double y1, double x2, double y2);
    PdfCanvas& FillRectangle(double x, double y, double width, double height);
    PdfCanvas& Stroke();
    PdfCanvas& Fill();
    PdfCanvas& FillEvenOdd();
    PdfCanvas& FillStroke();
    PdfCanvas& FillStrokeEvenOdd();
    PdfCanvas& Clip();
    PdfCanvas& ClipEvenOdd();
    PdfCanvas& EndPath();

    // Path styling.
    PdfCanvas& SetLineDash(std::span<const double> pattern, double phase = 0.0);

    // Polygon and curve construction. Each call starts a new subpath.
    PdfCanvas& DrawPolyline(std::span<const PdfPoint> points);
    PdfCanvas& DrawPolygon(std::span<const PdfPoint> points);
    PdfCanvas& FillPolygon(std::span<const PdfPoint> points);
    PdfCanvas& DrawBezier(double x0, double y0, double cx1, double cy1,
                          double cx2, double cy2, double x1, double y1);
    PdfCanvas& FillBezier(double x0, double y0, double cx1, double cy1,
                          double cx2, double cy2, double x1, double y1);

    // Ellipse/circle constructed from two cubic bezier segments (k=0.5523).
    PdfCanvas& DrawEllipse(double centerX, double centerY, double radiusX, double radiusY);
    PdfCanvas& FillEllipse(double centerX, double centerY, double radiusX, double radiusY);
    PdfCanvas& DrawCircle(double centerX, double centerY, double radius);
    PdfCanvas& FillCircle(double centerX, double centerY, double radius);
    PdfCanvas& BeginText();
    PdfCanvas& SetFontAndSize(std::string base14Font, double size);
    PdfCanvas& SetTrueTypeFontAndSize(const PdfTrueTypeFont& font, double size);
    [[nodiscard]] double GetCurrentFontSize() const noexcept;
    // Measures UTF-8 text with the active TrueType font and size. Returns 0
    // when no TrueType font is active.
    [[nodiscard]] double MeasureTextUtf8(std::string_view utf8Text) const;
    // Selects an embedded Type1 font for subsequent text. The font program is
    // stored in the document and written as a /FontFile stream.
    PdfCanvas& SetType1FontAndSize(const PdfType1Font& font, double size);
    // Shows Latin-1 text using the active Type1 font.
    PdfCanvas& ShowType1Text(std::string latin1Text);
    // Selects an embedded CFF (Type1C) font for subsequent text; the CFF
    // program is written as a /FontFile3 /Subtype /Type1C stream.
    PdfCanvas& SetEmbeddedCffFontAndSize(const PdfCffFont& font, double size);
    PdfCanvas& SetTextMatrix(double a, double b, double c, double d, double e, double f);
    PdfCanvas& MoveText(double x, double y);
    // Sets the text rendering mode (0 fill, 1 stroke, 2 fill+stroke,
    // 3 invisible, 4..7 with clipping).
    PdfCanvas& SetTextRenderMode(std::uint8_t mode);
    PdfCanvas& SetTextLeading(double leading);
    PdfCanvas& SetTextRise(double rise);
    PdfCanvas& SetHorizontalScaling(double scale);
    PdfCanvas& SetCharSpacing(double spacing);
    PdfCanvas& SetWordSpacing(double spacing);
    PdfCanvas& ShowText(std::string text);
    PdfCanvas& ShowTextUtf8(std::string utf8Text);
    // Shows UTF-8 text using the active font and, when a code point is missing,
    // falls back to the provided alternative fonts in order. Text is split into
    // runs so each run uses a font that covers it. Applied kerning follows the
    // current font's `kern` table.
    PdfCanvas& ShowTextUtf8WithFallback(std::string utf8Text,
                                        std::span<const PdfTrueTypeFont> fallbackFonts);
    PdfCanvas& DrawTextUtf8(const PdfTrueTypeFont& font, std::string utf8Text, const PdfTextLayoutOptions& options);
    // Vertical writing: rotates the text matrix 90° counter-clockwise so
    // subsequent text runs top-to-bottom. `SetVerticalWriting(false)` restores
    // horizontal writing.
    PdfCanvas& SetVerticalWriting(bool vertical);
    [[nodiscard]] bool IsVerticalWriting() const noexcept;
    // Shows text in vertical (top-to-bottom) direction using the active font.
    PdfCanvas& ShowTextVertical(std::string utf8Text);
    [[nodiscard]] static PdfTextLayoutResult MeasureTextLayout(
        const PdfTrueTypeFont& font, std::string_view utf8Text,
        const PdfTextLayoutOptions& options);
    PdfCanvas& EndText();
    PdfCanvas& DrawImage(const PdfImage& image, const PdfRectangle& rectangle);
    // Optional content: wraps subsequent drawing in /OC <name> BDC ... EMC so
    // the content belongs to the named layer. The layer must be registered on
    // the writer with AddOptionalContentGroup first.
    PdfCanvas& BeginLayer(std::string layerName);
    PdfCanvas& EndLayer();
private:
    friend class PdfWriter;
    PdfCanvas(std::shared_ptr<Internal::PdfWriterState> state, std::size_t pageIndex);
    void Append(const std::string& command);
    std::string RegisterOpacity(double strokeOpacity, double fillOpacity);
    std::shared_ptr<Internal::PdfWriterState> state_;
    std::size_t pageIndex_{};
};

} // namespace CPPPdf

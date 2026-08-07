#include <CPPPdf/Graphics/PdfCanvas.hpp>
#include "Writer/PdfWriterState.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <stdexcept>

namespace CPPPdf {
namespace {
std::string number(double v) { std::ostringstream s; s << std::fixed << std::setprecision(3) << v; auto x=s.str(); while(x.size()>1&&x.back()=='0')x.pop_back(); if(x.back()=='.')x.pop_back(); return x; }
std::string escape(std::string_view text) { std::string out; for(char c:text){ if(c=='('||c==')'||c=='\\')out.push_back('\\'); out.push_back(c);} return out; }
double opacityValue(double value) { if (!std::isfinite(value)) throw std::invalid_argument("Opacity must be finite."); return std::clamp(value, 0.0, 1.0); }
std::string pdfName(std::string name) {
    if (!name.empty() && name.front() == '/') name.erase(name.begin());
    if (name.empty()) throw std::invalid_argument("PDF name cannot be empty.");
    constexpr std::string_view delimiters{"()<>[]{}/%#"};
    for (const unsigned char ch : name) {
        if (ch <= 0x20U || ch >= 0x7FU || delimiters.find(static_cast<char>(ch)) != std::string_view::npos) {
            throw std::invalid_argument("PDF name contains an unsupported character.");
        }
    }
    return name;
}
std::vector<std::uint32_t> decodeUtf8(std::string_view text) {
    std::vector<std::uint32_t> result;
    for (std::size_t i=0;i<text.size();) {
        const auto c=static_cast<unsigned char>(text[i]);
        std::uint32_t cp=0; std::size_t n=0;
        if (c<0x80) { cp=c; n=1; }
        else if ((c&0xE0)==0xC0) { cp=c&0x1F; n=2; }
        else if ((c&0xF0)==0xE0) { cp=c&0x0F; n=3; }
        else if ((c&0xF8)==0xF0) { cp=c&0x07; n=4; }
        else throw std::invalid_argument("Invalid UTF-8 sequence.");
        if (i+n>text.size()) throw std::invalid_argument("Truncated UTF-8 sequence.");
        for (std::size_t j=1;j<n;++j) { const auto cc=static_cast<unsigned char>(text[i+j]); if ((cc&0xC0)!=0x80) throw std::invalid_argument("Invalid UTF-8 continuation byte."); cp=(cp<<6)|(cc&0x3F); }
        if ((n==2&&cp<0x80)||(n==3&&cp<0x800)||(n==4&&cp<0x10000)||cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF)) throw std::invalid_argument("Invalid UTF-8 code point.");
        result.push_back(cp); i+=n;
    }
    return result;
}

std::vector<std::string> wrapUtf8(const PdfTrueTypeFont& font, std::string_view text, double size, double maxWidth, bool wrap) {
    if (maxWidth <= 0 || !std::isfinite(maxWidth)) throw std::invalid_argument("Text layout box must have positive width.");
    std::vector<std::string> lines;
    std::size_t paragraphStart = 0;
    while (paragraphStart <= text.size()) {
        const auto newline = text.find('\n', paragraphStart);
        const auto paragraph = text.substr(paragraphStart, newline == std::string_view::npos ? text.size() - paragraphStart : newline - paragraphStart);
        if (!wrap || paragraph.empty()) {
            if (!paragraph.empty() && font.MeasureTextUtf8(paragraph, size) > maxWidth) throw std::invalid_argument("Text exceeds the layout width while wrapping is disabled.");
            lines.emplace_back(paragraph);
        } else {
            std::string current;
            std::size_t pos = 0;
            while (pos < paragraph.size()) {
                while (pos < paragraph.size() && paragraph[pos] == ' ') ++pos;
                if (pos >= paragraph.size()) break;
                const auto end = paragraph.find(' ', pos);
                const auto word = paragraph.substr(pos, end == std::string_view::npos ? paragraph.size() - pos : end - pos);
                std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
                if (!current.empty() && font.MeasureTextUtf8(candidate, size) > maxWidth) { lines.push_back(current); current.assign(word); }
                else current = std::move(candidate);
                if (font.MeasureTextUtf8(current, size) > maxWidth) throw std::invalid_argument("A word exceeds the text layout width.");
                if (end == std::string_view::npos) break;
                pos = end + 1;
            }
            lines.push_back(std::move(current));
        }
        if (newline == std::string_view::npos) break;
        paragraphStart = newline + 1;
    }
    return lines;
}
std::string hex4(std::uint16_t value) { static constexpr char d[]="0123456789ABCDEF"; std::string out(4,'0'); for(int i=3;i>=0;--i){out[i]=d[value&15];value>>=4;} return out; }


bool type3FontsEqual(const PdfType3Font& left, const PdfType3Font& right) {
    if (left.GetFontName() != right.GetFontName() ||
        left.GetFontBoundingBox().left != right.GetFontBoundingBox().left ||
        left.GetFontBoundingBox().bottom != right.GetFontBoundingBox().bottom ||
        left.GetFontBoundingBox().right != right.GetFontBoundingBox().right ||
        left.GetFontBoundingBox().top != right.GetFontBoundingBox().top ||
        left.GetFontMatrix() != right.GetFontMatrix() ||
        left.GetGlyphs().size() != right.GetGlyphs().size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.GetGlyphs().size(); ++index) {
        const auto& a = left.GetGlyphs()[index];
        const auto& b = right.GetGlyphs()[index];
        if (a.code != b.code || a.name != b.name || a.advanceWidth != b.advanceWidth ||
            a.boundingBox.left != b.boundingBox.left ||
            a.boundingBox.bottom != b.boundingBox.bottom ||
            a.boundingBox.right != b.boundingBox.right ||
            a.boundingBox.top != b.boundingBox.top ||
            a.content != b.content || a.unicodeCodePoint != b.unicodeCodePoint) {
            return false;
        }
    }
    return true;
}

std::size_t imageHash(const PdfImage& image) {
    std::size_t hash = sizeof(std::size_t) == 8U
        ? static_cast<std::size_t>(1469598103934665603ULL)
        : static_cast<std::size_t>(2166136261U);
    const std::size_t prime = sizeof(std::size_t) == 8U
        ? static_cast<std::size_t>(1099511628211ULL)
        : static_cast<std::size_t>(16777619U);
    auto mix = [&](const std::uint64_t value) {
        for (std::size_t shift = 0U; shift < sizeof(value); ++shift) {
            hash ^= static_cast<std::size_t>((value >> (shift * 8U)) & 0xFFU);
            hash *= prime;
        }
    };
    mix(image.GetWidth());
    mix(image.GetHeight());
    mix(static_cast<std::uint64_t>(image.GetColorSpace()));
    mix(static_cast<std::uint64_t>(image.GetEncoding()));
    mix(image.GetBitsPerComponent());
    for (const auto value : image.GetBytes()) {
        hash ^= static_cast<std::size_t>(std::to_integer<std::uint8_t>(value));
        hash *= prime;
    }
    mix(image.HasSoftMask() ? 1U : 0U);
    for (const auto value : image.GetSoftMaskBytes()) {
        hash ^= static_cast<std::size_t>(std::to_integer<std::uint8_t>(value));
        hash *= prime;
    }
    for (const double value : image.GetMatte()) {
        mix(static_cast<std::uint64_t>(std::llround(value * 1000000000.0)));
    }
    return hash;
}

bool imagesEqual(const PdfImage& left, const PdfImage& right) {
    return left.GetWidth() == right.GetWidth() && left.GetHeight() == right.GetHeight() &&
           left.GetColorSpace() == right.GetColorSpace() && left.GetEncoding() == right.GetEncoding() &&
           left.GetBitsPerComponent() == right.GetBitsPerComponent() &&
           left.GetBytes().size() == right.GetBytes().size() &&
           left.GetSoftMaskBytes().size() == right.GetSoftMaskBytes().size() &&
           left.GetMatte().size() == right.GetMatte().size() &&
           std::equal(left.GetBytes().begin(), left.GetBytes().end(), right.GetBytes().begin()) &&
           std::equal(left.GetSoftMaskBytes().begin(), left.GetSoftMaskBytes().end(),
                      right.GetSoftMaskBytes().begin()) &&
           std::equal(left.GetMatte().begin(), left.GetMatte().end(), right.GetMatte().begin());
}

std::string inlineColorSpace(const PdfImageColorSpace colorSpace) {
    switch (colorSpace) {
    case PdfImageColorSpace::DeviceGray: return "/DeviceGray";
    case PdfImageColorSpace::DeviceRGB: return "/DeviceRGB";
    case PdfImageColorSpace::DeviceCMYK: return "/DeviceCMYK";
    default: throw std::invalid_argument("Inline images support DeviceGray, DeviceRGB, and DeviceCMYK only.");
    }
}


std::pair<std::string_view, std::size_t> patternBaseColorSpace(
    const PdfPatternBaseColorSpace colorSpace) {
    switch (colorSpace) {
    case PdfPatternBaseColorSpace::Gray: return {"/DeviceGray", 1U};
    case PdfPatternBaseColorSpace::Rgb: return {"/DeviceRGB", 3U};
    case PdfPatternBaseColorSpace::Cmyk: return {"/DeviceCMYK", 4U};
    }
    return {"/DeviceRGB", 3U};
}

std::string asciiHex(const std::span<const std::byte> bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(bytes.size() * 2U + bytes.size() / 32U + 2U);
    std::size_t column = 0U;
    for (const auto value : bytes) {
        const auto byteValue = std::to_integer<std::uint8_t>(value);
        output.push_back(digits[byteValue >> 4U]);
        output.push_back(digits[byteValue & 0x0FU]);
        column += 2U;
        if (column >= 64U) {
            output.push_back('\n');
            column = 0U;
        }
    }
    output += ">\n";
    return output;
}
}
PdfCanvas::PdfCanvas(std::shared_ptr<Internal::PdfWriterState> state, std::size_t pageIndex):state_(std::move(state)),pageIndex_(pageIndex){}
void PdfCanvas::Append(const std::string& c){ if(!state_||pageIndex_>=state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page"); state_->pages[pageIndex_].content += c; }
std::string PdfCanvas::RegisterOpacity(double strokeOpacity, double fillOpacity,
                                       const PdfBlendMode blendMode) {
    strokeOpacity = opacityValue(strokeOpacity);
    fillOpacity = opacityValue(fillOpacity);
    for (std::size_t i = 0; i < state_->extGStates.size(); ++i) {
        const auto& gs = state_->extGStates[i];
        if (std::abs(gs.strokeOpacity - strokeOpacity) < 1.0e-9 &&
            std::abs(gs.fillOpacity - fillOpacity) < 1.0e-9 &&
            gs.blendMode == blendMode) {
            auto& refs = state_->pages[pageIndex_].extGStateIndices;
            if (std::find(refs.begin(), refs.end(), i) == refs.end()) refs.push_back(i);
            return gs.resourceName;
        }
    }
    const std::size_t index = state_->extGStates.size();
    const std::string name = "GS" + std::to_string(index + 1U);
    state_->extGStates.push_back({strokeOpacity, fillOpacity, blendMode, name});
    state_->pages[pageIndex_].extGStateIndices.push_back(index);
    return name;
}
PdfCanvas& PdfCanvas::SaveState() {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& page = state_->pages[pageIndex_];
    page.graphicsStateStack.push_back(page.graphicsState);
    Append("q\n");
    return *this;
}
PdfCanvas& PdfCanvas::RestoreState() {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& page = state_->pages[pageIndex_];
    if (page.graphicsStateStack.empty()) throw std::logic_error("RestoreState has no matching SaveState.");
    page.graphicsState = page.graphicsStateStack.back();
    page.graphicsStateStack.pop_back();
    Append("Q\n");
    return *this;
}
PdfCanvas& PdfCanvas::SetStrokeColor(PdfColor c){Append(number(c.r)+" "+number(c.g)+" "+number(c.b)+" RG\n");return *this;}
PdfCanvas& PdfCanvas::SetFillColor(PdfColor c){Append(number(c.r)+" "+number(c.g)+" "+number(c.b)+" rg\n");return *this;}
PdfCanvas& PdfCanvas::SetStrokeColorSpace(std::string colorSpaceName,
                                         const std::span<const double> components) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    colorSpaceName = pdfName(std::move(colorSpaceName));
    const auto iterator = std::find_if(state_->colorSpaces.begin(), state_->colorSpaces.end(),
        [&](const auto& item) { return item.resourceName == colorSpaceName; });
    if (iterator == state_->colorSpaces.end()) throw std::invalid_argument("Unknown writer color space: " + colorSpaceName);
    if (components.size() != iterator->components) throw std::invalid_argument("Stroke color component count does not match the color space.");
    std::string command = "/" + colorSpaceName + " CS ";
    for (const double component : components) {
        if (!std::isfinite(component) || component < 0.0 || component > 1.0) {
            throw std::invalid_argument("Stroke color components must be finite values in [0, 1].");
        }
        command += number(component) + " ";
    }
    command += "SCN\n";
    auto& page = state_->pages[pageIndex_];
    const auto index = static_cast<std::size_t>(iterator - state_->colorSpaces.begin());
    if (std::find(page.colorSpaceIndices.begin(), page.colorSpaceIndices.end(), index) == page.colorSpaceIndices.end()) {
        page.colorSpaceIndices.push_back(index);
    }
    Append(command);
    return *this;
}
PdfCanvas& PdfCanvas::SetFillColorSpace(std::string colorSpaceName,
                                       const std::span<const double> components) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    colorSpaceName = pdfName(std::move(colorSpaceName));
    const auto iterator = std::find_if(state_->colorSpaces.begin(), state_->colorSpaces.end(),
        [&](const auto& item) { return item.resourceName == colorSpaceName; });
    if (iterator == state_->colorSpaces.end()) throw std::invalid_argument("Unknown writer color space: " + colorSpaceName);
    if (components.size() != iterator->components) throw std::invalid_argument("Fill color component count does not match the color space.");
    std::string command = "/" + colorSpaceName + " cs ";
    for (const double component : components) {
        if (!std::isfinite(component) || component < 0.0 || component > 1.0) {
            throw std::invalid_argument("Fill color components must be finite values in [0, 1].");
        }
        command += number(component) + " ";
    }
    command += "scn\n";
    auto& page = state_->pages[pageIndex_];
    const auto index = static_cast<std::size_t>(iterator - state_->colorSpaces.begin());
    if (std::find(page.colorSpaceIndices.begin(), page.colorSpaceIndices.end(), index) == page.colorSpaceIndices.end()) {
        page.colorSpaceIndices.push_back(index);
    }
    Append(command);
    return *this;
}
PdfCanvas& PdfCanvas::SetPattern(std::string patternName, const bool applyToFill,
                                 const bool applyToStroke) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    patternName = pdfName(std::move(patternName));
    auto& page = state_->pages[pageIndex_];
    const auto iterator = std::find_if(state_->tilingPatterns.begin(), state_->tilingPatterns.end(),
        [&](const auto& pattern) { return pattern.options.name == patternName; });
    if (iterator == state_->tilingPatterns.end()) {
        throw std::invalid_argument("Unknown tiling pattern: " + patternName);
    }
    if (!iterator->options.paintTypeColor) {
        throw std::invalid_argument(
            "SetPattern requires a colored PaintType 1 pattern; use SetUncoloredPattern for PaintType 2.");
    }
    const auto index = static_cast<std::size_t>(iterator - state_->tilingPatterns.begin());
    if (std::find(page.patternIndices.begin(), page.patternIndices.end(), index) == page.patternIndices.end()) {
        page.patternIndices.push_back(index);
    }
    if (applyToFill) Append("/Pattern cs /" + patternName + " scn\n");
    if (applyToStroke) Append("/Pattern CS /" + patternName + " SCN\n");
    return *this;
}

PdfCanvas& PdfCanvas::SetUncoloredPattern(std::string patternName,
                                          const PdfPatternBaseColorSpace baseColorSpace,
                                          const std::span<const double> components,
                                          const bool applyToFill,
                                          const bool applyToStroke) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    patternName = pdfName(std::move(patternName));
    auto& page = state_->pages[pageIndex_];
    const auto iterator = std::find_if(state_->tilingPatterns.begin(), state_->tilingPatterns.end(),
        [&](const auto& pattern) { return pattern.options.name == patternName; });
    if (iterator == state_->tilingPatterns.end()) {
        throw std::invalid_argument("Unknown tiling pattern: " + patternName);
    }
    if (iterator->options.paintTypeColor) {
        throw std::invalid_argument(
            "SetUncoloredPattern requires an uncolored PaintType 2 pattern.");
    }
    const auto [baseName, componentCount] = patternBaseColorSpace(baseColorSpace);
    if (components.size() != componentCount) {
        throw std::invalid_argument("Uncolored pattern component count does not match its base color space.");
    }
    std::string tint;
    for (const double component : components) {
        if (!std::isfinite(component) || component < 0.0 || component > 1.0) {
            throw std::invalid_argument(
                "Uncolored pattern components must be finite values in [0, 1].");
        }
        tint += number(component) + " ";
    }
    const auto index = static_cast<std::size_t>(iterator - state_->tilingPatterns.begin());
    if (std::find(page.patternIndices.begin(), page.patternIndices.end(), index) == page.patternIndices.end()) {
        page.patternIndices.push_back(index);
    }
    if (applyToFill) {
        Append("[/Pattern " + std::string(baseName) + "] cs " + tint + "/" + patternName + " scn\n");
    }
    if (applyToStroke) {
        Append("[/Pattern " + std::string(baseName) + "] CS " + tint + "/" + patternName + " SCN\n");
    }
    return *this;
}
PdfCanvas& PdfCanvas::SetStrokeOpacity(const double opacity) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& graphics = state_->pages[pageIndex_].graphicsState;
    graphics.strokeOpacity = opacityValue(opacity);
    Append("/" + RegisterOpacity(graphics.strokeOpacity, graphics.fillOpacity, graphics.blendMode) + " gs\n");
    return *this;
}
PdfCanvas& PdfCanvas::SetFillOpacity(const double opacity) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& graphics = state_->pages[pageIndex_].graphicsState;
    graphics.fillOpacity = opacityValue(opacity);
    Append("/" + RegisterOpacity(graphics.strokeOpacity, graphics.fillOpacity, graphics.blendMode) + " gs\n");
    return *this;
}
PdfCanvas& PdfCanvas::SetOpacity(const double opacity) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    return SetTransparency(opacity, opacity, state_->pages[pageIndex_].graphicsState.blendMode);
}
PdfCanvas& PdfCanvas::SetBlendMode(const PdfBlendMode mode) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& graphics = state_->pages[pageIndex_].graphicsState;
    graphics.blendMode = mode;
    Append("/" + RegisterOpacity(graphics.strokeOpacity, graphics.fillOpacity, graphics.blendMode) + " gs\n");
    return *this;
}
PdfCanvas& PdfCanvas::SetTransparency(const double strokeOpacity, const double fillOpacity,
                                      const PdfBlendMode mode) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& graphics = state_->pages[pageIndex_].graphicsState;
    graphics.strokeOpacity = opacityValue(strokeOpacity);
    graphics.fillOpacity = opacityValue(fillOpacity);
    graphics.blendMode = mode;
    Append("/" + RegisterOpacity(graphics.strokeOpacity, graphics.fillOpacity, graphics.blendMode) + " gs\n");
    return *this;
}
PdfCanvas& PdfCanvas::SetLineWidth(double w){if(w<0||!std::isfinite(w))throw std::invalid_argument("Line width must be finite and non-negative.");Append(number(w)+" w\n");return *this;}
PdfCanvas& PdfCanvas::SetLineCap(PdfLineCap cap){Append(std::to_string(static_cast<int>(cap))+" J\n");return *this;}
PdfCanvas& PdfCanvas::SetLineJoin(PdfLineJoin join){Append(std::to_string(static_cast<int>(join))+" j\n");return *this;}
PdfCanvas& PdfCanvas::SetMiterLimit(double limit){if(limit<1||!std::isfinite(limit))throw std::invalid_argument("Miter limit must be at least 1.");Append(number(limit)+" M\n");return *this;}
PdfCanvas& PdfCanvas::SetDashPattern(std::span<const double> pattern,double phase){std::string cmd="[";for(double value:pattern){if(value<0||!std::isfinite(value))throw std::invalid_argument("Dash values must be finite and non-negative.");cmd+=number(value)+" ";}cmd+="] "+number(phase)+" d\n";Append(cmd);return *this;}
PdfCanvas& PdfCanvas::ClearDashPattern(){Append("[] 0 d\n");return *this;}
PdfCanvas& PdfCanvas::ConcatenateMatrix(double a,double b,double c,double d,double e,double f){Append(number(a)+" "+number(b)+" "+number(c)+" "+number(d)+" "+number(e)+" "+number(f)+" cm\n");return *this;}
PdfCanvas& PdfCanvas::MoveTo(double x,double y){Append(number(x)+" "+number(y)+" m\n");return *this;}
PdfCanvas& PdfCanvas::LineTo(double x,double y){Append(number(x)+" "+number(y)+" l\n");return *this;}
PdfCanvas& PdfCanvas::CurveTo(double x1,double y1,double x2,double y2,double x3,double y3){Append(number(x1)+" "+number(y1)+" "+number(x2)+" "+number(y2)+" "+number(x3)+" "+number(y3)+" c\n");return *this;}
PdfCanvas& PdfCanvas::ClosePath(){Append("h\n");return *this;}
PdfCanvas& PdfCanvas::Rectangle(double x,double y,double w,double h){Append(number(x)+" "+number(y)+" "+number(w)+" "+number(h)+" re\n");return *this;}
PdfCanvas& PdfCanvas::DrawLine(double x1,double y1,double x2,double y2){return MoveTo(x1,y1).LineTo(x2,y2).Stroke();}
PdfCanvas& PdfCanvas::FillRectangle(double x,double y,double w,double h){return Rectangle(x,y,w,h).Fill();}
PdfCanvas& PdfCanvas::Stroke(){Append("S\n");return *this;} PdfCanvas& PdfCanvas::Fill(){Append("f\n");return *this;}
PdfCanvas& PdfCanvas::FillEvenOdd(){Append("f*\n");return *this;} PdfCanvas& PdfCanvas::FillStroke(){Append("B\n");return *this;} PdfCanvas& PdfCanvas::FillStrokeEvenOdd(){Append("B*\n");return *this;}
PdfCanvas& PdfCanvas::Clip(){Append("W\n");return *this;} PdfCanvas& PdfCanvas::ClipEvenOdd(){Append("W*\n");return *this;} PdfCanvas& PdfCanvas::EndPath(){Append("n\n");return *this;}
PdfCanvas& PdfCanvas::SetLineDash(std::span<const double> pattern,double phase){
    if(pattern.empty()){Append("[] 0 d\n");return *this;}
    std::string out="[";
    for(std::size_t i=0;i<pattern.size();++i){out+=number(pattern[i]);if(i+1U<pattern.size())out+=" ";}
    out+="] "+number(phase)+" d\n";
    Append(out);return *this;
}
PdfCanvas& PdfCanvas::DrawPolyline(std::span<const PdfPoint> points){
    if(points.size()<2U)throw std::invalid_argument("Polyline requires at least two points.");
    MoveTo(points[0].x,points[0].y);
    for(std::size_t i=1U;i<points.size();++i)LineTo(points[i].x,points[i].y);
    return Stroke();
}
PdfCanvas& PdfCanvas::DrawPolygon(std::span<const PdfPoint> points){
    if(points.size()<3U)throw std::invalid_argument("Polygon requires at least three points.");
    MoveTo(points[0].x,points[0].y);
    for(std::size_t i=1U;i<points.size();++i)LineTo(points[i].x,points[i].y);
    return ClosePath().Stroke();
}
PdfCanvas& PdfCanvas::FillPolygon(std::span<const PdfPoint> points){
    if(points.size()<3U)throw std::invalid_argument("Polygon requires at least three points.");
    MoveTo(points[0].x,points[0].y);
    for(std::size_t i=1U;i<points.size();++i)LineTo(points[i].x,points[i].y);
    return ClosePath().Fill();
}
PdfCanvas& PdfCanvas::DrawBezier(double x0,double y0,double cx1,double cy1,double cx2,double cy2,double x1,double y1){
    return MoveTo(x0,y0).CurveTo(cx1,cy1,cx2,cy2,x1,y1).Stroke();
}
PdfCanvas& PdfCanvas::FillBezier(double x0,double y0,double cx1,double cy1,double cx2,double cy2,double x1,double y1){
    return MoveTo(x0,y0).CurveTo(cx1,cy1,cx2,cy2,x1,y1).Fill();
}
namespace {
// Constructs a full ellipse path using two cubic beziers (k = 0.5523).
void AppendEllipsePath(PdfCanvas& canvas, const double cx, const double cy,
                       const double rx, const double ry) {
    if (rx <= 0.0 || ry <= 0.0 || !std::isfinite(rx) || !std::isfinite(ry)) {
        throw std::invalid_argument("Ellipse radii must be positive and finite.");
    }
    const double k = 0.5522847498307936;
    canvas.MoveTo(cx - rx, cy);
    canvas.CurveTo(cx - rx, cy + k * ry, cx - k * rx, cy + ry, cx, cy + ry);
    canvas.CurveTo(cx + k * rx, cy + ry, cx + rx, cy + k * ry, cx + rx, cy);
    canvas.CurveTo(cx + rx, cy - k * ry, cx + k * rx, cy - ry, cx, cy - ry);
    canvas.CurveTo(cx - k * rx, cy - ry, cx - rx, cy - k * ry, cx - rx, cy);
    canvas.ClosePath();
}
} // namespace
PdfCanvas& PdfCanvas::DrawEllipse(double centerX,double centerY,double radiusX,double radiusY){
    AppendEllipsePath(*this,centerX,centerY,radiusX,radiusY);return Stroke();
}
PdfCanvas& PdfCanvas::FillEllipse(double centerX,double centerY,double radiusX,double radiusY){
    AppendEllipsePath(*this,centerX,centerY,radiusX,radiusY);return Fill();
}
PdfCanvas& PdfCanvas::DrawCircle(double centerX,double centerY,double radius){
    return DrawEllipse(centerX,centerY,radius,radius);
}
PdfCanvas& PdfCanvas::FillCircle(double centerX,double centerY,double radius){
    return FillEllipse(centerX,centerY,radius,radius);
}
PdfCanvas& PdfCanvas::BeginText(){Append("BT\n");return *this;}
PdfCanvas& PdfCanvas::SetFontAndSize(std::string font,double size){ if(!font.empty()&&font.front()=='/')font.erase(font.begin()); auto& page=state_->pages[pageIndex_]; page.fontName=std::move(font); page.currentFontSize=size; page.activeEmbeddedFontIndex.reset(); Append("/F1 "+number(size)+" Tf\n");return *this;}
PdfCanvas& PdfCanvas::SetTrueTypeFontAndSize(const PdfTrueTypeFont& font,double size){
    if(size<=0||!std::isfinite(size)) throw std::invalid_argument("Font size must be positive and finite.");
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    std::size_t index=state_->embeddedFonts.size();
    for(std::size_t i=0;i<state_->embeddedFonts.size();++i){
        if(!state_->embeddedFonts[i].vertical && state_->embeddedFonts[i].font.GetBytes()==font.GetBytes()){ index=i; break; }
    }
    if(index==state_->embeddedFonts.size()) state_->embeddedFonts.push_back({font,"FT"+std::to_string(index+1U),{},false});
    auto& page=state_->pages[pageIndex_];
    page.activeEmbeddedFontIndex=index;
    page.activeType1FontIndex.reset();
    page.activeCffFontIndex.reset();
    page.activeType3FontIndex.reset();
    page.currentFontSize=size;
    page.fontName=font.GetPostScriptName();
    if(std::find(page.embeddedFontIndices.begin(),page.embeddedFontIndices.end(),index)==page.embeddedFontIndices.end()) page.embeddedFontIndices.push_back(index);
    Append("/"+state_->embeddedFonts[index].resourceName+" "+number(size)+" Tf\n"); return *this;
}

PdfCanvas& PdfCanvas::SetTrueTypeFontAndSizeVertical(const PdfTrueTypeFont& font, const double size) {
    if (size <= 0.0 || !std::isfinite(size)) throw std::invalid_argument("Font size must be positive and finite.");
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    std::size_t index = state_->embeddedFonts.size();
    for (std::size_t i = 0; i < state_->embeddedFonts.size(); ++i) {
        if (state_->embeddedFonts[i].vertical && state_->embeddedFonts[i].font.GetBytes() == font.GetBytes()) {
            index = i;
            break;
        }
    }
    if (index == state_->embeddedFonts.size()) {
        state_->embeddedFonts.push_back({font, "FTV" + std::to_string(index + 1U), {}, true});
    }
    auto& page = state_->pages[pageIndex_];
    page.activeEmbeddedFontIndex = index;
    page.activeType1FontIndex.reset();
    page.activeCffFontIndex.reset();
    page.activeType3FontIndex.reset();
    page.currentFontSize = size;
    page.fontName = font.GetPostScriptName();
    if (std::find(page.embeddedFontIndices.begin(), page.embeddedFontIndices.end(), index) ==
        page.embeddedFontIndices.end()) {
        page.embeddedFontIndices.push_back(index);
    }
    Append("/" + state_->embeddedFonts[index].resourceName + " " + number(size) + " Tf\n");
    return *this;
}

double PdfCanvas::GetCurrentFontSize() const noexcept {
    if (!state_ || pageIndex_ >= state_->pages.size()) return 0.0;
    return state_->pages[pageIndex_].currentFontSize;
}

std::string PdfCanvas::GetActiveFontName() const noexcept {
    if (!state_ || pageIndex_ >= state_->pages.size()) return {};
    return state_->pages[pageIndex_].fontName;
}

double PdfCanvas::MeasureTextUtf8(const std::string_view utf8Text) const {
    if (!state_ || pageIndex_ >= state_->pages.size()) return 0.0;
    const auto& page = state_->pages[pageIndex_];
    if (!page.activeEmbeddedFontIndex || *page.activeEmbeddedFontIndex >= state_->embeddedFonts.size()) {
        return 0.0;
    }
    const auto& font = state_->embeddedFonts[*page.activeEmbeddedFontIndex].font;
    if (page.currentFontSize <= 0.0) return 0.0;
    return font.MeasureTextUtf8(utf8Text, page.currentFontSize);
}

PdfCanvas& PdfCanvas::SetType1FontAndSize(const PdfType1Font& font, const double size) {
    if (size <= 0.0 || !std::isfinite(size)) throw std::invalid_argument("Font size must be positive and finite.");
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    std::size_t index = state_->type1Fonts.size();
    for (std::size_t i = 0; i < state_->type1Fonts.size(); ++i) {
        if (state_->type1Fonts[i].font.GetBytes() == font.GetBytes()) { index = i; break; }
    }
    if (index == state_->type1Fonts.size()) {
        state_->type1Fonts.push_back({font, "T1" + std::to_string(index + 1U)});
    }
    auto& page = state_->pages[pageIndex_];
    page.activeType1FontIndex = index;
    page.currentFontSize = size;
    if (std::find(page.type1FontIndices.begin(), page.type1FontIndices.end(), index) == page.type1FontIndices.end()) {
        page.type1FontIndices.push_back(index);
    }
    Append("/" + state_->type1Fonts[index].resourceName + " " + number(size) + " Tf\n");
    return *this;
}

PdfCanvas& PdfCanvas::ShowType1Text(std::string latin1Text) {
    auto& page = state_->pages[pageIndex_];
    if (!page.activeType1FontIndex && !page.activeCffFontIndex) {
        throw std::logic_error("ShowType1Text requires SetType1FontAndSize or SetEmbeddedCffFontAndSize first.");
    }
    Append("(" + escape(latin1Text) + ") Tj\n");
    return *this;
}

PdfCanvas& PdfCanvas::SetEmbeddedCffFontAndSize(const PdfCffFont& font, const double size) {
    if (size <= 0.0 || !std::isfinite(size)) throw std::invalid_argument("Font size must be positive and finite.");
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    std::size_t index = state_->cffFonts.size();
    for (std::size_t i = 0; i < state_->cffFonts.size(); ++i) {
        if (state_->cffFonts[i].font.data == font.data) { index = i; break; }
    }
    if (index == state_->cffFonts.size()) {
        state_->cffFonts.push_back({font, "CFF" + std::to_string(index + 1U)});
    }
    auto& page = state_->pages[pageIndex_];
    page.activeCffFontIndex = index;
    page.currentFontSize = size;
    if (std::find(page.cffFontIndices.begin(), page.cffFontIndices.end(), index) == page.cffFontIndices.end()) {
        page.cffFontIndices.push_back(index);
    }
    Append("/" + state_->cffFonts[index].resourceName + " " + number(size) + " Tf\n");
    return *this;
}


PdfCanvas& PdfCanvas::SetType3FontAndSize(const PdfType3Font& font, const double size) {
    if (size <= 0.0 || !std::isfinite(size)) {
        throw std::invalid_argument("Font size must be positive and finite.");
    }
    if (font.Empty()) throw std::invalid_argument("Type3 font must contain at least one glyph.");
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    std::size_t index = state_->type3Fonts.size();
    for (std::size_t i = 0; i < state_->type3Fonts.size(); ++i) {
        if (type3FontsEqual(state_->type3Fonts[i].font, font)) {
            index = i;
            break;
        }
    }
    if (index == state_->type3Fonts.size()) {
        state_->type3Fonts.push_back({font, "T3" + std::to_string(index + 1U)});
    }
    auto& page = state_->pages[pageIndex_];
    page.activeType3FontIndex = index;
    page.activeEmbeddedFontIndex.reset();
    page.activeType1FontIndex.reset();
    page.activeCffFontIndex.reset();
    page.currentFontSize = size;
    page.fontName = font.GetFontName();
    if (std::find(page.type3FontIndices.begin(), page.type3FontIndices.end(), index) ==
        page.type3FontIndices.end()) {
        page.type3FontIndices.push_back(index);
    }
    Append("/" + state_->type3Fonts[index].resourceName + " " + number(size) + " Tf\n");
    return *this;
}

PdfCanvas& PdfCanvas::ShowType3Text(std::string encodedBytes) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    const auto& page = state_->pages[pageIndex_];
    if (!page.activeType3FontIndex || *page.activeType3FontIndex >= state_->type3Fonts.size()) {
        throw std::logic_error("ShowType3Text requires SetType3FontAndSize first.");
    }
    const auto& font = state_->type3Fonts[*page.activeType3FontIndex].font;
    for (const unsigned char code : encodedBytes) {
        if (!font.FindGlyphByCode(code)) {
            throw std::invalid_argument("Type3 text contains an undefined character code.");
        }
    }
    Append("(" + escape(encodedBytes) + ") Tj\n");
    return *this;
}

PdfCanvas& PdfCanvas::ShowType3TextUtf8(std::string utf8Text) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    const auto& page = state_->pages[pageIndex_];
    if (!page.activeType3FontIndex || *page.activeType3FontIndex >= state_->type3Fonts.size()) {
        throw std::logic_error("ShowType3TextUtf8 requires SetType3FontAndSize first.");
    }
    const auto& font = state_->type3Fonts[*page.activeType3FontIndex].font;
    std::string encoded;
    for (const auto codePoint : decodeUtf8(utf8Text)) {
        const auto* glyph = font.FindGlyphByUnicode(codePoint);
        if (!glyph) {
            throw std::invalid_argument("Type3 font does not define a requested Unicode code point.");
        }
        encoded.push_back(static_cast<char>(glyph->code));
    }
    return ShowType3Text(std::move(encoded));
}

PdfCanvas& PdfCanvas::SetVerticalWriting(const bool vertical) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& page = state_->pages[pageIndex_];
    if (vertical == page.verticalWriting) return *this;
    page.verticalWriting = vertical;
    // Rotate the text line matrix 90° CCW (horizontal baseline -> vertical).
    if (vertical) {
        SetTextMatrix(0, -1, 1, 0, 0, 0);
    } else {
        SetTextMatrix(1, 0, 0, 1, 0, 0);
    }
    return *this;
}

bool PdfCanvas::IsVerticalWriting() const noexcept {
    if (!state_ || pageIndex_ >= state_->pages.size()) return false;
    return state_->pages[pageIndex_].verticalWriting;
}

PdfCanvas& PdfCanvas::ShowTextVertical(std::string utf8Text) {
    const bool wasVertical = IsVerticalWriting();
    SetVerticalWriting(true);
    ShowTextUtf8(std::move(utf8Text));
    SetVerticalWriting(wasVertical);
    return *this;
}
PdfCanvas& PdfCanvas::SetTextMatrix(double a,double b,double c,double d,double e,double f){Append(number(a)+" "+number(b)+" "+number(c)+" "+number(d)+" "+number(e)+" "+number(f)+" Tm\n");return *this;}
PdfCanvas& PdfCanvas::MoveText(double x,double y){Append(number(x)+" "+number(y)+" Td\n");return *this;}
PdfCanvas& PdfCanvas::SetTextRenderMode(std::uint8_t mode){if(mode>7U)throw std::invalid_argument("Text rendering mode must be 0..7.");Append(std::to_string(mode)+" Tr\n");return *this;}
PdfCanvas& PdfCanvas::SetTextLeading(double leading){Append(number(leading)+" TL\n");return *this;}
PdfCanvas& PdfCanvas::SetTextRise(double rise){Append(number(rise)+" Ts\n");return *this;}
PdfCanvas& PdfCanvas::SetHorizontalScaling(double scale){if(scale<0)throw std::invalid_argument("Horizontal scaling must be non-negative.");Append(number(scale)+" Tz\n");return *this;}
PdfCanvas& PdfCanvas::SetCharSpacing(double spacing){Append(number(spacing)+" Tc\n");return *this;}
PdfCanvas& PdfCanvas::SetWordSpacing(double spacing){Append(number(spacing)+" Tw\n");return *this;}
PdfCanvas& PdfCanvas::ShowText(std::string text){Append("("+escape(text)+") Tj\n");return *this;}
PdfCanvas& PdfCanvas::ShowTextUtf8(std::string text){
    auto& page=state_->pages[pageIndex_]; if(!page.activeEmbeddedFontIndex) throw std::logic_error("ShowTextUtf8 requires SetTrueTypeFontAndSize first.");
    auto& embedded=state_->embeddedFonts.at(*page.activeEmbeddedFontIndex); std::string hex;
    for(const auto cp:decodeUtf8(text)){ const auto gid=embedded.font.GetGlyphId(cp); if(!gid) throw std::invalid_argument("The selected TrueType font does not contain a requested Unicode code point."); hex+=hex4(*gid); if(std::find(embedded.usedMappings.begin(),embedded.usedMappings.end(),std::pair{cp,*gid})==embedded.usedMappings.end()) embedded.usedMappings.emplace_back(cp,*gid); }
    Append("<"+hex+"> Tj\n"); return *this;
}

PdfCanvas& PdfCanvas::ShowTextUtf8WithFallback(
    std::string text,
    std::span<const PdfTrueTypeFont> fallbackFonts) {
    auto& page = state_->pages[pageIndex_];
    if (!page.activeEmbeddedFontIndex) throw std::logic_error("ShowTextUtf8WithFallback requires SetTrueTypeFontAndSize first.");
    const auto primaryIndex = *page.activeEmbeddedFontIndex;
    const auto& primary = state_->embeddedFonts.at(primaryIndex).font;
    double fontSize = GetCurrentFontSize();
    const PdfTrueTypeFont* current = &primary;
    std::vector<std::uint32_t> run;
    const auto flushRun = [&]() {
        if (run.empty()) return;
        SetTrueTypeFontAndSize(*current, fontSize);
        auto& active = state_->embeddedFonts.at(*page.activeEmbeddedFontIndex);
        std::string hex;
        std::uint16_t previousGlyph = 0xFFFFU;
        for (const auto cp : run) {
            const auto gid = current->GetGlyphId(cp);
            if (!gid) continue;
            double adjustment = 0.0;
            if (previousGlyph != 0xFFFFU) {
                adjustment = current->GetCachedKerning(previousGlyph, *gid, fontSize);
            }
            if (adjustment != 0.0 && !hex.empty()) {
                Append("[" + hex + " " + std::to_string(-adjustment) + "] TJ\n");
                hex.clear();
            }
            hex += hex4(*gid);
            if (std::find(active.usedMappings.begin(), active.usedMappings.end(),
                          std::pair{cp, *gid}) == active.usedMappings.end()) {
                active.usedMappings.emplace_back(cp, *gid);
            }
            previousGlyph = *gid;
        }
        if (!hex.empty()) Append("<" + hex + "> Tj\n");
        run.clear();
    };
    for (const auto cp : decodeUtf8(text)) {
        const PdfTrueTypeFont* chosen = &primary;
        if (!primary.Supports(cp)) {
            chosen = nullptr;
            for (const auto& candidate : fallbackFonts) {
                if (candidate.Supports(cp)) { chosen = &candidate; break; }
            }
        }
        if (chosen != current) {
            flushRun();
            current = chosen;
        }
        run.push_back(cp);
    }
    flushRun();
    return *this;
}
PdfCanvas& PdfCanvas::EndText(){Append("ET\n");return *this;}

PdfCanvas& PdfCanvas::DrawTextUtf8(const PdfTrueTypeFont& font, std::string text, const PdfTextLayoutOptions& options) {
    const auto width = options.box.width();
    const auto height = options.box.height();
    if (width <= 0 || height <= 0) throw std::invalid_argument("Text layout box must be non-empty.");
    const auto lines = wrapUtf8(font, text, options.fontSize, width, options.wrap);
    const auto lineHeight = font.GetLineHeight(options.fontSize, options.lineSpacing);
    double baseline = options.box.top - double(font.GetMetrics().ascent) * options.fontSize / font.GetMetrics().unitsPerEm;
    if (!lines.empty() && baseline - (lines.size() - 1U) * lineHeight < options.box.bottom) throw std::invalid_argument("Text does not fit vertically in the layout box.");
    BeginText().SetTrueTypeFontAndSize(font, options.fontSize);
    for (const auto& line : lines) {
        const auto lineWidth = font.MeasureTextUtf8(line, options.fontSize);
        double x = options.box.left;
        if (options.alignment == PdfTextAlignment::Center) x += (width - lineWidth) / 2.0;
        else if (options.alignment == PdfTextAlignment::Right) x += width - lineWidth;
        SetTextMatrix(1, 0, 0, 1, x, baseline).ShowTextUtf8(line);
        baseline -= lineHeight;
    }
    return EndText();
}

PdfTextLayoutResult PdfCanvas::MeasureTextLayout(
    const PdfTrueTypeFont& font, const std::string_view text,
    const PdfTextLayoutOptions& options) {
    if (options.box.width() <= 0.0 || options.box.height() <= 0.0 || options.fontSize <= 0.0)
        throw std::invalid_argument("Text layout options must be positive and non-empty.");
    const auto lines = wrapUtf8(font, std::string(text), options.fontSize,
                                options.box.width(), options.wrap);
    PdfTextLayoutResult result;
    result.lineCount = lines.size();
    for (const auto& line : lines) result.width = std::max(result.width, font.MeasureTextUtf8(line, options.fontSize));
    result.height = result.lineCount * font.GetLineHeight(options.fontSize, options.lineSpacing);
    return result;
}

PdfCanvas& PdfCanvas::BeginMarkedContent(std::string role,
                                               std::string alternativeText,
                                               std::string actualText) {
    PdfMarkedContentOptions options;
    options.role = std::move(role);
    options.alternativeText = std::move(alternativeText);
    options.actualText = std::move(actualText);
    return BeginMarkedContent(options);
}

PdfCanvas& PdfCanvas::BeginMarkedContent(const PdfMarkedContentOptions& options) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    const auto role = pdfName(options.role);
    state_->tagged = true;
    auto& page = state_->pages[pageIndex_];
    const auto itemIndex = page.markedContents.size();
    const auto mcid = static_cast<std::uint32_t>(itemIndex);
    Internal::PdfWriterMarkedContent item;
    item.mcid = mcid;
    item.role = role;
    item.alternativeText = options.alternativeText;
    item.actualText = options.actualText;
    item.language = options.language;
    item.title = options.title;
    item.expandedText = options.expandedText;
    item.identifier = options.identifier;
    item.attributes = options.attributes;
    if (item.attributes.rowSpan == 0U || item.attributes.columnSpan == 0U) {
        throw std::invalid_argument("Structure table row/column spans must be at least one.");
    }
    const auto finiteOptional = [](const std::optional<double>& value) {
        return !value || std::isfinite(*value);
    };
    if (!finiteOptional(item.attributes.width) || !finiteOptional(item.attributes.height)) {
        throw std::invalid_argument("Structure layout dimensions must be finite.");
    }
    if (!page.markedContentStack.empty() && page.markedContentStack.back()) {
        item.parentIndex = *page.markedContentStack.back();
    }
    page.markedContents.push_back(std::move(item));
    if (page.markedContents[itemIndex].parentIndex) {
        page.markedContents[*page.markedContents[itemIndex].parentIndex].childIndices.push_back(itemIndex);
    }
    page.markedContentStack.push_back(itemIndex);
    page.openMarkedContentDepth = page.markedContentStack.size();

    std::string properties = " /MCID " + std::to_string(mcid);
    if (!options.actualText.empty()) properties += " /ActualText (" + escape(options.actualText) + ")";
    Append("/" + role + " <<" + properties + " >> BDC\n");
    return *this;
}

PdfCanvas& PdfCanvas::BeginArtifact(std::string artifactType) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& page = state_->pages[pageIndex_];
    page.markedContentStack.push_back(std::nullopt);
    page.openMarkedContentDepth = page.markedContentStack.size();
    if (artifactType.empty()) {
        Append("/Artifact BMC\n");
    } else {
        artifactType = pdfName(std::move(artifactType));
        Append("/Artifact << /Type /" + artifactType + " >> BDC\n");
    }
    return *this;
}

PdfCanvas& PdfCanvas::EndMarkedContent() {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    auto& page = state_->pages[pageIndex_];
    if (page.markedContentStack.empty()) {
        throw std::logic_error("EndMarkedContent has no matching BeginMarkedContent or BeginArtifact.");
    }
    page.markedContentStack.pop_back();
    page.openMarkedContentDepth = page.markedContentStack.size();
    Append("EMC\n");
    return *this;
}

PdfCanvas& PdfCanvas::DrawImage(const PdfImage& image, const PdfRectangle& rectangle){
    if(!state_||pageIndex_>=state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    if (rectangle.empty()) throw std::invalid_argument("Image rectangle must be non-empty.");
    const auto hash = imageHash(image);
    std::size_t imageIndex = state_->images.size();
    const auto bucket = state_->imageCache.find(hash);
    if (bucket != state_->imageCache.end()) {
        for (const auto candidate : bucket->second) {
            if (candidate < state_->images.size() && imagesEqual(state_->images[candidate].image, image)) {
                imageIndex = candidate;
                break;
            }
        }
    }
    if (imageIndex == state_->images.size()) {
        const std::string resourceName="Im"+std::to_string(imageIndex+1U);
        state_->images.push_back(Internal::PdfWriterImage{image,resourceName});
        state_->imageCache[hash].push_back(imageIndex);
    }
    auto& pageImages = state_->pages[pageIndex_].imageIndices;
    if (std::find(pageImages.begin(), pageImages.end(), imageIndex) == pageImages.end()) {
        pageImages.push_back(imageIndex);
    }
    const auto& resourceName = state_->images[imageIndex].resourceName;
    const double width=rectangle.width();
    const double height=rectangle.height();
    Append("q\n"+number(width)+" 0 0 "+number(height)+" "+number(rectangle.left)+" "+number(rectangle.bottom)+" cm\n/"+resourceName+" Do\nQ\n");
    return *this;
}

PdfCanvas& PdfCanvas::DrawInlineImage(const PdfImage& image, const PdfRectangle& rectangle) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    if (rectangle.empty()) throw std::invalid_argument("Inline image rectangle must be non-empty.");
    if (image.HasSoftMask()) {
        throw std::invalid_argument("Inline images cannot carry a separate soft mask; use DrawImage instead.");
    }
    std::string filters = "/ASCIIHexDecode";
    std::string decodeParameters;
    switch (image.GetEncoding()) {
    case PdfImageEncoding::Raw: break;
    case PdfImageEncoding::Flate: filters = "[/ASCIIHexDecode /FlateDecode]"; break;
    case PdfImageEncoding::Dct: filters = "[/ASCIIHexDecode /DCTDecode]"; break;
    case PdfImageEncoding::Jpx: filters = "[/ASCIIHexDecode /JPXDecode]"; break;
    case PdfImageEncoding::CcittFax:
        filters = "[/ASCIIHexDecode /CCITTFaxDecode]";
        decodeParameters = " /DecodeParms [null << /K -1 /Columns " +
            std::to_string(image.GetWidth()) + " /Rows " + std::to_string(image.GetHeight()) + " >>]";
        break;
    default:
        throw std::invalid_argument("This image encoding is not supported for inline images.");
    }
    std::ostringstream dictionary;
    dictionary << "BI\n/W " << image.GetWidth() << " /H " << image.GetHeight()
               << " /BPC " << image.GetBitsPerComponent() << " /CS "
               << inlineColorSpace(image.GetColorSpace()) << " /F " << filters
               << decodeParameters << "\nID\n";
    Append("q\n" + number(rectangle.width()) + " 0 0 " + number(rectangle.height()) + " " +
           number(rectangle.left) + " " + number(rectangle.bottom) + " cm\n" +
           dictionary.str() + asciiHex(image.GetBytes()) + "EI\nQ\n");
    return *this;
}


PdfCanvas& PdfCanvas::PaintShading(std::string shadingName) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    shadingName = pdfName(std::move(shadingName));
    const auto iterator = std::find_if(state_->meshShadings.begin(), state_->meshShadings.end(),
        [&](const auto& shading) { return shading.resourceName == shadingName; });
    if (iterator == state_->meshShadings.end()) {
        throw std::invalid_argument("Unknown mesh shading: " + shadingName);
    }
    auto& page = state_->pages[pageIndex_];
    const auto index = static_cast<std::size_t>(iterator - state_->meshShadings.begin());
    if (std::find(page.shadingIndices.begin(), page.shadingIndices.end(), index) ==
        page.shadingIndices.end()) {
        page.shadingIndices.push_back(index);
    }
    Append("/" + shadingName + " sh\n");
    return *this;
}

PdfCanvas& PdfCanvas::BeginLayer(std::string layerName) {
    if (!state_ || pageIndex_ >= state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    const auto layer = std::find_if(state_->ocgs.begin(), state_->ocgs.end(),
        [&](const auto& item) { return item.name == layerName; });
    if (layer == state_->ocgs.end()) {
        throw std::invalid_argument("Layer is not registered: " + layerName);
    }
    const std::string resourceName = "OC" + std::to_string(
        static_cast<std::size_t>(std::distance(state_->ocgs.begin(), layer)) + 1U);
    state_->pages[pageIndex_].ocgResources.insert(resourceName);
    Append("/"+resourceName+" BDC\n");
    return *this;
}

PdfCanvas& PdfCanvas::EndLayer() {
    Append("EMC\n");
    return *this;
}
} // namespace CPPPdf

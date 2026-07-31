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
}
PdfCanvas::PdfCanvas(std::shared_ptr<Internal::PdfWriterState> state, std::size_t pageIndex):state_(std::move(state)),pageIndex_(pageIndex){}
void PdfCanvas::Append(const std::string& c){ if(!state_||pageIndex_>=state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page"); state_->pages[pageIndex_].content += c; }
std::string PdfCanvas::RegisterOpacity(double strokeOpacity, double fillOpacity) {
    strokeOpacity = opacityValue(strokeOpacity); fillOpacity = opacityValue(fillOpacity);
    for (std::size_t i=0;i<state_->extGStates.size();++i) {
        const auto& gs=state_->extGStates[i];
        if (std::abs(gs.strokeOpacity-strokeOpacity)<1e-9 && std::abs(gs.fillOpacity-fillOpacity)<1e-9) {
            auto& refs=state_->pages[pageIndex_].extGStateIndices;
            if(std::find(refs.begin(),refs.end(),i)==refs.end()) refs.push_back(i);
            return gs.resourceName;
        }
    }
    const std::size_t index=state_->extGStates.size();
    const std::string name="GS"+std::to_string(index+1U);
    state_->extGStates.push_back({strokeOpacity,fillOpacity,name});
    state_->pages[pageIndex_].extGStateIndices.push_back(index);
    return name;
}
PdfCanvas& PdfCanvas::SaveState(){Append("q\n");return *this;} PdfCanvas& PdfCanvas::RestoreState(){Append("Q\n");return *this;}
PdfCanvas& PdfCanvas::SetStrokeColor(PdfColor c){Append(number(c.r)+" "+number(c.g)+" "+number(c.b)+" RG\n");return *this;}
PdfCanvas& PdfCanvas::SetFillColor(PdfColor c){Append(number(c.r)+" "+number(c.g)+" "+number(c.b)+" rg\n");return *this;}
PdfCanvas& PdfCanvas::SetStrokeOpacity(double opacity){Append("/"+RegisterOpacity(opacity,1.0)+" gs\n");return *this;}
PdfCanvas& PdfCanvas::SetFillOpacity(double opacity){Append("/"+RegisterOpacity(1.0,opacity)+" gs\n");return *this;}
PdfCanvas& PdfCanvas::SetOpacity(double opacity){Append("/"+RegisterOpacity(opacity,opacity)+" gs\n");return *this;}
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
PdfCanvas& PdfCanvas::BeginText(){Append("BT\n");return *this;}
PdfCanvas& PdfCanvas::SetFontAndSize(std::string font,double size){ if(!font.empty()&&font.front()=='/')font.erase(font.begin()); auto& page=state_->pages[pageIndex_]; page.fontName=std::move(font); page.activeEmbeddedFontIndex.reset(); Append("/F1 "+number(size)+" Tf\n");return *this;}
PdfCanvas& PdfCanvas::SetTrueTypeFontAndSize(const PdfTrueTypeFont& font,double size){
    if(size<=0||!std::isfinite(size)) throw std::invalid_argument("Font size must be positive and finite.");
    std::size_t index=state_->embeddedFonts.size();
    for(std::size_t i=0;i<state_->embeddedFonts.size();++i){ if(state_->embeddedFonts[i].font.GetBytes()==font.GetBytes()){ index=i; break; } }
    if(index==state_->embeddedFonts.size()) state_->embeddedFonts.push_back({font,"FT"+std::to_string(index+1U),{}});
    auto& page=state_->pages[pageIndex_]; page.activeEmbeddedFontIndex=index; if(std::find(page.embeddedFontIndices.begin(),page.embeddedFontIndices.end(),index)==page.embeddedFontIndices.end()) page.embeddedFontIndices.push_back(index);
    Append("/"+state_->embeddedFonts[index].resourceName+" "+number(size)+" Tf\n"); return *this;
}
PdfCanvas& PdfCanvas::SetTextMatrix(double a,double b,double c,double d,double e,double f){Append(number(a)+" "+number(b)+" "+number(c)+" "+number(d)+" "+number(e)+" "+number(f)+" Tm\n");return *this;}
PdfCanvas& PdfCanvas::MoveText(double x,double y){Append(number(x)+" "+number(y)+" Td\n");return *this;}
PdfCanvas& PdfCanvas::ShowText(std::string text){Append("("+escape(text)+") Tj\n");return *this;}
PdfCanvas& PdfCanvas::ShowTextUtf8(std::string text){
    auto& page=state_->pages[pageIndex_]; if(!page.activeEmbeddedFontIndex) throw std::logic_error("ShowTextUtf8 requires SetTrueTypeFontAndSize first.");
    auto& embedded=state_->embeddedFonts.at(*page.activeEmbeddedFontIndex); std::string hex;
    for(const auto cp:decodeUtf8(text)){ const auto gid=embedded.font.GetGlyphId(cp); if(!gid) throw std::invalid_argument("The selected TrueType font does not contain a requested Unicode code point."); hex+=hex4(*gid); if(std::find(embedded.usedMappings.begin(),embedded.usedMappings.end(),std::pair{cp,*gid})==embedded.usedMappings.end()) embedded.usedMappings.emplace_back(cp,*gid); }
    Append("<"+hex+"> Tj\n"); return *this;
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

PdfCanvas& PdfCanvas::DrawImage(const PdfImage& image, const PdfRectangle& rectangle){
    if(!state_||pageIndex_>=state_->pages.size()) throw std::runtime_error("Invalid PdfCanvas page");
    const std::size_t imageIndex=state_->images.size();
    const std::string resourceName="Im"+std::to_string(imageIndex+1U);
    state_->images.push_back(Internal::PdfWriterImage{image,resourceName});
    state_->pages[pageIndex_].imageIndices.push_back(imageIndex);
    const double width=rectangle.right-rectangle.left;
    const double height=rectangle.top-rectangle.bottom;
    Append("q\n"+number(width)+" 0 0 "+number(height)+" "+number(rectangle.left)+" "+number(rectangle.bottom)+" cm\n/"+resourceName+" Do\nQ\n");
    return *this;
}
} // namespace CPPPdf

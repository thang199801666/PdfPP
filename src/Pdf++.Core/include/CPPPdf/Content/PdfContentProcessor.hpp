#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf {

enum class PdfContentEventType {
    BeginText,
    EndText,
    RenderText,
    MoveText,
    SetTextMatrix,
    SetFont,
    SetCharacterSpacing,
    SetWordSpacing,
    SetHorizontalScaling,
    SetLeading,
    SetTextRenderingMode,
    SetTextRise,
    SetLineWidth,
    SetStrokeColor,
    SetFillColor,
    SaveState,
    RestoreState,
    ConcatenateMatrix,
    RenderPath,
    InvokeXObject,
    RenderInlineImage,
    PaintShading,
    BeginTransparencyGroup,
    EndTransparencyGroup,
    BeginMarkedContent,
    EndMarkedContent,
    UnknownOperator
};

struct PdfTransparencyGroupProperties final {
    std::string blendMode{"Normal"};
    bool isolated{};
    bool knockout{};
    double alpha{1.0};
};

struct PdfTextStateSnapshot {
    std::string fontResource;
    double fontSize{};
    double characterSpacing{};
    double wordSpacing{};
    double horizontalScaling{100.0};
    double leading{};
    int renderingMode{};
    double rise{};
    double lineWidth{1.0};
    int lineCap{};
    int lineJoin{};
    double miterLimit{10.0};
    double strokeAlpha{1.0};
    double fillAlpha{1.0};
    bool transparencyIsolated{};
    bool transparencyKnockout{};
    std::array<double, 3> strokeColor{0.0, 0.0, 0.0};
    std::array<double, 3> fillColor{0.0, 0.0, 0.0};
    std::array<double, 6> textMatrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    std::array<double, 6> currentTransformationMatrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
};

struct PdfInlineImageProperty {
    std::string name;
    std::string value;
};

struct PdfContentEvent {
    PdfContentEventType type{};
    std::string text;
    std::string operation;
    std::string resourceScope;
    std::uint32_t resourceObjectNumber{};
    std::vector<double> numbers;
    std::vector<std::string> textSegments;
    std::vector<double> textSegmentAdjustments;
    std::vector<double> textAdjustments;
    PdfTextStateSnapshot textState;
    std::vector<PdfInlineImageProperty> inlineImageDictionary;
    std::vector<std::byte> bytes;
    PdfTransparencyGroupProperties transparencyGroup;
    std::string markedContentProperty;
};

class PdfContentProcessor final {
public:
    using Handler = std::function<void(const PdfContentEvent&)>;

    void SetHandler(Handler handler) { handler_ = std::move(handler); }
    void Process(
        std::string_view content,
        const PdfTextStateSnapshot& initialState = {}) const;

private:
    Handler handler_;
};

} // namespace CPPPdf

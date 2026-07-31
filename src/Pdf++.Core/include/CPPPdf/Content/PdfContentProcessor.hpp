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
    UnknownOperator
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
    std::vector<double> numbers;
    PdfTextStateSnapshot textState;
    std::vector<PdfInlineImageProperty> inlineImageDictionary;
    std::vector<std::byte> bytes;
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

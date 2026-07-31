#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace CPPPdf {

struct PdfReference {
    std::uint32_t objectNumber{};
    std::uint16_t generation{};
};

struct PdfPoint final {
    double x{};
    double y{};
};

struct PdfRectangle final {
    double left{};
    double bottom{};
    double right{};
    double top{};

    [[nodiscard]] double width() const noexcept { return right - left; }
    [[nodiscard]] double height() const noexcept { return top - bottom; }
    [[nodiscard]] bool empty() const noexcept { return width() <= 0.0 || height() <= 0.0; }
};

[[nodiscard]] constexpr bool operator==(const PdfReference& lhs, const PdfReference& rhs) noexcept {
    return lhs.objectNumber == rhs.objectNumber && lhs.generation == rhs.generation;
}

[[nodiscard]] constexpr bool operator==(const PdfPoint& lhs, const PdfPoint& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] constexpr bool operator==(const PdfRectangle& lhs, const PdfRectangle& rhs) noexcept {
    return lhs.left == rhs.left && lhs.bottom == rhs.bottom &&
           lhs.right == rhs.right && lhs.top == rhs.top;
}

struct PdfDocumentInfo {
    std::string title;
    std::string author;
    std::string subject;
    std::string keywords;
    std::string creator;
    std::string producer;
    std::string creationDate;
    std::string modificationDate;
};

struct PdfPageInfo {
    std::size_t pageIndex{};
    std::uint32_t objectNumber{};
    PdfRectangle mediaBox{};
    PdfRectangle cropBox{};
    int rotation{};
};

struct PdfTextExtractionOptions {
    bool preserveLayout{true};
    bool insertSpaces{true};
    double lineTolerance{0.25};
    double wordGapThreshold{3.0};
};

struct PdfTextPage {
    std::size_t pageIndex{};
    std::string text;

    [[nodiscard]] const std::string& GetText() const noexcept { return text; }
};

} // namespace CPPPdf

#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Writer/PdfWriter.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf {

// High-level document layout helpers built on PdfWriter. These provide
// paragraph flow, bullet/numbered lists, multi-column text, headers and
// footers, and automatic page breaks for flowing content.
class PdfDocumentLayout final {
public:
    enum class ListStyle { Bullet, Decimal, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman };

    struct ListOptions final {
        ListStyle style{ListStyle::Bullet};
        double indent{18.0};
        double spacing{4.0};
        double lineSpacing{1.2};
        double fontSize{12.0};
        bool bold{false};
    };

    struct ParagraphOptions final {
        std::string text;
        PdfTextAlignment alignment{PdfTextAlignment::Left};
        double firstLineIndent{0.0};
        double leftIndent{0.0};
        double rightIndent{0.0};
        double spaceBefore{0.0};
        double spaceAfter{0.0};
        double lineSpacing{1.2};
        double fontSize{12.0};
        bool bold{false};
        bool wrap{true};
        // Optional: the font to use for this paragraph (embedded TrueType).
        // When empty, the layout uses its configured default font.
        std::shared_ptr<const PdfTrueTypeFont> font;
    };

    struct HeaderFooterOptions final {
        std::string leftText;
        std::string centerText;
        std::string rightText;
        double fontSize{10.0};
        // /PAGE /NPAGE are replaced with the 1-based and absolute page numbers.
        bool pageNumber{false};
        bool pageNumberCenter{true};
        double topMargin{36.0};
        double bottomMargin{36.0};
        std::shared_ptr<const PdfTrueTypeFont> font;
    };

    struct LayoutResult final {
        std::size_t pagesWritten{};
        std::size_t paragraphsFlowed{};
    };

    explicit PdfDocumentLayout(PdfWriter& writer) : writer_(writer) {}

    // Sets the default embedded TrueType font used by paragraphs without their
    // own font. Paragraphs (and list/header/footer items) without a font fall
    // back to the base-14 Helvetica when no default is set.
    void SetDefaultFont(const PdfTrueTypeFont& font) {
        defaultFont_ = std::make_shared<const PdfTrueTypeFont>(font);
    }

    // Flows a sequence of paragraphs into the page flow, inserting page breaks
    // when content exceeds the printable area. Returns paragraphs written.
    [[nodiscard]] LayoutResult FlowParagraphs(
        const std::vector<ParagraphOptions>& paragraphs,
        const PdfRectangle& flowBox,
        std::size_t startPage = 0U,
        std::size_t* currentPage = nullptr);

    // Draws a bulleted or numbered list starting at `y`, returning the y
    // position after the list.
    [[nodiscard]] double DrawList(
        std::size_t pageIndex,
        const std::vector<std::string>& items,
        double x, double y,
        const ListOptions& options = {});

    // Draws text in two or more columns. `gap` is the horizontal gap between
    // columns. Returns the number of columns used.
    [[nodiscard]] std::size_t DrawColumns(
        std::size_t pageIndex,
        const std::vector<std::string>& columns,
        const PdfRectangle& box,
        double gap = 18.0,
        const ParagraphOptions& options = {});

    // Draws a header on every page from `startPage` to `endPage` (inclusive).
    [[nodiscard]] std::size_t DrawHeader(
        std::size_t startPage, std::size_t endPage,
        const PdfRectangle& pageBox,
        const HeaderFooterOptions& options);

    // Draws a footer on every page from `startPage` to `endPage` (inclusive).
    [[nodiscard]] std::size_t DrawFooter(
        std::size_t startPage, std::size_t endPage,
        const PdfRectangle& pageBox,
        const HeaderFooterOptions& options);

private:
    PdfWriter& writer_;
    std::shared_ptr<const PdfTrueTypeFont> defaultFont_;

    [[nodiscard]] const PdfTrueTypeFont* FontFor(const ParagraphOptions& options) const;
    [[nodiscard]] std::vector<std::string> SplitLines(
        const std::string& text, double width, const ParagraphOptions& options) const;
};

} // namespace CPPPdf

#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Writer/PdfWriter.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

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

    struct TextRun final {
        std::string text;
        // Base-14 font name used when `font` is not set. Common values are
        // Helvetica, Helvetica-Bold, Times-Roman, Times-Bold and Courier.
        std::string base14Font{"Helvetica"};
        double fontSize{12.0};
        PdfColor color{PdfColor::Black()};
        double characterSpacing{0.0};
        double textRise{0.0};
        bool underline{false};
        bool strikeThrough{false};
        std::shared_ptr<const PdfTrueTypeFont> font;
    };

    struct RichParagraphOptions final {
        std::vector<TextRun> runs;
        PdfTextAlignment alignment{PdfTextAlignment::Left};
        double firstLineIndent{0.0};
        double leftIndent{0.0};
        double rightIndent{0.0};
        double spaceBefore{0.0};
        double spaceAfter{0.0};
        double lineSpacing{1.2};
        bool wrap{true};
        // Avoids starting a paragraph on a page when fewer than this many
        // lines fit. Values greater than the paragraph line count keep the
        // complete paragraph together whenever it fits on one page.
        std::size_t orphanLines{2U};
        std::size_t widowLines{2U};
        bool keepTogether{false};
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
        std::size_t currentPage{};
        double currentY{};
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


    // Flows mixed-style paragraph runs across pages. Runs can switch fonts,
    // sizes, colors, rise and decoration inside one paragraph. Wrapping is
    // Unicode grapheme-safe and honors explicit newlines.
    [[nodiscard]] LayoutResult FlowRichParagraphs(
        const std::vector<RichParagraphOptions>& paragraphs,
        const PdfRectangle& flowBox,
        std::size_t startPage = 0U,
        double startY = 0.0);

    // Draws a bulleted or numbered list starting at `y`, returning the y
    // position after the list.
    [[nodiscard]] double DrawList(
        std::size_t pageIndex,
        const std::vector<std::string>& items,
        double x, double y,
        const ListOptions& options);
    [[nodiscard]] double DrawList(
        std::size_t pageIndex,
        const std::vector<std::string>& items,
        double x, double y) {
        return DrawList(pageIndex, items, x, y, ListOptions{});
    }

    struct NestedListItem final {
        std::vector<TextRun> runs;
        std::vector<NestedListItem> children;
    };

    struct FlowListOptions final {
        std::vector<ListStyle> levelStyles{ListStyle::Bullet, ListStyle::Decimal, ListStyle::LowerAlpha};
        double fontSize{12.0};
        double lineSpacing{1.2};
        double itemSpacing{3.0};
        double markerWidth{18.0};
        double levelIndent{24.0};
        std::size_t startNumber{1U};
        std::shared_ptr<const PdfTrueTypeFont> font;
    };

    [[nodiscard]] LayoutResult FlowNestedList(
        const std::vector<NestedListItem>& items,
        const PdfRectangle& flowBox,
        const FlowListOptions& options,
        std::size_t startPage = 0U,
        double startY = 0.0);
    [[nodiscard]] LayoutResult FlowNestedList(
        const std::vector<NestedListItem>& items,
        const PdfRectangle& flowBox,
        std::size_t startPage = 0U,
        double startY = 0.0) {
        return FlowNestedList(items, flowBox, FlowListOptions{}, startPage, startY);
    }

    struct TableOptions final {
        double fontSize{10.0};
        double padding{3.0};
        double headerRowHeight{18.0};
        double rowHeight{16.0};
        bool drawGrid{true};
        bool boldHeader{false};
    };


    enum class VerticalAlignment { Top, Middle, Bottom };

    struct TableCell final {
        std::vector<TextRun> runs;
        std::size_t columnSpan{1U};
        std::size_t rowSpan{1U};
        PdfTextAlignment alignment{PdfTextAlignment::Left};
        VerticalAlignment verticalAlignment{VerticalAlignment::Top};
        double padding{4.0};
        std::optional<PdfColor> backgroundColor;
    };

    struct TableRow final {
        std::vector<TableCell> cells;
        bool header{false};
        double minimumHeight{0.0};
        bool keepWithNext{false};
    };

    struct FlowTableOptions final {
        std::size_t columnCount{}; // zero: infer from rows
        std::vector<double> columnWidths; // relative weights when supplied
        double fontSize{10.0};
        double lineSpacing{1.15};
        double minimumRowHeight{18.0};
        double borderWidth{0.5};
        PdfColor borderColor{PdfColor::Gray(0.65)};
        bool drawGrid{true};
        bool autoWidth{true};
        bool repeatHeaderRows{true};
        std::shared_ptr<const PdfTrueTypeFont> font;
    };

    struct TableLayoutResult final {
        std::size_t pagesWritten{};
        std::size_t rowsWritten{};
        std::size_t currentPage{};
        double currentY{};
    };

    // Flows a table across pages. Supports automatic column widths, repeated
    // header rows, column spans, row spans and per-cell rich text/backgrounds.
    // Rows connected by a row span are kept on the same page when possible.
    [[nodiscard]] TableLayoutResult FlowTable(
        const std::vector<TableRow>& rows,
        const PdfRectangle& flowBox,
        const FlowTableOptions& options,
        std::size_t startPage = 0U,
        double startY = 0.0);
    [[nodiscard]] TableLayoutResult FlowTable(
        const std::vector<TableRow>& rows,
        const PdfRectangle& flowBox,
        std::size_t startPage = 0U,
        double startY = 0.0) {
        return FlowTable(rows, flowBox, FlowTableOptions{}, startPage, startY);
    }

    // Draws a table with a header row and data rows. `columnWidths` gives the
    // relative widths (scaled to fit `box`). Returns the y position after the
    // last row.
    [[nodiscard]] double DrawTable(
        std::size_t pageIndex,
        const std::vector<std::string>& headers,
        const std::vector<std::vector<std::string>>& rows,
        const PdfRectangle& box,
        const std::vector<double>& columnWidths,
        const TableOptions& options);
    [[nodiscard]] double DrawTable(
        std::size_t pageIndex,
        const std::vector<std::string>& headers,
        const std::vector<std::vector<std::string>>& rows,
        const PdfRectangle& box,
        const std::vector<double>& columnWidths = {}) {
        return DrawTable(pageIndex, headers, rows, box, columnWidths, TableOptions{});
    }

    // Draws text in two or more columns. `gap` is the horizontal gap between
    // columns. Returns the number of columns used.
    [[nodiscard]] std::size_t DrawColumns(
        std::size_t pageIndex,
        const std::vector<std::string>& columns,
        const PdfRectangle& box,
        double gap,
        const ParagraphOptions& options);
    [[nodiscard]] std::size_t DrawColumns(
        std::size_t pageIndex,
        const std::vector<std::string>& columns,
        const PdfRectangle& box,
        double gap = 18.0) {
        return DrawColumns(pageIndex, columns, box, gap, ParagraphOptions{});
    }

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

    enum class ImageFitMode { Contain, Cover, Stretch };

    struct ImageLayoutOptions final {
        ImageFitMode fit{ImageFitMode::Contain};
        PdfTextAlignment alignment{PdfTextAlignment::Center};
        double requestedWidth{0.0};
        double requestedHeight{0.0};
        double spaceBefore{0.0};
        double spaceAfter{0.0};
        bool keepTogether{true};
    };

    // Places an image in the current flow and performs a page break when the
    // requested image height does not fit. Returns the updated cursor.
    [[nodiscard]] LayoutResult FlowImage(
        const PdfImage& image,
        const PdfRectangle& flowBox,
        const ImageLayoutOptions& options,
        std::size_t startPage = 0U,
        double startY = 0.0);

    // Starts a new page using the current page's media box (or the supplied
    // media box when non-empty) and returns its index.
    [[nodiscard]] std::size_t AddAreaBreak(
        std::size_t currentPage,
        PdfRectangle mediaBox = {});

private:
    PdfWriter& writer_;
    std::shared_ptr<const PdfTrueTypeFont> defaultFont_;

    [[nodiscard]] const PdfTrueTypeFont* FontFor(const ParagraphOptions& options) const;
    [[nodiscard]] std::vector<std::string> SplitLines(
        const std::string& text, double width, const ParagraphOptions& options) const;
};

} // namespace CPPPdf

#include <CPPPdf/Document/PdfDocumentLayout.hpp>

#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <CPPPdf/Graphics/PdfCanvas.hpp>
#include <CPPPdf/Text/PdfTextLayout.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CPPPdf {
namespace {

using TextRun = PdfDocumentLayout::TextRun;

struct StyledFragment final {
    TextRun style;
    std::string text;
    double width{};
};

struct StyledLine final {
    std::vector<StyledFragment> fragments;
    double width{};
    double height{};
    double maximumFontSize{};
};

struct StyledAtom final {
    TextRun style;
    std::string text;
    bool whitespace{};
    bool newline{};
};

[[nodiscard]] double RunFontSize(const TextRun& run, const double fallback = 12.0) {
    return run.fontSize > 0.0 ? run.fontSize : fallback;
}

[[nodiscard]] double MeasureRun(const TextRun& run, const std::string_view text) {
    const double size = RunFontSize(run);
    double width = 0.0;
    if (run.font) {
        width = run.font->MeasureTextUtf8(text, size);
    } else {
        // Base-14 approximation. Courier is fixed-width; Helvetica/Times are
        // close enough at 0.5 em for line-breaking without external metrics.
        const double factor = run.base14Font.find("Courier") != std::string::npos ? 0.6 : 0.5;
        width = static_cast<double>(PdfTextLayout::CountCodePoints(text)) * size * factor;
    }
    if (!text.empty()) {
        width += run.characterSpacing *
            static_cast<double>(PdfTextLayout::CountCodePoints(text) > 0U
                                    ? PdfTextLayout::CountCodePoints(text) - 1U : 0U);
    }
    return std::max(0.0, width);
}

[[nodiscard]] std::vector<StyledAtom> TokenizeRuns(const std::vector<TextRun>& runs) {
    std::vector<StyledAtom> atoms;
    for (const auto& run : runs) {
        const auto clusters = PdfTextLayout::GraphemeClusters(run.text);
        std::string current;
        bool currentWhitespace = false;
        const auto flush = [&] {
            if (current.empty()) return;
            atoms.push_back(StyledAtom{run, current, currentWhitespace, false});
            current.clear();
        };
        for (const auto& cluster : clusters) {
            if (cluster == "\n" || cluster == "\r" || cluster == "\r\n") {
                flush();
                atoms.push_back(StyledAtom{run, {}, false, true});
                continue;
            }
            const bool whitespace = PdfTextLayout::IsWhitespace(cluster);
            if (!current.empty() && whitespace != currentWhitespace) flush();
            if (current.empty()) currentWhitespace = whitespace;
            current += cluster;
        }
        flush();
    }
    return atoms;
}

void AppendFragment(StyledLine& line, const TextRun& style,
                    std::string text, const double width,
                    const double lineSpacing) {
    if (text.empty()) return;
    if (!line.fragments.empty()) {
        auto& back = line.fragments.back();
        if (back.style.font == style.font && back.style.base14Font == style.base14Font &&
            back.style.fontSize == style.fontSize && back.style.color.r == style.color.r &&
            back.style.color.g == style.color.g && back.style.color.b == style.color.b &&
            back.style.characterSpacing == style.characterSpacing &&
            back.style.textRise == style.textRise && back.style.underline == style.underline &&
            back.style.strikeThrough == style.strikeThrough) {
            back.text += text;
            back.width += width;
            line.width += width;
            return;
        }
    }
    line.fragments.push_back(StyledFragment{style, std::move(text), width});
    line.width += width;
    const double fontSize = RunFontSize(style);
    line.maximumFontSize = std::max(line.maximumFontSize, fontSize);
    line.height = std::max(line.height, fontSize * std::max(0.5, lineSpacing));
}

[[nodiscard]] std::vector<StyledLine> BuildStyledLines(
    const std::vector<TextRun>& runs,
    const double firstLineWidth,
    const double normalLineWidth,
    const double lineSpacing,
    const bool wrap) {
    std::vector<StyledLine> lines;
    StyledLine current;
    const auto availableWidth = [&] {
        return std::max(1.0, lines.empty() ? firstLineWidth : normalLineWidth);
    };
    auto finishLine = [&](const bool forceEmpty = false) {
        if (!current.fragments.empty() || forceEmpty) {
            if (current.height <= 0.0) current.height = 12.0 * std::max(0.5, lineSpacing);
            if (current.maximumFontSize <= 0.0) current.maximumFontSize = 12.0;
            // Trim trailing whitespace so alignment and decoration use visible content.
            while (!current.fragments.empty() && PdfTextLayout::IsWhitespace(current.fragments.back().text)) {
                current.width -= current.fragments.back().width;
                current.fragments.pop_back();
            }
            lines.push_back(std::move(current));
            current = StyledLine{};
        }
    };

    for (const auto& atom : TokenizeRuns(runs)) {
        if (atom.newline) {
            finishLine(true);
            continue;
        }
        if (atom.whitespace && current.fragments.empty()) continue;
        const double atomWidth = MeasureRun(atom.style, atom.text);
        if (!wrap || current.width + atomWidth <= availableWidth()) {
            AppendFragment(current, atom.style, atom.text, atomWidth, lineSpacing);
            continue;
        }
        if (!current.fragments.empty()) {
            finishLine();
            if (atom.whitespace) continue;
        }
        if (atomWidth <= availableWidth()) {
            AppendFragment(current, atom.style, atom.text, atomWidth, lineSpacing);
            continue;
        }

        // Long unbreakable token: split at grapheme boundaries.
        std::string piece;
        double pieceWidth = 0.0;
        for (const auto& cluster : PdfTextLayout::GraphemeClusters(atom.text)) {
            const double clusterWidth = MeasureRun(atom.style, cluster);
            if (!piece.empty() && pieceWidth + clusterWidth > availableWidth()) {
                AppendFragment(current, atom.style, piece, pieceWidth, lineSpacing);
                finishLine();
                piece.clear();
                pieceWidth = 0.0;
            }
            piece += cluster;
            pieceWidth += clusterWidth;
        }
        AppendFragment(current, atom.style, piece, pieceWidth, lineSpacing);
    }
    finishLine(lines.empty());
    return lines;
}

void SelectRunFont(PdfCanvas& canvas, const TextRun& run) {
    const double size = RunFontSize(run);
    if (run.font) canvas.SetTrueTypeFontAndSize(*run.font, size);
    else canvas.SetFontAndSize(run.base14Font.empty() ? "Helvetica" : run.base14Font, size);
}

void DrawStyledLine(PdfCanvas canvas,
                    const StyledLine& line,
                    const double left,
                    const double availableWidth,
                    const double top,
                    const PdfTextAlignment alignment) {
    double x = left;
    if (alignment == PdfTextAlignment::Center) x += std::max(0.0, (availableWidth - line.width) * 0.5);
    else if (alignment == PdfTextAlignment::Right) x += std::max(0.0, availableWidth - line.width);
    const double baseline = top - line.maximumFontSize;
    for (const auto& fragment : line.fragments) {
        canvas.SetFillColor(fragment.style.color).BeginText();
        SelectRunFont(canvas, fragment.style);
        canvas.SetCharSpacing(fragment.style.characterSpacing)
              .SetTextRise(fragment.style.textRise)
              .MoveText(x, baseline);
        if (fragment.style.font) canvas.ShowTextUtf8(fragment.text);
        else canvas.ShowText(fragment.text);
        canvas.EndText();
        const double size = RunFontSize(fragment.style);
        if (fragment.style.underline || fragment.style.strikeThrough) {
            canvas.SaveState().SetStrokeColor(fragment.style.color)
                  .SetLineWidth(std::max(0.35, size / 18.0));
            if (fragment.style.underline) {
                canvas.DrawLine(x, baseline - size * 0.12,
                                x + fragment.width, baseline - size * 0.12);
            }
            if (fragment.style.strikeThrough) {
                canvas.DrawLine(x, baseline + size * 0.30,
                                x + fragment.width, baseline + size * 0.30);
            }
            canvas.RestoreState();
        }
        x += fragment.width;
    }
}

[[nodiscard]] std::string NumberLabel(const PdfDocumentLayout::ListStyle style,
                                      const std::size_t oneBasedIndex) {
    const auto alphabetic = [](std::size_t value, const bool upper) {
        std::string out;
        while (value > 0U) {
            const char c = static_cast<char>((value - 1U) % 26U) + (upper ? 'A' : 'a');
            out.insert(out.begin(), c);
            value = (value - 1U) / 26U;
        }
        return out;
    };
    const auto roman = [](std::size_t value, const bool upper) {
        struct Item { std::size_t value; const char* numeral; };
        static constexpr Item items[] = {
            {1000U,"M"},{900U,"CM"},{500U,"D"},{400U,"CD"},{100U,"C"},{90U,"XC"},
            {50U,"L"},{40U,"XL"},{10U,"X"},{9U,"IX"},{5U,"V"},{4U,"IV"},{1U,"I"}};
        std::string result;
        for (const auto& item : items) {
            while (value >= item.value) {
                result += item.numeral;
                value -= item.value;
            }
        }
        if (!upper) result = PdfTextLayout::ToLower(result);
        return result;
    };
    switch (style) {
    case PdfDocumentLayout::ListStyle::Bullet: return "\xE2\x80\xA2";
    case PdfDocumentLayout::ListStyle::Decimal: return std::to_string(oneBasedIndex) + ".";
    case PdfDocumentLayout::ListStyle::LowerAlpha: return alphabetic(oneBasedIndex, false) + ".";
    case PdfDocumentLayout::ListStyle::UpperAlpha: return alphabetic(oneBasedIndex, true) + ".";
    case PdfDocumentLayout::ListStyle::LowerRoman: return roman(oneBasedIndex, false) + ".";
    case PdfDocumentLayout::ListStyle::UpperRoman: return roman(oneBasedIndex, true) + ".";
    }
    return {};
}

[[nodiscard]] std::string ReplaceAll(std::string text,
                                     const std::string_view token,
                                     const std::string_view replacement) {
    std::size_t offset = 0U;
    while ((offset = text.find(token, offset)) != std::string::npos) {
        text.replace(offset, token.size(), replacement);
        offset += replacement.size();
    }
    return text;
}

[[nodiscard]] std::string ResolvePageFields(std::string text,
                                            const std::size_t page,
                                            const std::size_t totalPages) {
    text = ReplaceAll(std::move(text), "/PAGE", std::to_string(page + 1U));
    return ReplaceAll(std::move(text), "/NPAGE", std::to_string(totalPages));
}

[[nodiscard]] double MeasureHeaderFooterText(const std::string_view text,
                                             const double fontSize,
                                             const std::shared_ptr<const PdfTrueTypeFont>& font) {
    if (font) return font->MeasureTextUtf8(text, fontSize);
    return static_cast<double>(PdfTextLayout::CountCodePoints(text)) * fontSize * 0.5;
}

void DrawHeaderFooterText(PdfCanvas canvas,
                          const std::string& text,
                          const double x,
                          const double y,
                          const double fontSize,
                          const std::shared_ptr<const PdfTrueTypeFont>& font) {
    if (text.empty()) return;
    canvas.BeginText();
    if (font) canvas.SetTrueTypeFontAndSize(*font, fontSize);
    else canvas.SetFontAndSize("Helvetica", fontSize);
    canvas.MoveText(x, y);
    if (font) canvas.ShowTextUtf8(text);
    else canvas.ShowText(text);
    canvas.EndText();
}

} // namespace

const PdfTrueTypeFont* PdfDocumentLayout::FontFor(const ParagraphOptions& options) const {
    if (options.font) return options.font.get();
    return defaultFont_ ? defaultFont_.get() : nullptr;
}

std::vector<std::string> PdfDocumentLayout::SplitLines(
    const std::string& text, const double width, const ParagraphOptions& options) const {
    const auto* font = FontFor(options);
    TextRun run;
    run.text = text;
    run.fontSize = options.fontSize;
    run.base14Font = options.bold ? "Helvetica-Bold" : "Helvetica";
    if (font != nullptr) run.font = std::make_shared<const PdfTrueTypeFont>(*font);
    const auto styled = BuildStyledLines({run}, width, width, options.lineSpacing, options.wrap);
    std::vector<std::string> lines;
    lines.reserve(styled.size());
    for (const auto& line : styled) {
        std::string value;
        for (const auto& fragment : line.fragments) value += fragment.text;
        lines.push_back(std::move(value));
    }
    return lines;
}

PdfDocumentLayout::LayoutResult PdfDocumentLayout::FlowParagraphs(
    const std::vector<ParagraphOptions>& paragraphs,
    const PdfRectangle& flowBox,
    const std::size_t startPage,
    std::size_t* currentPage) {
    std::vector<RichParagraphOptions> rich;
    rich.reserve(paragraphs.size());
    for (const auto& paragraph : paragraphs) {
        RichParagraphOptions item;
        item.alignment = paragraph.alignment;
        item.firstLineIndent = paragraph.firstLineIndent;
        item.leftIndent = paragraph.leftIndent;
        item.rightIndent = paragraph.rightIndent;
        item.spaceBefore = paragraph.spaceBefore;
        item.spaceAfter = paragraph.spaceAfter;
        item.lineSpacing = paragraph.lineSpacing;
        item.wrap = paragraph.wrap;
        TextRun run;
        run.text = paragraph.text;
        run.fontSize = paragraph.fontSize;
        run.base14Font = paragraph.bold ? "Helvetica-Bold" : "Helvetica";
        run.font = paragraph.font ? paragraph.font : defaultFont_;
        item.runs.push_back(std::move(run));
        rich.push_back(std::move(item));
    }
    auto result = FlowRichParagraphs(rich, flowBox, startPage, flowBox.top);
    if (currentPage) *currentPage = result.currentPage;
    return result;
}

PdfDocumentLayout::LayoutResult PdfDocumentLayout::FlowRichParagraphs(
    const std::vector<RichParagraphOptions>& paragraphs,
    const PdfRectangle& flowBox,
    const std::size_t startPage,
    const double startY) {
    if (flowBox.empty()) throw std::invalid_argument("FlowRichParagraphs requires a non-empty flow box.");
    if (writer_.GetPageCount() == 0U) (void)writer_.AddPage();
    if (startPage >= writer_.GetPageCount()) {
        while (writer_.GetPageCount() <= startPage) (void)writer_.AddPage();
    }
    const auto templateBox = writer_.GetPageMediaBox(startPage);
    std::size_t page = startPage;
    double y = startY > flowBox.bottom && startY <= flowBox.top ? startY : flowBox.top;
    const auto nextPage = [&] {
        if (page + 1U >= writer_.GetPageCount()) (void)writer_.AddPage(templateBox);
        ++page;
        y = flowBox.top;
    };

    LayoutResult result;
    for (const auto& paragraph : paragraphs) {
        y -= std::max(0.0, paragraph.spaceBefore);
        const double normalLeft = flowBox.left + paragraph.leftIndent;
        const double firstLeft = normalLeft + paragraph.firstLineIndent;
        const double normalWidth = flowBox.width() - paragraph.leftIndent - paragraph.rightIndent;
        const double firstWidth = normalWidth - paragraph.firstLineIndent;
        if (normalWidth <= 0.0 || firstWidth <= 0.0) {
            throw std::invalid_argument("Paragraph indents leave no usable width.");
        }
        auto lines = BuildStyledLines(paragraph.runs, firstWidth, normalWidth,
                                      paragraph.lineSpacing, paragraph.wrap);
        const double totalHeight = std::accumulate(lines.begin(), lines.end(), 0.0,
            [](const double sum, const StyledLine& line) { return sum + line.height; });
        const double pageHeight = flowBox.height();
        if ((paragraph.keepTogether || lines.size() <= paragraph.orphanLines + paragraph.widowLines) &&
            totalHeight <= pageHeight && y - totalHeight < flowBox.bottom && y < flowBox.top) {
            nextPage();
        }

        std::size_t lineIndex = 0U;
        while (lineIndex < lines.size()) {
            std::size_t fitting = 0U;
            double fittingHeight = 0.0;
            for (std::size_t scan = lineIndex; scan < lines.size(); ++scan) {
                if (y - fittingHeight - lines[scan].height < flowBox.bottom) break;
                fittingHeight += lines[scan].height;
                ++fitting;
            }
            const auto remaining = lines.size() - lineIndex;
            if (fitting == 0U && y < flowBox.top) {
                nextPage();
                continue;
            }
            if (lineIndex == 0U && fitting < std::min(paragraph.orphanLines, remaining) && y < flowBox.top) {
                nextPage();
                continue;
            }
            if (fitting < remaining && remaining - fitting < paragraph.widowLines &&
                fitting > paragraph.orphanLines) {
                fitting -= paragraph.widowLines - (remaining - fitting);
            }
            if (fitting == 0U) fitting = 1U; // oversize line: make progress.

            for (std::size_t count = 0U; count < fitting && lineIndex < lines.size(); ++count, ++lineIndex) {
                const bool firstLine = lineIndex == 0U;
                const double left = firstLine ? firstLeft : normalLeft;
                const double width = firstLine ? firstWidth : normalWidth;
                DrawStyledLine(writer_.GetCanvas(page), lines[lineIndex], left, width,
                               y, paragraph.alignment);
                y -= lines[lineIndex].height;
            }
            if (lineIndex < lines.size()) nextPage();
        }
        y -= std::max(0.0, paragraph.spaceAfter);
        ++result.paragraphsFlowed;
    }
    result.currentPage = page;
    result.currentY = y;
    result.pagesWritten = page - startPage + 1U;
    return result;
}

double PdfDocumentLayout::DrawList(
    const std::size_t pageIndex,
    const std::vector<std::string>& items,
    const double x, const double y,
    const ListOptions& options) {
    double cursor = y;
    auto canvas = writer_.GetCanvas(pageIndex);
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const std::string label = NumberLabel(options.style, index + 1U);
        canvas.BeginText().SetFontAndSize(options.bold ? "Helvetica-Bold" : "Helvetica", options.fontSize)
            .MoveText(x, cursor).ShowText(label)
            .MoveText(options.indent, 0.0).ShowText(items[index]).EndText();
        cursor -= options.fontSize * options.lineSpacing + options.spacing;
    }
    return cursor;
}

PdfDocumentLayout::LayoutResult PdfDocumentLayout::FlowNestedList(
    const std::vector<NestedListItem>& items,
    const PdfRectangle& flowBox,
    const FlowListOptions& options,
    const std::size_t startPage,
    const double startY) {
    std::vector<RichParagraphOptions> paragraphs;
    std::function<void(const std::vector<NestedListItem>&, std::size_t)> appendLevel;
    appendLevel = [&](const std::vector<NestedListItem>& levelItems, const std::size_t level) {
        const auto style = options.levelStyles.empty()
            ? ListStyle::Bullet : options.levelStyles[level % options.levelStyles.size()];
        for (std::size_t index = 0U; index < levelItems.size(); ++index) {
            RichParagraphOptions paragraph;
            paragraph.leftIndent = static_cast<double>(level) * options.levelIndent + options.markerWidth;
            paragraph.firstLineIndent = -options.markerWidth;
            paragraph.spaceAfter = options.itemSpacing;
            paragraph.lineSpacing = options.lineSpacing;
            TextRun marker;
            marker.text = NumberLabel(style, options.startNumber + index) + " ";
            marker.fontSize = options.fontSize;
            marker.font = options.font ? options.font : defaultFont_;
            paragraph.runs.push_back(std::move(marker));
            if (levelItems[index].runs.empty()) {
                TextRun empty;
                empty.fontSize = options.fontSize;
                empty.font = options.font ? options.font : defaultFont_;
                paragraph.runs.push_back(std::move(empty));
            } else {
                for (auto run : levelItems[index].runs) {
                    if (run.fontSize <= 0.0) run.fontSize = options.fontSize;
                    if (!run.font) run.font = options.font ? options.font : defaultFont_;
                    paragraph.runs.push_back(std::move(run));
                }
            }
            paragraphs.push_back(std::move(paragraph));
            if (!levelItems[index].children.empty()) appendLevel(levelItems[index].children, level + 1U);
        }
    };
    appendLevel(items, 0U);
    return FlowRichParagraphs(paragraphs, flowBox, startPage,
                              startY > 0.0 ? startY : flowBox.top);
}

PdfDocumentLayout::TableLayoutResult PdfDocumentLayout::FlowTable(
    const std::vector<TableRow>& rows,
    const PdfRectangle& flowBox,
    const FlowTableOptions& options,
    const std::size_t startPage,
    const double startY) {
    TableLayoutResult result;
    if (rows.empty()) {
        result.currentPage = startPage;
        result.currentY = startY > 0.0 ? startY : flowBox.top;
        return result;
    }
    if (flowBox.empty()) throw std::invalid_argument("FlowTable requires a non-empty flow box.");
    if (writer_.GetPageCount() == 0U) (void)writer_.AddPage();
    if (startPage >= writer_.GetPageCount()) {
        while (writer_.GetPageCount() <= startPage) (void)writer_.AddPage();
    }

    std::size_t columnCount = options.columnCount;
    if (columnCount == 0U) {
        for (const auto& row : rows) {
            std::size_t count = 0U;
            for (const auto& cell : row.cells) count += std::max<std::size_t>(1U, cell.columnSpan);
            columnCount = std::max(columnCount, count);
        }
    }
    if (columnCount == 0U) throw std::invalid_argument("FlowTable could not infer a column count.");

    struct Placement final {
        std::size_t row{};
        std::size_t column{};
        std::size_t rowSpan{1U};
        std::size_t columnSpan{1U};
        const TableCell* cell{};
        std::vector<StyledLine> lines;
    };
    std::vector<std::vector<bool>> occupied(rows.size(), std::vector<bool>(columnCount, false));
    std::vector<Placement> placements;
    for (std::size_t rowIndex = 0U; rowIndex < rows.size(); ++rowIndex) {
        std::size_t column = 0U;
        for (const auto& cell : rows[rowIndex].cells) {
            while (column < columnCount && occupied[rowIndex][column]) ++column;
            const auto columnSpan = std::max<std::size_t>(1U, cell.columnSpan);
            const auto rowSpan = std::max<std::size_t>(1U, cell.rowSpan);
            if (column + columnSpan > columnCount || rowIndex + rowSpan > rows.size()) {
                throw std::invalid_argument("Table cell span exceeds the table grid.");
            }
            for (std::size_t r = rowIndex; r < rowIndex + rowSpan; ++r) {
                for (std::size_t c = column; c < column + columnSpan; ++c) {
                    if (occupied[r][c]) throw std::invalid_argument("Overlapping table cell spans.");
                    occupied[r][c] = true;
                }
            }
            placements.push_back(Placement{rowIndex, column, rowSpan, columnSpan, &cell, {}});
            column += columnSpan;
        }
    }

    std::vector<double> widths(columnCount, flowBox.width() / static_cast<double>(columnCount));
    if (options.columnWidths.size() == columnCount) {
        const double total = std::accumulate(options.columnWidths.begin(), options.columnWidths.end(), 0.0);
        if (total <= 0.0) throw std::invalid_argument("Table column widths must have a positive sum.");
        for (std::size_t column = 0U; column < columnCount; ++column) {
            widths[column] = flowBox.width() * options.columnWidths[column] / total;
        }
    } else if (options.autoWidth) {
        std::vector<double> minimum(columnCount, 18.0);
        std::vector<double> preferred(columnCount, 30.0);
        for (const auto& placement : placements) {
            if (placement.columnSpan != 1U) continue;
            double totalWidth = 0.0;
            double longestWord = 0.0;
            for (const auto& sourceRun : placement.cell->runs) {
                auto run = sourceRun;
                if (run.fontSize <= 0.0) run.fontSize = options.fontSize;
                if (!run.font) run.font = options.font ? options.font : defaultFont_;
                totalWidth += MeasureRun(run, run.text);
                std::string word;
                for (const auto& cluster : PdfTextLayout::GraphemeClusters(run.text)) {
                    if (PdfTextLayout::IsWhitespace(cluster)) {
                        longestWord = std::max(longestWord, MeasureRun(run, word));
                        word.clear();
                    } else word += cluster;
                }
                longestWord = std::max(longestWord, MeasureRun(run, word));
            }
            const double padding = std::max(0.0, placement.cell->padding) * 2.0;
            minimum[placement.column] = std::max(minimum[placement.column], longestWord + padding);
            preferred[placement.column] = std::max(preferred[placement.column], totalWidth + padding);
        }
        const double minimumTotal = std::accumulate(minimum.begin(), minimum.end(), 0.0);
        const double preferredTotal = std::accumulate(preferred.begin(), preferred.end(), 0.0);
        if (flowBox.width() >= preferredTotal) {
            widths = preferred;
            const double extra = (flowBox.width() - preferredTotal) / static_cast<double>(columnCount);
            for (auto& width : widths) width += extra;
        } else if (flowBox.width() >= minimumTotal && preferredTotal > minimumTotal) {
            const double factor = (flowBox.width() - minimumTotal) / (preferredTotal - minimumTotal);
            for (std::size_t column = 0U; column < columnCount; ++column) {
                widths[column] = minimum[column] + (preferred[column] - minimum[column]) * factor;
            }
        } else {
            widths = minimum;
            const double factor = flowBox.width() / std::max(1.0, minimumTotal);
            for (auto& width : widths) width *= factor;
        }
    }

    std::vector<double> x(columnCount + 1U, flowBox.left);
    for (std::size_t column = 0U; column < columnCount; ++column) x[column + 1U] = x[column] + widths[column];
    x.back() = flowBox.right;

    std::vector<double> rowHeights(rows.size(), std::max(1.0, options.minimumRowHeight));
    struct SpanRequirement final { std::size_t row; std::size_t span; double height; };
    std::vector<SpanRequirement> spanRequirements;
    for (auto& placement : placements) {
        const double cellWidth = x[placement.column + placement.columnSpan] - x[placement.column];
        std::vector<TextRun> runs = placement.cell->runs;
        if (runs.empty()) runs.push_back(TextRun{});
        for (auto& run : runs) {
            if (run.fontSize <= 0.0) run.fontSize = options.fontSize;
            if (!run.font) run.font = options.font ? options.font : defaultFont_;
        }
        const double contentWidth = std::max(1.0, cellWidth - 2.0 * std::max(0.0, placement.cell->padding));
        placement.lines = BuildStyledLines(runs, contentWidth, contentWidth, options.lineSpacing, true);
        const double contentHeight = std::accumulate(placement.lines.begin(), placement.lines.end(), 0.0,
            [](const double sum, const StyledLine& line) { return sum + line.height; }) +
            2.0 * std::max(0.0, placement.cell->padding);
        if (placement.rowSpan == 1U) {
            rowHeights[placement.row] = std::max(rowHeights[placement.row], contentHeight);
        } else {
            spanRequirements.push_back({placement.row, placement.rowSpan, contentHeight});
        }
    }
    for (std::size_t row = 0U; row < rows.size(); ++row) {
        rowHeights[row] = std::max(rowHeights[row], rows[row].minimumHeight);
    }
    for (const auto& requirement : spanRequirements) {
        const double current = std::accumulate(rowHeights.begin() + static_cast<std::ptrdiff_t>(requirement.row),
            rowHeights.begin() + static_cast<std::ptrdiff_t>(requirement.row + requirement.span), 0.0);
        if (current < requirement.height) {
            const double extra = (requirement.height - current) / static_cast<double>(requirement.span);
            for (std::size_t row = requirement.row; row < requirement.row + requirement.span; ++row) {
                rowHeights[row] += extra;
            }
        }
    }

    std::size_t headerRows = 0U;
    while (headerRows < rows.size() && rows[headerRows].header) ++headerRows;
    const double headerHeight = std::accumulate(rowHeights.begin(),
        rowHeights.begin() + static_cast<std::ptrdiff_t>(headerRows), 0.0);

    const auto drawRange = [&](const std::size_t pageIndex,
                               const std::size_t firstRow,
                               const std::size_t endRow,
                               const double top) {
        auto canvas = writer_.GetCanvas(pageIndex);
        for (const auto& placement : placements) {
            if (placement.row < firstRow || placement.row >= endRow) continue;
            const auto placementEnd = placement.row + placement.rowSpan;
            if (placementEnd > endRow) continue;
            const double cellTop = top - std::accumulate(
                rowHeights.begin() + static_cast<std::ptrdiff_t>(firstRow),
                rowHeights.begin() + static_cast<std::ptrdiff_t>(placement.row), 0.0);
            const double cellHeight = std::accumulate(
                rowHeights.begin() + static_cast<std::ptrdiff_t>(placement.row),
                rowHeights.begin() + static_cast<std::ptrdiff_t>(placementEnd), 0.0);
            const double left = x[placement.column];
            const double right = x[placement.column + placement.columnSpan];
            const double bottom = cellTop - cellHeight;
            if (placement.cell->backgroundColor) {
                canvas.SaveState().SetFillColor(*placement.cell->backgroundColor)
                      .FillRectangle(left, bottom, right - left, cellHeight).RestoreState();
            }
            if (options.drawGrid) {
                canvas.SaveState().SetStrokeColor(options.borderColor)
                      .SetLineWidth(std::max(0.1, options.borderWidth))
                      .Rectangle(left, bottom, right - left, cellHeight).Stroke().RestoreState();
            }
            const double padding = std::max(0.0, placement.cell->padding);
            const double contentHeight = std::accumulate(placement.lines.begin(), placement.lines.end(), 0.0,
                [](const double sum, const StyledLine& line) { return sum + line.height; });
            double textTop = cellTop - padding;
            if (placement.cell->verticalAlignment == VerticalAlignment::Middle) {
                textTop = cellTop - std::max(0.0, (cellHeight - contentHeight) * 0.5);
            } else if (placement.cell->verticalAlignment == VerticalAlignment::Bottom) {
                textTop = bottom + padding + contentHeight;
            }
            const double contentWidth = std::max(1.0, right - left - 2.0 * padding);
            for (const auto& line : placement.lines) {
                DrawStyledLine(canvas, line, left + padding, contentWidth,
                               textTop, placement.cell->alignment);
                textTop -= line.height;
            }
        }
    };

    const auto templateBox = writer_.GetPageMediaBox(startPage);
    std::size_t page = startPage;
    double y = startY > flowBox.bottom && startY <= flowBox.top ? startY : flowBox.top;
    const auto newPage = [&] {
        if (page + 1U >= writer_.GetPageCount()) (void)writer_.AddPage(templateBox);
        ++page;
        y = flowBox.top;
        if (options.repeatHeaderRows && headerRows > 0U) {
            drawRange(page, 0U, headerRows, y);
            y -= headerHeight;
        }
    };

    std::size_t row = 0U;
    while (row < rows.size()) {
        std::size_t groupEnd = row + 1U;
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (const auto& placement : placements) {
                if (placement.row >= row && placement.row < groupEnd &&
                    placement.row + placement.rowSpan > groupEnd) {
                    groupEnd = placement.row + placement.rowSpan;
                    expanded = true;
                }
            }
            if (groupEnd < rows.size() && rows[groupEnd - 1U].keepWithNext) {
                ++groupEnd;
                expanded = true;
            }
        }
        const double groupHeight = std::accumulate(
            rowHeights.begin() + static_cast<std::ptrdiff_t>(row),
            rowHeights.begin() + static_cast<std::ptrdiff_t>(groupEnd), 0.0);
        if (y - groupHeight < flowBox.bottom && y < flowBox.top) {
            newPage();
        }
        drawRange(page, row, groupEnd, y);
        y -= groupHeight;
        result.rowsWritten += groupEnd - row;
        row = groupEnd;
    }
    result.currentPage = page;
    result.currentY = y;
    result.pagesWritten = page - startPage + 1U;
    return result;
}

std::size_t PdfDocumentLayout::DrawColumns(
    const std::size_t pageIndex,
    const std::vector<std::string>& columns,
    const PdfRectangle& box,
    const double gap,
    const ParagraphOptions& options) {
    if (columns.empty()) return 0U;
    const std::size_t count = columns.size();
    const double columnWidth = (box.width() - gap * static_cast<double>(count - 1U)) /
                               static_cast<double>(count);
    if (columnWidth <= 0.0) throw std::invalid_argument("Column gap leaves no usable width.");
    for (std::size_t index = 0U; index < count; ++index) {
        const double left = box.left + static_cast<double>(index) * (columnWidth + gap);
        ParagraphOptions paragraph = options;
        paragraph.text = columns[index];
        const auto* font = FontFor(paragraph);
        TextRun run;
        run.text = columns[index];
        run.fontSize = paragraph.fontSize;
        run.base14Font = paragraph.bold ? "Helvetica-Bold" : "Helvetica";
        if (font) run.font = std::make_shared<const PdfTrueTypeFont>(*font);
        const auto lines = BuildStyledLines({run}, columnWidth, columnWidth,
                                            paragraph.lineSpacing, true);
        double y = box.top;
        for (const auto& line : lines) {
            if (y - line.height < box.bottom) break;
            DrawStyledLine(writer_.GetCanvas(pageIndex), line, left, columnWidth,
                           y, paragraph.alignment);
            y -= line.height;
        }
    }
    return count;
}

std::size_t PdfDocumentLayout::DrawHeader(
    const std::size_t startPage, const std::size_t endPage,
    const PdfRectangle& pageBox,
    const HeaderFooterOptions& options) {
    if (endPage < startPage) return 0U;
    const std::size_t totalPages = writer_.GetPageCount();
    std::size_t drawn = 0U;
    for (std::size_t page = startPage; page <= endPage && page < totalPages; ++page) {
        std::string left = ResolvePageFields(options.leftText, page, totalPages);
        std::string center = ResolvePageFields(options.centerText, page, totalPages);
        std::string right = ResolvePageFields(options.rightText, page, totalPages);
        if (options.pageNumber && left.empty() && center.empty() && right.empty()) {
            if (options.pageNumberCenter) center = std::to_string(page + 1U);
            else right = std::to_string(page + 1U);
        }
        const double y = pageBox.top - options.topMargin + options.fontSize;
        DrawHeaderFooterText(writer_.GetCanvas(page), left, pageBox.left, y,
                             options.fontSize, options.font);
        const double centerWidth = MeasureHeaderFooterText(center, options.fontSize, options.font);
        DrawHeaderFooterText(writer_.GetCanvas(page), center,
            pageBox.left + (pageBox.width() - centerWidth) * 0.5, y,
            options.fontSize, options.font);
        const double rightWidth = MeasureHeaderFooterText(right, options.fontSize, options.font);
        DrawHeaderFooterText(writer_.GetCanvas(page), right, pageBox.right - rightWidth, y,
                             options.fontSize, options.font);
        ++drawn;
    }
    return drawn;
}

std::size_t PdfDocumentLayout::DrawFooter(
    const std::size_t startPage, const std::size_t endPage,
    const PdfRectangle& pageBox,
    const HeaderFooterOptions& options) {
    if (endPage < startPage) return 0U;
    const std::size_t totalPages = writer_.GetPageCount();
    std::size_t drawn = 0U;
    for (std::size_t page = startPage; page <= endPage && page < totalPages; ++page) {
        std::string left = ResolvePageFields(options.leftText, page, totalPages);
        std::string center = ResolvePageFields(options.centerText, page, totalPages);
        std::string right = ResolvePageFields(options.rightText, page, totalPages);
        if (options.pageNumber && left.empty() && center.empty() && right.empty()) {
            if (options.pageNumberCenter) center = std::to_string(page + 1U);
            else right = std::to_string(page + 1U);
        }
        const double y = pageBox.bottom + options.bottomMargin;
        DrawHeaderFooterText(writer_.GetCanvas(page), left, pageBox.left, y,
                             options.fontSize, options.font);
        const double centerWidth = MeasureHeaderFooterText(center, options.fontSize, options.font);
        DrawHeaderFooterText(writer_.GetCanvas(page), center,
            pageBox.left + (pageBox.width() - centerWidth) * 0.5, y,
            options.fontSize, options.font);
        const double rightWidth = MeasureHeaderFooterText(right, options.fontSize, options.font);
        DrawHeaderFooterText(writer_.GetCanvas(page), right, pageBox.right - rightWidth, y,
                             options.fontSize, options.font);
        ++drawn;
    }
    return drawn;
}

double PdfDocumentLayout::DrawTable(
    const std::size_t pageIndex,
    const std::vector<std::string>& headers,
    const std::vector<std::vector<std::string>>& rows,
    const PdfRectangle& box,
    const std::vector<double>& columnWidths,
    const TableOptions& options) {
    if (headers.empty()) return box.top;
    std::vector<TableRow> advancedRows;
    TableRow header;
    header.header = true;
    header.minimumHeight = options.headerRowHeight;
    for (const auto& value : headers) {
        TableCell cell;
        TextRun run;
        run.text = value;
        run.fontSize = options.fontSize;
        run.base14Font = options.boldHeader ? "Helvetica-Bold" : "Helvetica";
        cell.runs.push_back(std::move(run));
        cell.padding = options.padding;
        header.cells.push_back(std::move(cell));
    }
    advancedRows.push_back(std::move(header));
    for (const auto& sourceRow : rows) {
        TableRow row;
        row.minimumHeight = options.rowHeight;
        for (const auto& value : sourceRow) {
            TableCell cell;
            TextRun run;
            run.text = value;
            run.fontSize = options.fontSize;
            cell.runs.push_back(std::move(run));
            cell.padding = options.padding;
            row.cells.push_back(std::move(cell));
        }
        advancedRows.push_back(std::move(row));
    }
    FlowTableOptions flowOptions;
    flowOptions.columnCount = headers.size();
    flowOptions.columnWidths = columnWidths;
    flowOptions.fontSize = options.fontSize;
    flowOptions.minimumRowHeight = options.rowHeight;
    flowOptions.drawGrid = options.drawGrid;
    flowOptions.repeatHeaderRows = false;
    const auto result = FlowTable(advancedRows, box, flowOptions, pageIndex, box.top);
    return result.currentPage == pageIndex ? result.currentY : box.bottom;
}

PdfDocumentLayout::LayoutResult PdfDocumentLayout::FlowImage(
    const PdfImage& image,
    const PdfRectangle& flowBox,
    const ImageLayoutOptions& options,
    const std::size_t startPage,
    const double startY) {
    if (flowBox.empty()) throw std::invalid_argument("FlowImage requires a non-empty flow box.");
    if (image.GetWidth() == 0U || image.GetHeight() == 0U) {
        throw std::invalid_argument("FlowImage requires a non-empty image.");
    }
    if (writer_.GetPageCount() == 0U) (void)writer_.AddPage();
    if (startPage >= writer_.GetPageCount()) {
        while (writer_.GetPageCount() <= startPage) (void)writer_.AddPage();
    }
    const auto templateBox = writer_.GetPageMediaBox(startPage);
    std::size_t page = startPage;
    double y = startY > flowBox.bottom && startY <= flowBox.top ? startY : flowBox.top;
    y -= std::max(0.0, options.spaceBefore);

    const double naturalWidth = static_cast<double>(image.GetWidth());
    const double naturalHeight = static_cast<double>(image.GetHeight());
    double targetWidth = options.requestedWidth > 0.0 ? options.requestedWidth : std::min(naturalWidth, flowBox.width());
    double targetHeight = options.requestedHeight;
    if (targetHeight <= 0.0) targetHeight = targetWidth * naturalHeight / naturalWidth;
    if (options.requestedWidth <= 0.0 && options.requestedHeight > 0.0) {
        targetWidth = targetHeight * naturalWidth / naturalHeight;
    }
    targetWidth = std::min(targetWidth, flowBox.width());
    const double availablePageHeight = flowBox.height();
    if (targetHeight > availablePageHeight && options.fit == ImageFitMode::Contain) {
        const double scale = availablePageHeight / targetHeight;
        targetHeight *= scale;
        targetWidth *= scale;
    }
    if (options.keepTogether && y - targetHeight < flowBox.bottom && y < flowBox.top) {
        if (page + 1U >= writer_.GetPageCount()) (void)writer_.AddPage(templateBox);
        ++page;
        y = flowBox.top;
    }

    double left = flowBox.left;
    if (options.alignment == PdfTextAlignment::Center) left += (flowBox.width() - targetWidth) * 0.5;
    else if (options.alignment == PdfTextAlignment::Right) left += flowBox.width() - targetWidth;
    const PdfRectangle target{left, y - targetHeight, left + targetWidth, y};
    auto canvas = writer_.GetCanvas(page);
    if (options.fit != ImageFitMode::Cover || options.requestedWidth <= 0.0 || options.requestedHeight <= 0.0) {
        canvas.DrawImage(image, target);
    } else {
        const double coverScale = std::max(targetWidth / naturalWidth, targetHeight / naturalHeight);
        const double drawWidth = naturalWidth * coverScale;
        const double drawHeight = naturalHeight * coverScale;
        const PdfRectangle drawRect{
            left + (targetWidth - drawWidth) * 0.5,
            target.bottom + (targetHeight - drawHeight) * 0.5,
            left + (targetWidth + drawWidth) * 0.5,
            target.bottom + (targetHeight + drawHeight) * 0.5};
        canvas.SaveState().Rectangle(target.left, target.bottom, target.width(), target.height())
              .Clip().EndPath().DrawImage(image, drawRect).RestoreState();
    }
    y -= targetHeight + std::max(0.0, options.spaceAfter);
    LayoutResult result;
    result.pagesWritten = page - startPage + 1U;
    result.paragraphsFlowed = 1U;
    result.currentPage = page;
    result.currentY = y;
    return result;
}

std::size_t PdfDocumentLayout::AddAreaBreak(
    const std::size_t currentPage,
    PdfRectangle mediaBox) {
    if (writer_.GetPageCount() == 0U) return writer_.AddPage(mediaBox.empty() ? PdfRectangle{0,0,595,842} : mediaBox);
    if (mediaBox.empty()) {
        if (currentPage >= writer_.GetPageCount()) throw std::out_of_range("Area break page index is out of range.");
        mediaBox = writer_.GetPageMediaBox(currentPage);
    }
    return writer_.AddPage(mediaBox);
}

} // namespace CPPPdf

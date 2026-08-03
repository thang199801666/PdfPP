#include <CPPPdf/Document/PdfDocumentLayout.hpp>

#include <CPPPdf/Graphics/PdfCanvas.hpp>
#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

namespace CPPPdf {
namespace {

std::string numberLabel(const PdfDocumentLayout::ListStyle style, const std::size_t index) {
    const auto letter = [](const std::size_t i, const bool upper) {
        std::string out;
        std::size_t value = i + 1U;
        while (value > 0U) {
            const char c = static_cast<char>((value - 1U) % 26U) + (upper ? 'A' : 'a');
            out.insert(out.begin(), c);
            value = (value - 1U) / 26U;
        }
        return out;
    };
    switch (style) {
    case PdfDocumentLayout::ListStyle::Bullet: return "\xE2\x80\xA2"; // U+2022
    case PdfDocumentLayout::ListStyle::Decimal: return std::to_string(index + 1U) + ".";
    case PdfDocumentLayout::ListStyle::LowerAlpha: return letter(index, false) + ".";
    case PdfDocumentLayout::ListStyle::UpperAlpha: return letter(index, true) + ".";
    case PdfDocumentLayout::ListStyle::LowerRoman: {
        constexpr const char* romans[] = {"i","ii","iii","iv","v","vi","vii","viii","ix","x","xi","xii"};
        return index < 12U ? std::string(romans[index]) + "." : std::to_string(index + 1U) + ".";
    }
    case PdfDocumentLayout::ListStyle::UpperRoman: {
        constexpr const char* romans[] = {"I","II","III","IV","V","VI","VII","VIII","IX","X","XI","XII"};
        return index < 12U ? std::string(romans[index]) + "." : std::to_string(index + 1U) + ".";
    }
    }
    return "";
}

} // namespace

const PdfTrueTypeFont* PdfDocumentLayout::FontFor(const ParagraphOptions& options) const {
    if (options.font) return options.font.get();
    return defaultFont_ ? defaultFont_.get() : nullptr;
}

std::vector<std::string> PdfDocumentLayout::SplitLines(
    const std::string& text, const double width, const ParagraphOptions& options) const {
    std::vector<std::string> lines;
    const auto* font = FontFor(options);
    std::size_t paragraphStart = 0;
    while (paragraphStart <= text.size()) {
        const auto newline = text.find('\n', paragraphStart);
        const auto paragraph = text.substr(paragraphStart,
            newline == std::string::npos ? text.size() - paragraphStart : newline - paragraphStart);
        if (!options.wrap || paragraph.empty() || font == nullptr) {
            lines.push_back(paragraph);
        } else {
            std::string current;
            std::size_t pos = 0;
            while (pos < paragraph.size()) {
                while (pos < paragraph.size() && paragraph[pos] == ' ') ++pos;
                if (pos >= paragraph.size()) break;
                const auto end = paragraph.find(' ', pos);
                const auto word = paragraph.substr(pos,
                    end == std::string::npos ? paragraph.size() - pos : end - pos);
                std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
                if (!current.empty() && font->MeasureTextUtf8(candidate, options.fontSize) > width) {
                    lines.push_back(current);
                    current.assign(word);
                } else {
                    current = std::move(candidate);
                }
                if (font->MeasureTextUtf8(current, options.fontSize) > width) {
                    // A single word exceeds the width: hard-break it.
                    lines.push_back(current);
                    current.clear();
                }
                if (end == std::string::npos) break;
                pos = end + 1;
            }
            if (!current.empty()) lines.push_back(current);
        }
        if (newline == std::string::npos) break;
        paragraphStart = newline + 1;
    }
    return lines;
}

PdfDocumentLayout::LayoutResult PdfDocumentLayout::FlowParagraphs(
    const std::vector<ParagraphOptions>& paragraphs,
    const PdfRectangle& flowBox,
    const std::size_t startPage,
    std::size_t* currentPage) {
    LayoutResult result;
    std::size_t page = startPage;
    double y = flowBox.top;
    for (const auto& paragraph : paragraphs) {
        if (paragraph.spaceBefore > 0.0) y -= paragraph.spaceBefore;
        const auto* font = FontFor(paragraph);
        const double usableWidth = flowBox.width() - paragraph.leftIndent - paragraph.rightIndent;
        if (font == nullptr) {
            // Base-14 fallback: draw with Helvetica, line height approximation.
            const double lineHeight = paragraph.fontSize * paragraph.lineSpacing;
            const std::size_t lineCount = std::max<std::size_t>(
                1U, static_cast<std::size_t>(std::ceil(paragraph.text.size() * paragraph.fontSize * 0.5 / std::max(usableWidth, 1.0))));
            y -= lineCount * lineHeight;
        } else {
            const auto lines = SplitLines(paragraph.text, usableWidth, paragraph);
            const double lineHeight = font->GetLineHeight(paragraph.fontSize, paragraph.lineSpacing);
            y -= lines.size() * lineHeight;
        }
        if (paragraph.spaceAfter > 0.0) y -= paragraph.spaceAfter;
        ++result.paragraphsFlowed;
        if (y < flowBox.bottom && page + 1U >= writer_.GetPageCount()) {
            writer_.AddPage();
            ++page;
            y = flowBox.top;
        }
    }
    if (currentPage) *currentPage = page;
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
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::string label = numberLabel(options.style, i);
        canvas.BeginText().SetFontAndSize("Helvetica", options.fontSize)
            .MoveText(x, cursor).ShowText(label)
            .MoveText(options.indent, 0.0).ShowText(items[i])
            .EndText();
        cursor -= options.fontSize * options.lineSpacing + options.spacing;
    }
    return cursor;
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
    for (std::size_t i = 0; i < count; ++i) {
        const double left = box.left + static_cast<double>(i) * (columnWidth + gap);
        PdfTextLayoutOptions layoutOptions;
        layoutOptions.box = PdfRectangle{left, box.bottom, left + columnWidth, box.top};
        layoutOptions.fontSize = options.fontSize;
        layoutOptions.lineSpacing = options.lineSpacing;
        layoutOptions.alignment = options.alignment;
        layoutOptions.wrap = true;
        const auto* font = FontFor(options);
        if (font != nullptr) {
            writer_.GetCanvas(pageIndex).DrawTextUtf8(*font, columns[i], layoutOptions);
        } else {
            double cursor = box.top - options.fontSize;
            const auto lines = SplitLines(columns[i], columnWidth, options);
            for (const auto& line : lines) {
                writer_.GetCanvas(pageIndex).BeginText().SetFontAndSize("Helvetica", options.fontSize)
                    .MoveText(left, cursor).ShowText(line).EndText();
                cursor -= options.fontSize * options.lineSpacing;
            }
        }
    }
    return count;
}

std::size_t PdfDocumentLayout::DrawHeader(
    const std::size_t startPage, const std::size_t endPage,
    const PdfRectangle& pageBox,
    const HeaderFooterOptions& options) {
    std::size_t drawn = 0U;
    for (std::size_t page = startPage; page <= endPage; ++page) {
        auto canvas = writer_.GetCanvas(page);
        const double y = pageBox.top - options.topMargin + options.fontSize;
        std::string text = options.pageNumber ? std::to_string(page + 1U) : options.centerText;
        if (text.empty()) text = options.leftText;
        canvas.BeginText();
        if (options.font) canvas.SetTrueTypeFontAndSize(*options.font, options.fontSize);
        else canvas.SetFontAndSize("Helvetica", options.fontSize);
        canvas.MoveText(pageBox.left, y).ShowText(text).EndText();
        if (!options.rightText.empty()) {
            canvas.BeginText();
            if (options.font) canvas.SetTrueTypeFontAndSize(*options.font, options.fontSize);
            else canvas.SetFontAndSize("Helvetica", options.fontSize);
            canvas.MoveText(pageBox.right - options.rightText.size() * options.fontSize * 0.5, y)
                 .ShowText(options.rightText).EndText();
        }
        ++drawn;
    }
    return drawn;
}

std::size_t PdfDocumentLayout::DrawFooter(
    const std::size_t startPage, const std::size_t endPage,
    const PdfRectangle& pageBox,
    const HeaderFooterOptions& options) {
    std::size_t drawn = 0U;
    for (std::size_t page = startPage; page <= endPage; ++page) {
        auto canvas = writer_.GetCanvas(page);
        const double y = pageBox.bottom + options.bottomMargin;
        const std::string text = options.pageNumber
            ? std::to_string(page + 1U)
            : (options.centerText.empty() ? options.leftText : options.centerText);
        canvas.BeginText();
        if (options.font) canvas.SetTrueTypeFontAndSize(*options.font, options.fontSize);
        else canvas.SetFontAndSize("Helvetica", options.fontSize);
        const double textWidth = text.size() * options.fontSize * 0.5;
        canvas.MoveText(pageBox.left + (pageBox.width() - textWidth) * 0.5, y)
             .ShowText(text).EndText();
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
    const std::size_t columns = headers.size();
    if (columns == 0U) return box.top;
    // Normalize relative column widths.
    std::vector<double> widths(columns, 1.0);
    if (columnWidths.size() == columns) {
        double total = 0.0;
        for (const double w : columnWidths) total += w;
        if (total > 0.0) {
            for (std::size_t c = 0; c < columns; ++c) widths[c] = columnWidths[c] / total * box.width();
        }
    } else {
        const double colWidth = box.width() / static_cast<double>(columns);
        for (auto& w : widths) w = colWidth;
    }

    auto canvas = writer_.GetCanvas(pageIndex);
    double y = box.top;
    // Header row.
    for (std::size_t c = 0; c < columns; ++c) {
        const double left = box.left + std::accumulate(widths.begin(), widths.begin() + static_cast<std::ptrdiff_t>(c), 0.0);
        canvas.BeginText().SetFontAndSize("Helvetica", options.fontSize)
            .MoveText(left + options.padding, y - options.fontSize)
            .ShowText(headers[c]).EndText();
    }
    y -= options.headerRowHeight;
    if (options.drawGrid) {
        canvas.SaveState().SetStrokeColor(PdfColor::Gray(0.7)).SetLineWidth(0.5);
        for (std::size_t c = 0; c <= columns; ++c) {
            const double x = box.left + std::accumulate(widths.begin(), widths.begin() + static_cast<std::ptrdiff_t>(c), 0.0);
            canvas.DrawLine(x, box.top, x, y);
        }
        canvas.DrawLine(box.left, box.top, box.right, box.top);
        canvas.DrawLine(box.left, y, box.right, y);
        canvas.RestoreState();
    }
    // Data rows.
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < columns && c < row.size(); ++c) {
            const double left = box.left + std::accumulate(widths.begin(), widths.begin() + static_cast<std::ptrdiff_t>(c), 0.0);
            canvas.BeginText().SetFontAndSize("Helvetica", options.fontSize)
                .MoveText(left + options.padding, y - options.fontSize)
                .ShowText(row[c]).EndText();
        }
        if (options.drawGrid) {
            canvas.SaveState().SetStrokeColor(PdfColor::Gray(0.7)).SetLineWidth(0.5);
            canvas.DrawLine(box.left, y, box.right, y);
            canvas.RestoreState();
        }
        y -= options.rowHeight;
    }
    if (options.drawGrid) {
        canvas.SaveState().SetStrokeColor(PdfColor::Gray(0.7)).SetLineWidth(0.5);
        canvas.DrawLine(box.left, y, box.right, y);
        canvas.RestoreState();
    }
    return y;
}

} // namespace CPPPdf

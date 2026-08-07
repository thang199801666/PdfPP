#include <CPPPdf/CPPPdf.h>
#include <CPPPdf/Document/PdfDocumentLayout.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::vector<std::byte> ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open test data: " + path.string());
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size < 0) throw std::runtime_error("Cannot determine test data size: " + path.string());
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("Cannot read test data: " + path.string());
    return bytes;
}

std::vector<std::byte> MakeGradient(const std::uint32_t width, const std::uint32_t height) {
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 3U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 3U;
            pixels[offset] = static_cast<std::byte>((x * 7U + y * 3U) & 0xFFU);
            pixels[offset + 1U] = static_cast<std::byte>((y * 11U + x * 2U) & 0xFFU);
            pixels[offset + 2U] = static_cast<std::byte>((x * 5U + y * 13U) & 0xFFU);
        }
    }
    return pixels;
}

CPPPdf::PdfDocumentLayout::TextRun Run(std::string text,
                                       std::string font = "Helvetica",
                                       const double size = 10.0) {
    CPPPdf::PdfDocumentLayout::TextRun run;
    run.text = std::move(text);
    run.base14Font = std::move(font);
    run.fontSize = size;
    return run;
}
} // namespace

int main() {
    try {
        using namespace CPPPdf;
        const auto working = std::filesystem::current_path();
        const auto gradient = MakeGradient(37U, 29U);
        const auto jpeg = PdfImage::EncodeJpeg(37U, 29U, gradient, 86);
        if (jpeg.size() < 256U || jpeg[0] != std::byte{0xFF} || jpeg[1] != std::byte{0xD8}) return 1;
        {
            std::ofstream output(working / "image_smoke.jpg", std::ios::binary);
            output.write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
        }
        const auto jpegImage = PdfImage::FromJpeg(jpeg);
        if (jpegImage.GetWidth() != 37U || jpegImage.GetHeight() != 29U ||
            jpegImage.GetEncoding() != PdfImageEncoding::Dct) return 2;

        const auto pngBytes = ReadFile(std::filesystem::path(PDFPP_TEST_DATA_DIR) / "adam7_rgba.png");
        const auto png = PdfImage::FromPng(pngBytes);
        if (png.GetWidth() != 17U || png.GetHeight() != 13U || png.GetBytes().size() != 17U * 13U * 3U ||
            !png.HasSoftMask() || png.GetSoftMaskBytes().size() != 17U * 13U) return 3;
        const auto decoded = png.GetBytes();
        const auto pixel = [&](const std::size_t x, const std::size_t y, const std::size_t channel) {
            return std::to_integer<std::uint8_t>(decoded[(y * 17U + x) * 3U + channel]);
        };
        if (pixel(1U, 0U, 0U) != 17U || pixel(1U, 0U, 1U) != 5U || pixel(1U, 0U, 2U) != 11U) return 4;
        if (pixel(2U, 1U, 0U) != 37U || pixel(2U, 1U, 1U) != 29U || pixel(2U, 1U, 2U) != 35U ||
            std::to_integer<std::uint8_t>(png.GetSoftMaskBytes()[1U * 17U + 2U]) != 128U) return 5;

        // A dedicated image PDF lets Poppler validate DCT, inline-image syntax,
        // and cross-page XObject reuse.
        {
            PdfWriter imageWriter;
            const auto firstPage = imageWriter.AddPage(PdfRectangle{0, 0, 180, 140});
            const auto secondPage = imageWriter.AddPage(PdfRectangle{0, 0, 180, 140});
            imageWriter.GetCanvas(firstPage).DrawImage(jpegImage, PdfRectangle{10, 45, 170, 130});
            imageWriter.GetCanvas(secondPage).DrawImage(jpegImage, PdfRectangle{10, 10, 170, 130});
            const std::vector<std::byte> inlinePixels = {
                std::byte{255},std::byte{0},std::byte{0}, std::byte{0},std::byte{255},std::byte{0},
                std::byte{0},std::byte{0},std::byte{255}, std::byte{255},std::byte{255},std::byte{0}};
            const auto inlineImage = PdfImage::FromRgb(2U, 2U, inlinePixels);
            imageWriter.GetCanvas(firstPage).DrawInlineImage(inlineImage, PdfRectangle{65, 5, 115, 40});
            imageWriter.Save(working / "jpeg_smoke.pdf");
            const auto imagePdfBytes = ReadFile(working / "jpeg_smoke.pdf");
            const std::string imagePdf(reinterpret_cast<const char*>(imagePdfBytes.data()), imagePdfBytes.size());
            std::size_t imageObjectCount = 0U;
            std::size_t search = 0U;
            while ((search = imagePdf.find("/Subtype /Image", search)) != std::string::npos) {
                ++imageObjectCount;
                search += 15U;
            }
            if (imageObjectCount != 1U || imagePdf.find("BI\n/W 2 /H 2") == std::string::npos) return 6;
        }

        PdfWriter writer;
        (void)writer.AddPage(PdfRectangle{0, 0, 300, 220});
        PdfDocumentLayout layout(writer);
        const PdfRectangle flow{24, 28, 276, 196};

        std::vector<PdfDocumentLayout::RichParagraphOptions> paragraphs;
        for (int paragraphIndex = 0; paragraphIndex < 5; ++paragraphIndex) {
            PdfDocumentLayout::RichParagraphOptions paragraph;
            paragraph.spaceAfter = 7.0;
            paragraph.lineSpacing = 1.18;
            paragraph.alignment = paragraphIndex % 3 == 0 ? PdfTextAlignment::Left :
                                  paragraphIndex % 3 == 1 ? PdfTextAlignment::Center : PdfTextAlignment::Right;
            paragraph.runs.push_back(Run("Rich paragraph " + std::to_string(paragraphIndex + 1) + ": ",
                                         "Helvetica-Bold", 12.0));
            auto colored = Run("mixed runs, underline, wrapping and automatic page flow. ", "Helvetica", 10.0);
            colored.color = PdfColor::Blue();
            colored.underline = true;
            paragraph.runs.push_back(std::move(colored));
            auto raised = Run("H2O ", "Times-Roman", 10.0);
            raised.textRise = 2.0;
            paragraph.runs.push_back(std::move(raised));
            paragraph.runs.push_back(Run(
                "This deliberately long sentence repeats enough content to force grapheme-safe wrapping and page breaks without splitting styled words incorrectly. ",
                "Times-Roman", 10.0));
            paragraphs.push_back(std::move(paragraph));
        }
        auto cursor = layout.FlowRichParagraphs(paragraphs, flow, 0U, flow.top);
        if (cursor.pagesWritten < 2U) return 7;

        std::vector<PdfDocumentLayout::NestedListItem> listItems;
        for (int index = 0; index < 3; ++index) {
            PdfDocumentLayout::NestedListItem item;
            item.runs.push_back(Run("Nested list item " + std::to_string(index + 1) +
                " with enough text to wrap onto another line.", "Helvetica", 10.0));
            PdfDocumentLayout::NestedListItem child;
            child.runs.push_back(Run("Child item using a different marker style.", "Helvetica-Oblique", 9.0));
            item.children.push_back(std::move(child));
            listItems.push_back(std::move(item));
        }
        PdfDocumentLayout::FlowListOptions listOptions;
        listOptions.itemSpacing = 2.0;
        auto listResult = layout.FlowNestedList(listItems, flow, listOptions,
                                                cursor.currentPage, cursor.currentY);

        std::vector<PdfDocumentLayout::TableRow> rows;
        PdfDocumentLayout::TableRow header;
        header.header = true;
        for (const char* value : {"Group", "Description", "Value"}) {
            PdfDocumentLayout::TableCell cell;
            cell.backgroundColor = PdfColor::Gray(0.88);
            cell.runs.push_back(Run(value, "Helvetica-Bold", 9.0));
            header.cells.push_back(std::move(cell));
        }
        rows.push_back(std::move(header));
        for (int index = 0; index < 24; ++index) {
            PdfDocumentLayout::TableRow row;
            if (index == 0) {
                PdfDocumentLayout::TableCell group;
                group.rowSpan = 2U;
                group.verticalAlignment = PdfDocumentLayout::VerticalAlignment::Middle;
                group.backgroundColor = PdfColor::Gray(0.95);
                group.runs.push_back(Run("rowSpan=2", "Helvetica-Bold", 8.5));
                row.cells.push_back(std::move(group));
            }
            if (index == 5) {
                PdfDocumentLayout::TableCell span;
                span.columnSpan = 2U;
                span.backgroundColor = PdfColor::Gray(0.93);
                span.runs.push_back(Run("A column-spanning cell with auto-width wrapping", "Helvetica-Bold", 8.5));
                row.cells.push_back(std::move(span));
                PdfDocumentLayout::TableCell value;
                value.runs.push_back(Run("span", "Courier", 8.5));
                row.cells.push_back(std::move(value));
            } else {
                if (index != 0 && index != 1) {
                    PdfDocumentLayout::TableCell group;
                    group.runs.push_back(Run("G" + std::to_string(index + 1), "Helvetica", 8.5));
                    row.cells.push_back(std::move(group));
                }
                PdfDocumentLayout::TableCell description;
                description.runs.push_back(Run(
                    "Auto-sized description row " + std::to_string(index + 1) +
                    " that may wrap and must remain inside its cell.", "Helvetica", 8.5));
                row.cells.push_back(std::move(description));
                PdfDocumentLayout::TableCell value;
                value.alignment = PdfTextAlignment::Right;
                value.runs.push_back(Run(std::to_string(index * 17), "Courier", 8.5));
                row.cells.push_back(std::move(value));
            }
            rows.push_back(std::move(row));
        }
        PdfDocumentLayout::FlowTableOptions tableOptions;
        tableOptions.columnCount = 3U;
        tableOptions.columnWidths = {1.0, 3.2, 1.0};
        tableOptions.repeatHeaderRows = true;
        const auto tableResult = layout.FlowTable(rows, flow, tableOptions,
                                                  listResult.currentPage, listResult.currentY);
        if (tableResult.rowsWritten != rows.size() || tableResult.pagesWritten < 2U) return 8;

        PdfDocumentLayout::ImageLayoutOptions imageOptions;
        imageOptions.requestedWidth = 150.0;
        imageOptions.requestedHeight = 80.0;
        imageOptions.fit = PdfDocumentLayout::ImageFitMode::Cover;
        imageOptions.spaceBefore = 5.0;
        imageOptions.spaceAfter = 5.0;
        const auto imageResult = layout.FlowImage(jpegImage, flow, imageOptions,
                                                  tableResult.currentPage, tableResult.currentY);
        if (imageResult.currentPage >= writer.GetPageCount()) return 9;

        PdfDocumentLayout::HeaderFooterOptions footer;
        footer.leftText = "Pdf++ layout smoke";
        footer.centerText = "/PAGE / /NPAGE";
        footer.rightText = "JPEG + Adam7";
        footer.fontSize = 8.0;
        (void)layout.DrawFooter(0U, writer.GetPageCount() - 1U,
                                PdfRectangle{0, 0, 300, 220}, footer);
        writer.Save(working / "layout_smoke.pdf");

        const auto document = PdfDocument::Open(working / "layout_smoke.pdf");
        if (document.GetPageCount() != writer.GetPageCount() || document.GetPageCount() < 4U) return 10;

        std::cout << "JPEG, Adam7 PNG, rich layout and flowing table smoke tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 20;
    }
}

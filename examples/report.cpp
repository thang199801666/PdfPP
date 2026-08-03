// Pdf++ example: build a report with headers, footers, a list, and a portfolio
// of embedded files, then render it in parallel.
//
// Build (MSVC):
//   cl /std:c++20 /EHsc /I src\Pdf++.Core\include report.cpp Pdf++.Core.lib zlibstatic.lib

#include <CPPPdf/Api.hpp>
#include <CPPPdf/Document/PdfDocumentLayout.hpp>
#include <CPPPdf/Rendering/PdfPageRenderer.hpp>

#include <cstdio>
#include <string>
#include <vector>

int main() {
    using namespace CPPPdf;
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 595, 842});
    (void)page;

    // Headers and footers on every page.
    PdfDocumentLayout layout(writer);
    layout.DrawHeader(0U, 0U, PdfRectangle{0, 0, 595, 842},
        PdfDocumentLayout::HeaderFooterOptions{"", "Pdf++", "Report"});
    layout.DrawFooter(0U, 0U, PdfRectangle{0, 0, 595, 842},
        PdfDocumentLayout::HeaderFooterOptions{"", "", "", 9.0, true, true});

    // A bullet list.
    layout.DrawList(0U, {"First item", "Second item", "Third item"}, 60.0, 760.0,
        PdfDocumentLayout::ListOptions{PdfDocumentLayout::ListStyle::Bullet});

    // Two columns of text.
    layout.DrawColumns(0U, {"Left column text content.", "Right column text content."},
        PdfRectangle{60, 500, 535, 740}, 18.0);

    // An embedded file and a portfolio shell.
    const std::string note = "Hello from Pdf++";
    writer.AddEmbeddedFile("note.txt", std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(note.data()),
        reinterpret_cast<const std::byte*>(note.data()) + note.size()));
    writer.SetPortfolio(PdfPortfolioOptions{"Sample Portfolio", "T"});

    const auto output = std::filesystem::temp_directory_path() / "pdfpp_report.pdf";
    writer.Save(output);

    // Render all pages in parallel and dump a PPM of page 0.
    const auto frames = PdfPageRenderer::RenderAllPagesParallel(output, PdfRenderOptions{});
    if (!frames.empty()) {
        const auto ppm = std::filesystem::temp_directory_path() / "pdfpp_report.pgm";
        frames[0].bitmap.SavePpm(ppm);
        std::printf("Wrote %s (%u pages), page 0 as %s\n",
                    output.string().c_str(), static_cast<unsigned>(frames.size()),
                    ppm.string().c_str());
    }
    return 0;
}

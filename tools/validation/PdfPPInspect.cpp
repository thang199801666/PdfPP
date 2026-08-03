#include <CPPPdf/PdfDocument.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Rendering/PdfPageRenderer.hpp>

#include <filesystem>
#include <iostream>
#include <string>

// Inspects a PDF and, when --render is supplied, renders every page to a PPM
// file and prints a compact per-page summary that a differential harness can
// compare against MuPDF/Poppler output.
int main(int argc, char** argv) {
    std::filesystem::path input;
    std::filesystem::path renderDir;
    double dpi = 72.0;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--render" && i + 1 < argc) renderDir = argv[++i];
        else if (argument == "--dpi" && i + 1 < argc) dpi = std::stod(argv[++i]);
        else input = argument;
    }
    if (input.empty()) {
        std::cerr << "usage: PdfPP.Inspect <input.pdf> [--render <dir>] [--dpi <dpi>]\n";
        return 2;
    }

    try {
        const auto document = CPPPdf::PdfDocument::Open(input);
        const auto info = document.GetDocumentInfo();
        std::cout << "version\t" << document.GetVersion() << '\n';
        std::cout << "pages\t" << document.GetPageCount() << '\n';
        std::cout << "bytes\t" << document.GetFileSize() << '\n';
        std::cout << "xref_entries\t" << document.GetXrefEntryCount() << '\n';
        std::cout << "title\t" << info.title << '\n';
        std::cout << "author\t" << info.author << '\n';
        for (std::size_t page = 0; page < document.GetPageCount(); ++page) {
            std::string text = document.GetPageText(page);
            for (char& value : text) {
                if (value == '\t' || value == '\r' || value == '\n') value = ' ';
            }
            std::cout << "page_text\t" << page << '\t' << text << '\n';
            std::cout << "page_images\t" << page << '\t'
                      << document.ExtractImages(page).size() << '\n';

            CPPPdf::PdfRenderOptions options;
            options.dpi = dpi;
            const auto bitmap = CPPPdf::PdfPageRenderer::Render(document, page, options);
            std::size_t dark = 0U;
            for (std::size_t y = 0; y < bitmap.GetHeight(); ++y) {
                for (std::size_t x = 0; x < bitmap.GetWidth(); ++x) {
                    const auto pixel = bitmap.GetPixel(x, y);
                    if (pixel.red < 128U && pixel.green < 128U && pixel.blue < 128U) ++dark;
                }
            }
            std::cout << "render\t" << page << '\t' << bitmap.GetWidth() << '\t'
                      << bitmap.GetHeight() << '\t' << dark << '\n';
            if (!renderDir.empty()) {
                std::filesystem::create_directories(renderDir);
                bitmap.SavePpm(renderDir / ("page-" + std::to_string(page + 1U) + ".ppm"));
            }
        }
        return 0;
    } catch (const CPPPdf::PdfException& error) {
        std::cerr << "pdf_error\t" << static_cast<int>(error.code())
                  << '\t' << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "error\t" << error.what() << '\n';
        return 1;
    }
}

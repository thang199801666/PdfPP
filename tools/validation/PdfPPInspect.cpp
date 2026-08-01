#include <CPPPdf/PdfDocument.hpp>
#include <CPPPdf/PdfError.hpp>

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: PdfPP.Inspect <input.pdf>\n";
        return 2;
    }

    try {
        const std::filesystem::path path = argv[1];
        const auto document = CPPPdf::PdfDocument::Open(path);
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

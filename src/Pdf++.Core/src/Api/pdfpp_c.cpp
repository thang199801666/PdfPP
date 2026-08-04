#include <CPPPdf/pdfpp_c.h>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Rendering/PdfPageRenderer.hpp>
#include <CPPPdf/Version.hpp>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

void setError(char* errbuf, const std::size_t errbufSize, const std::string& message) {
    if (errbuf != nullptr && errbufSize > 0U) {
        const std::size_t n = std::min(message.size(), errbufSize - 1U);
        std::memcpy(errbuf, message.data(), n);
        errbuf[n] = '\0';
    }
}

} // namespace

extern "C" {

PdfDocumentHandle pdfpp_open(const char* path, int* outPageCount, char* errbuf, const std::size_t errbufSize) {
    if (path == nullptr) {
        setError(errbuf, errbufSize, "Null path");
        return nullptr;
    }
    try {
        auto* document = new CPPPdf::PdfDocument(CPPPdf::PdfDocument::Open(path));
        if (outPageCount != nullptr) *outPageCount = static_cast<int>(document->GetPageCount());
        return document;
    } catch (const std::exception& error) {
        setError(errbuf, errbufSize, error.what());
        return nullptr;
    }
}

int pdfpp_page_count(PdfDocumentHandle doc, char* errbuf, const std::size_t errbufSize) {
    if (doc == nullptr) {
        setError(errbuf, errbufSize, "Null document");
        return -1;
    }
    try {
        return static_cast<int>(static_cast<CPPPdf::PdfDocument*>(doc)->GetPageCount());
    } catch (const std::exception& error) {
        setError(errbuf, errbufSize, error.what());
        return -1;
    }
}

int pdfpp_page_text(PdfDocumentHandle doc, const int pageIndex,
                    char* buffer, const std::size_t bufferSize, char* errbuf, const std::size_t errbufSize) {
    if (doc == nullptr || buffer == nullptr || bufferSize == 0U) {
        setError(errbuf, errbufSize, "Invalid arguments");
        return -1;
    }
    try {
        auto* document = static_cast<CPPPdf::PdfDocument*>(doc);
        const std::string text = document->GetPageText(static_cast<std::size_t>(pageIndex));
        if (text.size() >= bufferSize) {
            setError(errbuf, errbufSize, "Output buffer too small");
            return -1;
        }
        std::memcpy(buffer, text.data(), text.size());
        buffer[text.size()] = '\0';
        return static_cast<int>(text.size());
    } catch (const std::exception& error) {
        setError(errbuf, errbufSize, error.what());
        return -1;
    }
}

int pdfpp_render_ppm(PdfDocumentHandle doc, const int pageIndex, const double dpi,
                     const char* outputPath, char* errbuf, const std::size_t errbufSize) {
    if (doc == nullptr || outputPath == nullptr) {
        setError(errbuf, errbufSize, "Invalid arguments");
        return -1;
    }
    try {
        auto* document = static_cast<CPPPdf::PdfDocument*>(doc);
        CPPPdf::PdfRenderOptions options;
        options.dpi = dpi;
        const auto bitmap = CPPPdf::PdfPageRenderer::Render(
            *document, static_cast<std::size_t>(pageIndex), options);
        bitmap.SavePpm(outputPath);
        return 0;
    } catch (const std::exception& error) {
        setError(errbuf, errbufSize, error.what());
        return -1;
    }
}

const char* pdfpp_version(void) {
    return CPPPdf::VersionString.data();
}

void pdfpp_close(PdfDocumentHandle doc) {
    delete static_cast<CPPPdf::PdfDocument*>(doc);
}

} // extern "C"

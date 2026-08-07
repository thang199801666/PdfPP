#include <CPPPdf/CPPPdf.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

std::uintmax_t fileSize(const std::filesystem::path& path) {
    return std::filesystem::file_size(path);
}

} // namespace

int main() {
    using namespace CPPPdf;
    try {
        const auto directory = std::filesystem::temp_directory_path();
        const auto source = directory / "pdfpp_incremental_source.pdf";
        const auto output = directory / "pdfpp_incremental_output.pdf";

        PdfWriter writer;
        const auto pageIndex = writer.AddPage(PdfRectangle{0.0, 0.0, 300.0, 200.0});
        (void)pageIndex;
        writer.Save(source);
        const auto sourceSize = fileSize(source);

        auto document = PdfDocument::Open(source);

        bool samePathRejected = false;
        try {
            PdfIncrementalUpdate invalidUpdate(document, source);
        } catch (const PdfException&) {
            samePathRejected = true;
        }
        require(samePathRejected, "same-path incremental update was not rejected");
        require(fileSize(source) == sourceSize,
                "same-path validation truncated the source document");
        require(PdfDocument::Open(source).GetPageCount() == 1U,
                "same-path validation damaged the source document");

        PdfIncrementalUpdate update(document, output);
        PdfDictionary marker;
        marker.Put(PdfName("Type"), PdfObject(PdfName("PdfPPTest")));
        const auto markerReference = update.AddDictionary(marker);
        require(markerReference.objectNumber > 0U, "new object number was not allocated");
        update.Commit();

        require(fileSize(output) > sourceSize, "incremental revision was not appended");
        auto reopened = PdfDocument::Open(output);
        require(reopened.GetPageCount() == 1U, "incremental output lost the page tree");

        PdfSaveOptions resaveOptions;
        resaveOptions.mode = PdfSaveMode::Incremental;
        resaveOptions.writeXrefStream = true;
        const auto resaved = directory / "pdfpp_incremental_resave.pdf";
        PdfWriter::Resave(reopened, resaved, resaveOptions);
        require(fileSize(resaved) > fileSize(output), "general incremental resave did not append");
        require(PdfDocument::Open(resaved).GetPageCount() == 1U,
                "incremental resave produced an unreadable page tree");

        std::filesystem::remove(source);
        std::filesystem::remove(output);
        std::filesystem::remove(resaved);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

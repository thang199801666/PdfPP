#include <CPPPdf/CPPPdf.hpp>

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: PdfPP.WriteValidation <output.pdf>\n";
        return 2;
    }

    try {
        CPPPdf::PdfWriter writer;
        writer.SetTitle("PdfPP writer validation");
        writer.SetAuthor("PdfPP validation");
        for (std::size_t index = 0; index < 3U; ++index) {
            const auto page = writer.AddPage({0, 0, 612, 792});
            auto canvas = writer.GetCanvas(page);
            canvas.BeginText()
                .SetFontAndSize("Helvetica", 16.0)
                .SetTextMatrix(1, 0, 0, 1, 72, 720)
                .ShowText("PdfPP writer validation page " + std::to_string(index + 1U))
                .EndText();
            canvas.BeginText()
                .SetFontAndSize("Helvetica", 11.0)
                .SetTextMatrix(1, 0, 0, 1, 72, 690)
                .ShowText("Page marker: WRITER-VALIDATION-" + std::to_string(index + 1U))
                .EndText();
            canvas.Rectangle(72, 560, 180, 80).Stroke();
        }
        writer.Save(std::filesystem::path(argv[1]));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

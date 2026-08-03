#include <CPPPdf/CPPPdf.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// Generates a deterministic, self-owned validation corpus. The fixtures are
// produced by Pdf++'s own writer, so they are redistributable under the
// repository LICENSE. Each fixture exercises a distinct writer/reader path and
// can be validated against MuPDF/Poppler with the tools in this directory.
namespace {

void writeTextPage(CPPPdf::PdfWriter& writer) {
    const auto page = writer.AddPage({0, 0, 612, 792});
    auto canvas = writer.GetCanvas(page);
    canvas.BeginText()
        .SetFontAndSize("Helvetica", 18.0)
        .SetTextMatrix(1, 0, 0, 1, 72, 720)
        .ShowText("PdfPP corpus: text extraction")
        .EndText();
    canvas.BeginText()
        .SetFontAndSize("Helvetica", 11.0)
        .SetTextMatrix(1, 0, 0, 1, 72, 690)
        .ShowText("ASCII marker: CORPUS-TEXT-001")
        .EndText();
}

void writeVectorPage(CPPPdf::PdfWriter& writer) {
    const auto page = writer.AddPage({0, 0, 400, 300});
    auto canvas = writer.GetCanvas(page);
    canvas.SetStrokeColor(CPPPdf::PdfColor::Red())
        .SetLineWidth(3.0)
        .Rectangle(20, 20, 120, 80)
        .Stroke();
    canvas.SetFillColor(CPPPdf::PdfColor::Blue())
        .FillRectangle(160, 40, 60, 60);
    canvas.SetStrokeColor(CPPPdf::PdfColor::Black());
    const std::array<double, 2> dash{6.0, 4.0};
    canvas.SetDashPattern(dash, 0.0)
        .MoveTo(20, 200)
        .LineTo(200, 120)
        .Stroke();
}

void writeImagePage(CPPPdf::PdfWriter& writer) {
    const auto page = writer.AddPage({0, 0, 300, 300});
    auto canvas = writer.GetCanvas(page);
    const std::array<std::byte, 12> pixels{
        std::byte{255}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{255}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{0}
    };
    canvas.DrawImage(CPPPdf::PdfImage::FromRgb(2U, 2U, pixels),
                     {20, 20, 180, 180});
    canvas.BeginText().SetFontAndSize("Helvetica", 12.0)
        .SetTextMatrix(1, 0, 0, 1, 20, 250)
        .ShowText("CORPUS-IMAGE-001")
        .EndText();
}

void writeTransparencyPage(CPPPdf::PdfWriter& writer) {
    const auto page = writer.AddPage({0, 0, 300, 300});
    auto canvas = writer.GetCanvas(page);
    canvas.SetFillColor(CPPPdf::PdfColor::Red())
        .SetFillOpacity(0.5)
        .FillRectangle(20, 20, 120, 120);
    canvas.SetFillColor(CPPPdf::PdfColor::Blue())
        .SetBlendMode(CPPPdf::PdfBlendMode::Multiply)
        .FillRectangle(80, 80, 120, 120);
    canvas.SetBlendMode(CPPPdf::PdfBlendMode::SourceOver)
        .SetFillOpacity(1.0);
}

void writeMultiPage(CPPPdf::PdfWriter& writer) {
    for (std::size_t index = 0; index < 5U; ++index) {
        const auto page = writer.AddPage({0, 0, 400, 300});
        auto canvas = writer.GetCanvas(page);
        canvas.BeginText().SetFontAndSize("Helvetica", 14.0)
            .SetTextMatrix(1, 0, 0, 1, 40, 200)
            .ShowText("CORPUS-MULTI-" + std::to_string(index + 1U))
            .EndText();
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: PdfPP.GenCorpus <output-dir>\n";
        return 2;
    }
    const std::filesystem::path directory = argv[1];
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::cerr << "cannot create output directory: " << error.message() << '\n';
        return 1;
    }

    try {
        const std::vector<std::pair<std::string, void (*)(CPPPdf::PdfWriter&)>> fixtures = {
            {"corpus-text.pdf", writeTextPage},
            {"corpus-vector.pdf", writeVectorPage},
            {"corpus-image.pdf", writeImagePage},
            {"corpus-transparency.pdf", writeTransparencyPage},
            {"corpus-multipage.pdf", writeMultiPage},
        };
        for (const auto& [name, generator] : fixtures) {
            CPPPdf::PdfWriter writer;
            generator(writer);
            writer.Save(directory / name);
            std::cout << "wrote " << name << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

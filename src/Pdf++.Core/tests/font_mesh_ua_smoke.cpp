#include <CPPPdf/CPPPdf.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main() {
    try {
        using namespace CPPPdf;
        const auto path = std::filesystem::current_path() / "font_mesh_ua_smoke.pdf";

        PdfWriter writer;
        writer.SetTaggedPdf();
        writer.SetLanguage("en-US");
        const auto pageIndex = writer.AddPage(PdfRectangle{0.0, 0.0, 420.0, 300.0});

        PdfFreeFormMeshShadingOptions freeForm;
        freeForm.name = "Mesh4";
        freeForm.colorSpace = PdfDeviceColorSpace::Rgb;
        freeForm.triangles = {
            std::array<PdfMeshVertex, 3>{
                PdfMeshVertex{{20.0, 160.0}, {1.0, 0.0, 0.0}},
                PdfMeshVertex{{190.0, 160.0}, {0.0, 1.0, 0.0}},
                PdfMeshVertex{{20.0, 280.0}, {0.0, 0.0, 1.0}}},
            std::array<PdfMeshVertex, 3>{
                PdfMeshVertex{{190.0, 160.0}, {0.0, 1.0, 0.0}},
                PdfMeshVertex{{190.0, 280.0}, {1.0, 1.0, 0.0}},
                PdfMeshVertex{{20.0, 280.0}, {0.0, 0.0, 1.0}}}
        };
        (void)writer.AddFreeFormMeshShading(freeForm);

        PdfLatticeMeshShadingOptions lattice;
        lattice.name = "Mesh5";
        lattice.colorSpace = PdfDeviceColorSpace::Rgb;
        lattice.verticesPerRow = 2U;
        lattice.vertices = {
            {{220.0, 160.0}, {1.0, 0.2, 0.2}},
            {{390.0, 160.0}, {0.2, 1.0, 0.2}},
            {{220.0, 280.0}, {0.2, 0.2, 1.0}},
            {{390.0, 280.0}, {1.0, 0.4, 1.0}}
        };
        (void)writer.AddLatticeMeshShading(lattice);
        if (writer.GetMeshShadingCount() != 2U) return 1;

        PdfType3Font symbols("EngineeringSymbols", PdfRectangle{0.0, -100.0, 1000.0, 1000.0});
        symbols.AddGlyph(PdfType3Glyph{
            32U, "space", 350.0, PdfRectangle{0.0, 0.0, 1.0, 1.0}, "", 0x20U});
        symbols.AddGlyph(PdfType3Glyph{
            65U, "Delta", 850.0, PdfRectangle{20.0, 0.0, 820.0, 760.0},
            "0 0 0 rg 40 20 m 420 740 l 800 20 l h 80 20 m 420 650 l 710 20 l h f*",
            0x0394U});
        symbols.AddGlyph(PdfType3Glyph{
            66U, "Square", 850.0, PdfRectangle{40.0, 0.0, 810.0, 760.0},
            "0.1 0.25 0.8 rg 80 30 680 680 re f 1 1 1 RG 35 w 80 30 680 680 re S",
            0x25A0U});

        auto canvas = writer.GetCanvas(pageIndex);
        canvas.SaveState().Rectangle(20.0, 160.0, 170.0, 120.0).Clip().EndPath()
              .PaintShading("Mesh4").RestoreState();
        canvas.SaveState().Rectangle(220.0, 160.0, 170.0, 120.0).Clip().EndPath()
              .PaintShading("Mesh5").RestoreState();

        PdfMarkedContentOptions header;
        header.role = "TH";
        header.identifier = "stress-header";
        header.alternativeText = "Stress column header";
        header.attributes.scope = PdfTableScope::Column;
        header.attributes.placement = PdfStructurePlacement::Block;
        header.attributes.textAlignment = PdfStructureTextAlignment::Center;
        header.attributes.boundingBox = PdfRectangle{20.0, 95.0, 190.0, 145.0};
        header.attributes.width = 170.0;
        header.attributes.height = 50.0;
        canvas.BeginMarkedContent(header)
              .SetFillColor(PdfColor::Gray(0.9)).FillRectangle(20.0, 95.0, 170.0, 50.0)
              .EndMarkedContent();

        PdfMarkedContentOptions cell;
        cell.role = "TD";
        cell.identifier = "stress-cell";
        cell.attributes.headers = {"stress-header"};
        cell.attributes.columnSpan = 2U;
        cell.attributes.rowSpan = 2U;
        cell.attributes.placement = PdfStructurePlacement::Block;
        canvas.BeginMarkedContent(cell)
              .SetFillColor(PdfColor::Gray(0.75)).FillRectangle(220.0, 95.0, 170.0, 50.0)
              .EndMarkedContent();

        canvas.BeginText()
              .SetType3FontAndSize(symbols, 32.0)
              .SetTextMatrix(1.0, 0.0, 0.0, 1.0, 30.0, 35.0)
              .ShowType3TextUtf8("\xCE\x94 \xE2\x96\xA0")
              .EndText();

        bool verticalFontWritten = false;
        const std::array<std::filesystem::path, 3> fontCandidates{
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "C:/Windows/Fonts/arial.ttf"};
        for (const auto& fontPath : fontCandidates) {
            if (!std::filesystem::exists(fontPath)) continue;
            const auto font = PdfTrueTypeFont::Load(fontPath);
            canvas.BeginText()
                  .SetTrueTypeFontAndSizeVertical(font, 12.0)
                  .SetTextMatrix(1.0, 0.0, 0.0, 1.0, 350.0, 80.0)
                  .ShowTextUtf8("VERT")
                  .EndText();
            verticalFontWritten = true;
            break;
        }

        writer.Save(path);
        const auto document = PdfDocument::Open(path);
        if (document.GetPageCount() != 1U) return 2;
        const auto bytes = ReadText(path);
        for (const std::string token : {
                 "/Subtype /Type3", "/CharProcs", "/PdfPPType3ToUnicode",
                 "/ShadingType 4", "/ShadingType 5", "/BitsPerFlag 2",
                 "/VerticesPerRow 2", "/O /Layout", "/O /Table",
                 "/Scope /Column", "/RowSpan 2", "/ColSpan 2",
                 "/Headers [(stress-header) ", "/ID (stress-cell)"}) {
            if (bytes.find(token) == std::string::npos) return 3;
        }
        if (verticalFontWritten) {
            for (const std::string token : {"/Identity-V", "/DW2 [880 -1000]", "/W2 ["}) {
                if (bytes.find(token) == std::string::npos) return 4;
            }
        }

        std::cout << "Type3, vertical CID, mesh shading, and PDF/UA attributes smoke test passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}

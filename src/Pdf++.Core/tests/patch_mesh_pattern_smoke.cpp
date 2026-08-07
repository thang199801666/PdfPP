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

CPPPdf::PdfPatchCornerColors RgbCorners() {
    return {{{1.0, 0.1, 0.1}, {0.1, 1.0, 0.1},
             {0.1, 0.2, 1.0}, {1.0, 0.85, 0.1}}};
}
}

int main() {
    try {
        using namespace CPPPdf;
        const auto path = std::filesystem::current_path() / "patch_mesh_pattern_smoke.pdf";

        PdfWriter writer;

        PdfTilingPatternOptions hatch;
        hatch.name = "Hatch2";
        hatch.content = "0.8 w 0 0 m 8 8 l S";
        hatch.bbox = PdfRectangle{0.0, 0.0, 8.0, 8.0};
        hatch.xStep = 8.0;
        hatch.yStep = 8.0;
        hatch.paintTypeColor = false;
        hatch.tilingType = PdfTilingType::NoDistortion;
        hatch.matrix = {1.0, 0.0, 0.25, 1.0, 0.0, 0.0};
        (void)writer.AddTilingPattern(hatch);

        PdfCoonsPatchMeshShadingOptions coons;
        coons.name = "Mesh6";
        PdfCoonsPatch coonsPatch;
        coonsPatch.controlPoints = {
            PdfPoint{20.0, 20.0}, PdfPoint{65.0, 8.0}, PdfPoint{115.0, 8.0}, PdfPoint{160.0, 20.0},
            PdfPoint{172.0, 65.0}, PdfPoint{172.0, 115.0}, PdfPoint{160.0, 160.0},
            PdfPoint{115.0, 172.0}, PdfPoint{65.0, 172.0}, PdfPoint{20.0, 160.0},
            PdfPoint{8.0, 115.0}, PdfPoint{8.0, 65.0}};
        coonsPatch.cornerColors = RgbCorners();
        coons.patches.push_back(coonsPatch);
        (void)writer.AddCoonsPatchMeshShading(coons);

        PdfTensorProductPatchMeshShadingOptions tensor;
        tensor.name = "Mesh7";
        PdfTensorProductPatch tensorPatch;
        // PDF Type 7 order: twelve boundary points followed by four interior points.
        tensorPatch.controlPoints = {
            PdfPoint{220.0, 20.0}, PdfPoint{265.0, 2.0}, PdfPoint{315.0, 2.0}, PdfPoint{360.0, 20.0},
            PdfPoint{378.0, 65.0}, PdfPoint{378.0, 115.0}, PdfPoint{360.0, 160.0},
            PdfPoint{315.0, 178.0}, PdfPoint{265.0, 178.0}, PdfPoint{220.0, 160.0},
            PdfPoint{202.0, 115.0}, PdfPoint{202.0, 65.0},
            PdfPoint{262.0, 58.0}, PdfPoint{318.0, 58.0},
            PdfPoint{318.0, 122.0}, PdfPoint{262.0, 122.0}};
        tensorPatch.cornerColors = RgbCorners();
        tensor.patches.push_back(tensorPatch);
        (void)writer.AddTensorProductPatchMeshShading(tensor);

        if (writer.GetMeshShadingCount() != 2U) return 1;

        const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 400.0, 360.0});
        auto canvas = writer.GetCanvas(page);
        const std::array<double, 3U> hatchColor{0.08, 0.25, 0.75};
        canvas.SetUncoloredPattern("Hatch2", PdfPatternBaseColorSpace::Rgb, hatchColor, true, false)
              .FillRectangle(20.0, 220.0, 360.0, 110.0);
        canvas.SaveState().ConcatenateMatrix(1.0, 0.0, 0.0, 1.0, 10.0, 20.0)
              .PaintShading("Mesh6").RestoreState();
        canvas.SaveState().ConcatenateMatrix(1.0, 0.0, 0.0, 1.0, 10.0, 20.0)
              .PaintShading("Mesh7").RestoreState();

        bool coloredApiRejected = false;
        try {
            canvas.SetPattern("Hatch2");
        } catch (const std::invalid_argument&) {
            coloredApiRejected = true;
        }
        if (!coloredApiRejected) return 2;

        const std::array<double, 1U> wrongComponentCount{0.5};
        bool componentCountRejected = false;
        try {
            canvas.SetUncoloredPattern("Hatch2", PdfPatternBaseColorSpace::Rgb,
                                       wrongComponentCount);
        } catch (const std::invalid_argument&) {
            componentCountRejected = true;
        }
        if (!componentCountRejected) return 3;

        writer.Save(path);
        const auto document = PdfDocument::Open(path);
        if (document.GetPageCount() != 1U) return 4;

        const auto bytes = ReadText(path);
        for (const std::string token : {
                 "/ShadingType 6", "/ShadingType 7", "/BitsPerFlag 2",
                 "/PaintType 2", "/TilingType 2", "/Matrix [1 0 0.25 1 0 0 ",
                 "[/Pattern /DeviceRGB] cs 0.08 0.25 0.75 /Hatch2 scn"}) {
            if (bytes.find(token) == std::string::npos) {
                std::cerr << "Missing token: " << token << '\n';
                return 5;
            }
        }

        std::cout << "Patch mesh and uncolored pattern smoke test passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}

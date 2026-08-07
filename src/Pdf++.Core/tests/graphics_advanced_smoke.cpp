#include <CPPPdf/CPPPdf.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::vector<std::byte> MinimalRgbIccProfile() {
    std::vector<std::byte> profile(132U, std::byte{0});
    const auto put32 = [&](const std::size_t offset, const std::uint32_t value) {
        profile[offset] = static_cast<std::byte>((value >> 24U) & 0xFFU);
        profile[offset + 1U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
        profile[offset + 2U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
        profile[offset + 3U] = static_cast<std::byte>(value & 0xFFU);
    };
    const auto putText = [&](const std::size_t offset, const char* text) {
        for (std::size_t index = 0U; index < 4U; ++index) {
            profile[offset + index] = static_cast<std::byte>(text[index]);
        }
    };
    put32(0U, static_cast<std::uint32_t>(profile.size()));
    put32(8U, 0x04300000U);
    putText(12U, "mntr");
    putText(16U, "RGB ");
    putText(20U, "XYZ ");
    putText(36U, "acsp");
    putText(40U, "MSFT");
    put32(68U, 0x0000F6D6U);
    put32(72U, 0x00010000U);
    put32(76U, 0x0000D32DU);
    putText(80U, "PDFP");
    put32(128U, 0U);
    return profile;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
    try {
        using namespace CPPPdf;
        const auto path = std::filesystem::current_path() / "graphics_advanced_smoke.pdf";
        PdfWriter writer;

        PdfIccColorSpaceOptions icc;
        icc.name = "CSICC";
        icc.profileBytes = MinimalRgbIccProfile();
        (void)writer.AddIccColorSpace(icc);

        PdfSeparationColorSpaceOptions separation;
        separation.name = "CSSpot";
        separation.colorantName = "PdfPPOrange";
        separation.c0 = {0.0, 0.0, 0.0, 0.0};
        separation.c1 = {0.0, 0.55, 1.0, 0.0};
        (void)writer.AddSeparationColorSpace(separation);

        PdfDeviceNColorSpaceOptions deviceN;
        deviceN.name = "CSDeviceN";
        deviceN.colorantNames = {"PdfPPOrange", "PdfPPViolet"};
        deviceN.tintTransformProgram = "exch 0 exch 0";
        (void)writer.AddDeviceNColorSpace(deviceN);
        if (writer.GetColorSpaceCount() != 3U) return 1;

        const auto page = writer.AddPage(PdfRectangle{0.0, 0.0, 420.0, 420.0});
        auto canvas = writer.GetCanvas(page);
        const std::array<double, 3U> iccComponents{0.1, 0.5, 0.8};
        const std::array<double, 1U> spotComponents{0.75};
        const std::array<double, 2U> deviceNComponents{0.6, 0.35};
        canvas.SetFillColorSpace("CSICC", iccComponents).FillRectangle(15.0, 370.0, 115.0, 30.0);
        canvas.SetFillColorSpace("CSSpot", spotComponents).FillRectangle(150.0, 370.0, 115.0, 30.0);
        canvas.SetFillColorSpace("CSDeviceN", deviceNComponents).FillRectangle(285.0, 370.0, 115.0, 30.0);

        const std::array<PdfBlendMode, 16U> modes{
            PdfBlendMode::SourceOver, PdfBlendMode::Multiply, PdfBlendMode::Screen,
            PdfBlendMode::Overlay, PdfBlendMode::Darken, PdfBlendMode::Lighten,
            PdfBlendMode::ColorDodge, PdfBlendMode::ColorBurn, PdfBlendMode::HardLight,
            PdfBlendMode::SoftLight, PdfBlendMode::Difference, PdfBlendMode::Exclusion,
            PdfBlendMode::Hue, PdfBlendMode::Saturation, PdfBlendMode::Color,
            PdfBlendMode::Luminosity};
        for (std::size_t index = 0U; index < modes.size(); ++index) {
            const double x = 20.0 + static_cast<double>(index % 4U) * 98.0;
            const double y = 275.0 - static_cast<double>(index / 4U) * 68.0;
            canvas.SaveState()
                  .SetFillColor(PdfColor::Red())
                  .FillRectangle(x, y, 55.0, 45.0)
                  .SetTransparency(0.8, 0.72, modes[index])
                  .SetFillColor(PdfColor::Blue())
                  .FillRectangle(x + 25.0, y + 15.0, 55.0, 45.0)
                  .RestoreState();
        }

        std::vector<std::byte> rgba(24U * 24U * 4U);
        for (std::size_t y = 0U; y < 24U; ++y) {
            for (std::size_t x = 0U; x < 24U; ++x) {
                const auto offset = (y * 24U + x) * 4U;
                rgba[offset] = static_cast<std::byte>(10U + x * 9U);
                rgba[offset + 1U] = static_cast<std::byte>(20U + y * 8U);
                rgba[offset + 2U] = std::byte{180};
                rgba[offset + 3U] = static_cast<std::byte>((x + y) * 255U / 46U);
            }
        }
        const std::array<double, 3U> matte{1.0, 1.0, 1.0};
        const auto transparentImage = PdfImage::FromRgba(24U, 24U, rgba, matte);
        if (!transparentImage.HasSoftMask()) return 2;
        canvas.DrawImage(transparentImage, PdfRectangle{145.0, 10.0, 275.0, 115.0});
        writer.Save(path);

        const auto document = PdfDocument::Open(path);
        if (document.GetPageCount() != 1U) return 3;
        const auto bytes = ReadText(path);
        for (const std::string token : {"/ICCBased", "/Separation", "/DeviceN", "/SMask",
                                        "/Matte [1 1 1 ", "/ColorDodge", "/ColorBurn",
                                        "/HardLight", "/SoftLight", "/Luminosity"}) {
            if (bytes.find(token) == std::string::npos) return 4;
        }

        for (const auto mode : modes) {
            PdfBitmap bitmap(1U, 1U, PdfRgbaColor{40, 100, 180, 255});
            bitmap.BlendPixel(0, 0, PdfRgbaColor{220, 70, 30, 180}, mode);
            if (bitmap.GetPixel(0U, 0U).alpha == 0U) return 5;
        }

        bool clearRejected = false;
        try {
            writer.ClearColorSpaces();
        } catch (const std::logic_error&) {
            clearRejected = true;
        }
        if (!clearRejected) return 6;

        std::cout << "Advanced graphics/color-space smoke test passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}

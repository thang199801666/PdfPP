#include <CPPPdf/Api.hpp>
#include "TestRunner.hpp"
#include "TestHelpers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sstream>
#include <regex>
#include <vector>

namespace {
using namespace CPPPdf;

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    PDFPP_TEST_CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template <typename Callable>
void ExpectThrows(Callable&& callable) {
    bool thrown = false;
    try {
        std::forward<Callable>(callable)();
    } catch (const std::exception&) {
        thrown = true;
    }
    PDFPP_TEST_CHECK(thrown);
}

void TestCanvasGraphicsStateAndPaths() {
    const auto output = TempPath("pdfpp_feature_canvas.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 300, 300});
    const std::array<double, 2> dashPattern{3.0, 2.0};
    writer.GetCanvas(page)
        .SaveState()
        .SetStrokeColor(PdfColor::Red())
        .SetFillColor(PdfColor::Blue())
        .SetStrokeOpacity(0.5)
        .SetFillOpacity(0.25)
        .SetBlendMode(PdfBlendMode::Multiply)
        .SetLineWidth(2.0)
        .SetLineCap(PdfLineCap::Round)
        .SetLineJoin(PdfLineJoin::Bevel)
        .SetMiterLimit(4.0)
        .SetDashPattern(dashPattern, 1.0)
        .MoveTo(10, 10)
        .LineTo(100, 10)
        .CurveTo(110, 20, 120, 30, 130, 40)
        .ClosePath()
        .FillStroke()
        .Rectangle(20, 50, 80, 30)
        .Clip()
        .EndPath()
        .ConcatenateMatrix(1, 0, 0, 1, 5, 6)
        .RestoreState();
    writer.Save(output);

    const auto pdf = ReadText(output);
    PDFPP_TEST_CHECK(pdf.find("2 w") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("1 J") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("2 j") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("[3 2 ] 1 d") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/BM /Multiply") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find(" W") != std::string::npos || pdf.find("W\n") != std::string::npos);
    PDFPP_TEST_CHECK(PdfDocument::Open(output).GetPageCount() == 1);
    std::filesystem::remove(output);
}

void TestCanvasTextValidation() {
    const auto output = TempPath("pdfpp_feature_canvas_text.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage();
    auto canvas = writer.GetCanvas(page);
    canvas.SetLineWidth(1.0)
        .SetStrokeOpacity(0.0)
        .SetFillOpacity(1.0)
        .BeginText()
        .SetFontAndSize("Helvetica", 12.0)
        .MoveText(10, 10)
        .ShowText("A (B) \\ C")
        .EndText();
    writer.Save(output);
    const auto pdf = ReadText(output);
    PDFPP_TEST_CHECK(pdf.find("A \\(B\\) \\\\ C") != std::string::npos);
    PDFPP_TEST_CHECK(PdfDocument::Open(output).GetPageText(0).find("A (B)") != std::string::npos);
    std::filesystem::remove(output);
}

void TestTextImageStampsAndWatermarks() {
    const auto output = TempPath("pdfpp_feature_stamps.pdf");
    PdfWriter writer;
    const auto page0 = writer.AddPage({0, 0, 200, 200});
    const auto page1 = writer.AddPage({0, 0, 200, 200});

    PdfTextStampOptions stamp;
    stamp.text = "APPROVED";
    stamp.position = {30, 40};
    stamp.fontSize = 14;
    stamp.opacity = 0.7;
    stamp.rotationDegrees = 15;
    stamp.drawBackground = true;
    stamp.drawBorder = true;
    writer.AddTextStamp(page0, stamp);

    PdfWatermarkOptions watermark;
    watermark.text = "DRAFT";
    watermark.fontSize = 24;
    watermark.opacity = 0.2;
    writer.AddWatermarkToAllPages(watermark);

    const std::array<std::byte, 3> pixel{std::byte{255}, std::byte{0}, std::byte{0}};
    const auto image = PdfImage::FromRgb(1, 1, pixel);
    PdfImageStampOptions imageStamp;
    imageStamp.rectangle = {100, 100, 120, 120};
    imageStamp.opacity = 0.8;
    imageStamp.drawBorder = true;
    writer.AddImageStamp(page1, image, imageStamp);
    writer.Save(output);

    const auto pdf = ReadText(output);
    PDFPP_TEST_CHECK(pdf.find("APPROVED") != std::string::npos);
    PDFPP_TEST_CHECK(std::count(pdf.begin(), pdf.end(), 'D') >= 2);
    PDFPP_TEST_CHECK(pdf.find("/Subtype /Image") != std::string::npos);
    PDFPP_TEST_CHECK(PdfDocument::Open(output).GetPageCount() == 2);
    std::filesystem::remove(output);
}

void TestViewerPreferencesSerialization() {
    const auto output = TempPath("pdfpp_feature_viewer_preferences.pdf");
    PdfWriter writer;
    (void)writer.AddPage();
    PdfViewerPreferences preferences;
    preferences.pageLayout = PdfPageLayout::TwoPageRight;
    preferences.pageMode = PdfPageMode::FullScreen;
    preferences.readingDirection = PdfReadingDirection::RightToLeft;
    preferences.hideToolbar = true;
    preferences.hideMenuBar = true;
    preferences.hideWindowUi = true;
    preferences.fitWindow = true;
    preferences.centerWindow = true;
    preferences.displayDocumentTitle = true;
    preferences.nonFullScreenPageMode = PdfPageMode::UseOutlines;
    preferences.printScaling = PdfPrintScaling::None;
    preferences.duplex = PdfDuplexMode::DuplexFlipLongEdge;
    preferences.pickTrayByPdfSize = true;
    preferences.numberOfCopies = 3;
    writer.SetViewerPreferences(preferences);
    PDFPP_TEST_CHECK(writer.GetViewerPreferences().numberOfCopies == 3);
    writer.Save(output);

    const auto pdf = ReadText(output);
    for (const std::string token : {"/PageLayout /TwoPageRight", "/PageMode /FullScreen", "/Direction /R2L",
                                    "/HideToolbar true", "/HideMenubar true", "/HideWindowUI true",
                                    "/FitWindow true", "/CenterWindow true", "/DisplayDocTitle true",
                                    "/NonFullScreenPageMode /UseOutlines", "/PrintScaling /None",
                                    "/Duplex /DuplexFlipLongEdge", "/PickTrayByPDFSize true", "/NumCopies 3"}) {
        PDFPP_TEST_CHECK(pdf.find(token) != std::string::npos);
    }
    std::filesystem::remove(output);
}

void TestViewerPreferencesValidation() {
    PdfWriter writer;
    (void)writer.AddPage();
    PdfViewerPreferences invalid;
    invalid.numberOfCopies = 0;
    ExpectThrows([&] { writer.SetViewerPreferences(invalid); });
}

void TestPageLabelsLifecycleAndRemapping() {
    const auto output = TempPath("pdfpp_feature_page_labels.pdf");
    PdfWriter writer;
    (void)writer.AddPage();
    (void)writer.AddPage();
    (void)writer.AddPage();

    PdfPageLabelOptions roman;
    roman.style = PdfPageLabelStyle::LowerRoman;
    roman.prefix = "Front-";
    writer.AddPageLabel(0, roman);

    PdfPageLabelOptions decimal;
    decimal.style = PdfPageLabelStyle::Decimal;
    decimal.prefix = "P-";
    decimal.startNumber = 10;
    writer.AddPageLabel(2, decimal);
    PDFPP_TEST_CHECK(writer.GetPageLabelCount() == 2);

    (void)writer.InsertPage(1);
    writer.MovePage(3, 2);
    writer.Save(output);
    const auto pdf = ReadText(output);
    PDFPP_TEST_CHECK(pdf.find("/PageLabels") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/S /r") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/P (Front-)") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/St 10") != std::string::npos);

    writer.RemovePageLabel(0);
    PDFPP_TEST_CHECK(writer.GetPageLabelCount() == 1);
    writer.ClearPageLabels();
    PDFPP_TEST_CHECK(writer.GetPageLabelCount() == 0);
    ExpectThrows([&] { writer.AddPageLabel(99, roman); });
    PdfPageLabelOptions invalid;
    invalid.startNumber = 0;
    ExpectThrows([&] { writer.AddPageLabel(0, invalid); });
    std::filesystem::remove(output);
}

void TestNamedDestinationsAndLinks() {
    const auto output = TempPath("pdfpp_feature_destinations_links.pdf");
    PdfWriter writer;
    (void)writer.AddPage();
    (void)writer.AddPage();

    PdfDestinationOptions destination;
    destination.pageIndex = 1;
    destination.destinationType = PdfDestinationType::XYZ;
    destination.left = 20.0;
    destination.top = 700.0;
    destination.zoom = 1.5;
    writer.AddNamedDestination("details", destination);
    PDFPP_TEST_CHECK(writer.GetNamedDestinationCount() == 1);

    PdfLinkOptions link;
    link.rectangle = {10, 10, 100, 30};
    link.drawBorder = true;
    link.borderWidth = 2;
    writer.AddNamedDestinationLink(0, "details", link);
    writer.AddUriLink(0, "https://example.com", link);
    PDFPP_TEST_CHECK(writer.GetLinkCount(0) == 2);
    writer.Save(output);

    const auto pdf = ReadText(output);
    PDFPP_TEST_CHECK(pdf.find("/Dests") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("(details)") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/Subtype /Link") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/S /URI") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("(https://example.com)") != std::string::npos);

    writer.ClearLinks(0);
    PDFPP_TEST_CHECK(writer.GetLinkCount(0) == 0);
    writer.RemoveNamedDestination("details");
    PDFPP_TEST_CHECK(writer.GetNamedDestinationCount() == 0);
    writer.ClearNamedDestinations();
    writer.AddNamedDestination("again", destination);
    ExpectThrows([&] { writer.AddNamedDestination("again", destination); });
    ExpectThrows([&] { writer.AddNamedDestinationLink(99, "again", link); });
    ExpectThrows([&] { writer.AddUriLink(99, "https://example.com", link); });
    std::filesystem::remove(output);
}

void TestOpenActionLifecycleAndRemapping() {
    const auto output = TempPath("pdfpp_feature_open_action.pdf");
    PdfWriter writer;
    (void)writer.AddPage();
    (void)writer.AddPage();
    PdfDestinationOptions action;
    action.pageIndex = 1;
    action.destinationType = PdfDestinationType::FitWidth;
    action.top = 800.0;
    writer.SetOpenAction(action);
    PDFPP_TEST_CHECK(writer.HasOpenAction());
    (void)writer.InsertPage(0);
    writer.MovePage(2, 1);
    writer.Save(output);
    PDFPP_TEST_CHECK(ReadText(output).find("/OpenAction") != std::string::npos);

    writer.ClearOpenAction();
    PDFPP_TEST_CHECK(!writer.HasOpenAction());
    action.pageIndex = 99;
    ExpectThrows([&] { writer.SetOpenAction(action); });
    action.pageIndex = 0;
    action.destinationType = PdfDestinationType::XYZ;
    action.zoom = 0.0;
    ExpectThrows([&] { writer.SetOpenAction(action); });
    std::filesystem::remove(output);
}

void TestBookmarkValidationAndLifecycle() {
    PdfWriter writer;
    (void)writer.AddPage();
    PdfBookmarkOptions bookmark;
    bookmark.title = "Root";
    bookmark.pageIndex = 0;
    const auto root = writer.AddBookmark(bookmark);
    PDFPP_TEST_CHECK(root == 0);
    PDFPP_TEST_CHECK(writer.GetBookmarkCount() == 1);

    PdfBookmarkOptions child = bookmark;
    child.title = "Child";
    child.parentIndex = root;
    child.zoom = 1.25;
    child.destinationType = PdfBookmarkDestinationType::XYZ;
    PDFPP_TEST_CHECK(writer.AddBookmark(child) == 1);

    PdfBookmarkOptions invalid = bookmark;
    invalid.title.clear();
    ExpectThrows([&] { (void)writer.AddBookmark(invalid); });
    invalid = bookmark;
    invalid.parentIndex = 99;
    ExpectThrows([&] { (void)writer.AddBookmark(invalid); });
    invalid = bookmark;
    invalid.destinationType = PdfBookmarkDestinationType::XYZ;
    invalid.zoom = 0.0;
    ExpectThrows([&] { (void)writer.AddBookmark(invalid); });

    writer.ClearBookmarks();
    PDFPP_TEST_CHECK(writer.GetBookmarkCount() == 0);
}

void TestEmbeddedFileLifecycleAndValidation() {
    const auto filePath = TempPath("pdfpp_feature_attachment_input.txt");
    {
        std::ofstream output(filePath, std::ios::binary);
        output << "attachment";
    }
    PdfWriter writer;
    const auto page = writer.AddPage();
    PdfEmbeddedFileOptions options;
    options.compress = false;
    options.associateWithDocument = false;
    writer.AddEmbeddedFile(filePath, options);
    PDFPP_TEST_CHECK(writer.GetEmbeddedFileCount() == 1);

    PdfFileAttachmentOptions attachment;
    attachment.rectangle = {10, 10, 30, 30};
    writer.AddFileAttachment(page, filePath.filename().string(), attachment);
    ExpectThrows([&] { writer.RemoveEmbeddedFile(filePath.filename().string()); });
    writer.ClearFileAttachments(page);
    writer.RemoveEmbeddedFile(filePath.filename().string());
    PDFPP_TEST_CHECK(writer.GetEmbeddedFileCount() == 0);

    const std::array<std::byte, 1> byte{std::byte{1}};
    ExpectThrows([&] { writer.AddEmbeddedFile("", byte); });
    writer.AddEmbeddedFile("one.bin", byte);
    ExpectThrows([&] { writer.AddEmbeddedFile("one.bin", byte); });
    ExpectThrows([&] { writer.AddFileAttachment(page, "missing.bin", attachment); });
    writer.ClearEmbeddedFiles();
    PDFPP_TEST_CHECK(writer.GetEmbeddedFileCount() == 0);
    std::filesystem::remove(filePath);
}

void TestShadingRenderingAndSoftMask() {
    // Build a raw PDF with an axial shading resource and an image whose soft
    // mask has a different size than the image itself.
    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    std::array<std::size_t, 9> offsets{};
    const auto object = [&](const std::size_t number, const std::string& body) {
        offsets[number] = static_cast<std::size_t>(pdf.tellp());
        pdf << number << " 0 obj\n" << body << "\nendobj\n";
    };
    object(1, "<< /Type /Catalog /Pages 2 0 R >>");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    object(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
              "/Resources << /Shading << /Sh1 4 0 R >> /XObject << /Im1 6 0 R >> >> "
              "/Contents 8 0 R >>");
    object(4, "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 100 0] "
              "/Function 5 0 R /Extend [true true] >>");
    object(5, "<< /FunctionType 2 /C0 [0 0 1] /C1 [1 0 0] /N 1 >>");
    const std::string imageStream = "\xFF\x00\x00\x00\xFF\x00\x00\x00\xFF\xFF\xFF\x00";
    object(6, "<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
              "/ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 9 0 R "
              "/Length " + std::to_string(imageStream.size()) + " >>\nstream\n"
              + imageStream + "\nendstream");
    const std::string content = "0 0 100 100 re W n /Sh1 sh /Im1 Do";
    object(8, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n"
              + content + "endstream");
    // 4x4 soft mask (different size than the 2x2 image): all opaque, so the
    // image must still be visible over the shading.
    const std::string maskStream = std::string(16, '\xFF');
    object(9, "<< /Type /XObject /Subtype /Image /Width 4 /Height 4 "
              "/ColorSpace /DeviceGray /BitsPerComponent 8 /Length "
              + std::to_string(maskStream.size()) + " >>\nstream\n"
              + maskStream + "\nendstream");
    const std::size_t xrefOffset = static_cast<std::size_t>(pdf.tellp());
    pdf << "xref\n0 10\n0000000000 65535 f \n";
    for (std::size_t i = 1; i <= 9U; ++i) {
        pdf << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    pdf << "trailer\n<< /Size 10 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    const auto output = TempPath("pdfpp_feature_shading.pdf");
    {
        std::ofstream file(output, std::ios::binary);
        file << pdf.str();
    }
    const auto document = PdfDocument::Open(output);
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    PDFPP_TEST_CHECK(bitmap.GetWidth() == 100U);
    PDFPP_TEST_CHECK(bitmap.GetHeight() == 100U);
    // The axial shading goes blue -> red across the page; the left edge is
    // blue-dominant and the right edge red-dominant.
    const auto leftPixel = bitmap.GetPixel(5U, 50U);
    const auto rightPixel = bitmap.GetPixel(95U, 50U);
    PDFPP_TEST_CHECK(leftPixel.blue > leftPixel.red);
    PDFPP_TEST_CHECK(rightPixel.red > rightPixel.blue);
    std::filesystem::remove(output);
}

void TestTilingPatternRendering() {
    // A page filled with a tiling pattern: the pattern is a 20x20 tile whose
    // content fills a 10x10 square at its top-left with red, so the page shows
    // alternating red squares on white.
    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    std::array<std::size_t, 6> offsets{};
    const auto object = [&](const std::size_t number, const std::string& body) {
        offsets[number] = static_cast<std::size_t>(pdf.tellp());
        pdf << number << " 0 obj\n" << body << "\nendobj\n";
    };
    object(1, "<< /Type /Catalog /Pages 2 0 R >>");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    object(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
              "/Resources << /Pattern << /P1 4 0 R >> >> /Contents 5 0 R >>");
    const std::string tileContent = "1 0 0 rg 0 0 10 10 re f";
    object(4, "<< /Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
              "/BBox [0 0 20 20] /XStep 20 /YStep 20 /Resources << >> /Length "
              + std::to_string(tileContent.size()) + " >>\nstream\n"
              + tileContent + "endstream");
    const std::string content = "/Pattern cs /P1 scn 0 0 100 100 re f";
    object(5, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n"
              + content + "endstream");
    const std::size_t xrefOffset = static_cast<std::size_t>(pdf.tellp());
    pdf << "xref\n0 6\n0000000000 65535 f \n";
    for (std::size_t i = 1; i <= 5U; ++i) {
        pdf << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    pdf << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    const auto output = TempPath("pdfpp_feature_tiling.pdf");
    {
        std::ofstream file(output, std::ios::binary);
        file << pdf.str();
    }
    const auto document = PdfDocument::Open(output);
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    PDFPP_TEST_CHECK(bitmap.GetWidth() == 100U);
    PDFPP_TEST_CHECK(bitmap.GetHeight() == 100U);
    // Tile 0,0 covers [0,0]-[10,10] with red; tile 0,1 covers [10,0]-[20,10]
    // (offset by 20 in x) so [10,10] area is white.
    const auto redPixel = bitmap.GetPixel(5U, 5U);
    PDFPP_TEST_CHECK(redPixel.red > 200U && redPixel.green < 60U);
    const auto whitePixel = bitmap.GetPixel(15U, 5U);
    PDFPP_TEST_CHECK(whitePixel.red > 200U && whitePixel.green > 200U);
    const auto redFar = bitmap.GetPixel(25U, 25U);
    PDFPP_TEST_CHECK(redFar.red > 200U && redFar.green < 60U);
    std::filesystem::remove(output);
}

void TestSeparationAndDeviceNRendering() {
    // Separation image: a 2x1 image whose single tint is transformed by a
    // Type 2 function into DeviceRGB alternate. tint 0 -> C0 [0 0 1] (blue),
    // tint 1 -> C1 [1 0 0] (red). The renderer must apply the function (not
    // multiply the transformed value by tint again).
    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    std::array<std::size_t, 8> offsets{};
    const auto object = [&](const std::size_t number, const std::string& body) {
        offsets[number] = static_cast<std::size_t>(pdf.tellp());
        pdf << number << " 0 obj\n" << body << "\nendobj\n";
    };
    object(1, "<< /Type /Catalog /Pages 2 0 R >>");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    object(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
              "/Resources << /XObject << /Im1 4 0 R >> >> /Contents 5 0 R >>");
    const std::string imageBytes = "\x10\xFF";
    object(4, "<< /Type /XObject /Subtype /Image /Width 2 /Height 1 "
              "/ColorSpace [ /Separation /Spot /DeviceRGB 6 0 R ] "
              "/BitsPerComponent 8 /Length " + std::to_string(imageBytes.size()) + " >>\nstream\n"
              + imageBytes + "\nendstream");
    const std::string content = "q 0 0 100 100 cm /Im1 Do Q";
    object(5, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n"
              + content + "endstream");
    object(6, "<< /FunctionType 2 /C0 [0 0 1] /C1 [1 0 0] /N 1 >>");
    const std::size_t xrefOffset = static_cast<std::size_t>(pdf.tellp());
    pdf << "xref\n0 7\n0000000000 65535 f \n";
    for (std::size_t i = 1; i <= 6U; ++i) {
        pdf << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    pdf << "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    const auto output = TempPath("pdfpp_feature_separation.pdf");
    {
        std::ofstream file(output, std::ios::binary);
        file << pdf.str();
    }
    const auto document = PdfDocument::Open(output);
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    PDFPP_TEST_CHECK(bitmap.GetWidth() == 100U);
    PDFPP_TEST_CHECK(bitmap.GetHeight() == 100U);
    // tint 0 -> blue, tint 1 -> red.
    const auto bluePixel = bitmap.GetPixel(10U, 25U);
    PDFPP_TEST_CHECK(bluePixel.blue > bluePixel.red && bluePixel.blue > 200U);
    const auto redPixel = bitmap.GetPixel(90U, 25U);
    PDFPP_TEST_CHECK(redPixel.red > redPixel.blue && redPixel.red > 200U);
    std::filesystem::remove(output);
}

void TestIccBasedRendering() {
    // ICCBased image with an N=3 (RGB) profile: identity rendering keeps the
    // decoded samples, so a red/blue 2x1 image stays red/blue.
    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    std::array<std::size_t, 7> offsets{};
    const auto object = [&](const std::size_t number, const std::string& body) {
        offsets[number] = static_cast<std::size_t>(pdf.tellp());
        pdf << number << " 0 obj\n" << body << "\nendobj\n";
    };
    object(1, "<< /Type /Catalog /Pages 2 0 R >>");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    object(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
              "/Resources << /XObject << /Im1 4 0 R >> >> /Contents 5 0 R >>");
    const std::string imageBytes = "\xFF\x10\x10\x10\x10\xFF";
    object(4, "<< /Type /XObject /Subtype /Image /Width 2 /Height 1 "
              "/ColorSpace [ /ICCBased 6 0 R ] "
              "/BitsPerComponent 8 /Length " + std::to_string(imageBytes.size()) + " >>\nstream\n"
              + imageBytes + "\nendstream");
    const std::string content = "q 0 0 100 100 cm /Im1 Do Q";
    object(5, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n"
              + content + "endstream");
    const std::string profile = "ICC profile placeholder";
    object(6, "<< /N 3 /Length " + std::to_string(profile.size()) + " >>\nstream\n"
              + profile + "\nendstream");
    const std::size_t xrefOffset = static_cast<std::size_t>(pdf.tellp());
    pdf << "xref\n0 7\n0000000000 65535 f \n";
    for (std::size_t i = 1; i <= 6U; ++i) {
        pdf << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    pdf << "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    const auto output = TempPath("pdfpp_feature_iccbased.pdf");
    {
        std::ofstream file(output, std::ios::binary);
        file << pdf.str();
    }
    const auto document = PdfDocument::Open(output);
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    PDFPP_TEST_CHECK(bitmap.GetWidth() == 100U);
    PDFPP_TEST_CHECK(bitmap.GetHeight() == 100U);
    const auto redPixel = bitmap.GetPixel(10U, 25U);
    PDFPP_TEST_CHECK(redPixel.red > 200U && redPixel.green < 60U);
    const auto bluePixel = bitmap.GetPixel(90U, 25U);
    PDFPP_TEST_CHECK(bluePixel.blue > 200U && bluePixel.red < 60U);
    std::filesystem::remove(output);
}

void TestIccSrgbGammaRendering() {
    // A minimal sRGB ICC profile (header + rTRC 'curv' gamma 2.2) triggers the
    // renderer's gamma re-encode: a 128 (mid) sample becomes ~210.
    std::vector<std::byte> profile(180U, std::byte{0});
    const auto put = [&](const std::size_t offset, const std::uint32_t value) {
        profile[offset] = std::byte((value >> 24U) & 0xFFU);
        profile[offset + 1U] = std::byte((value >> 16U) & 0xFFU);
        profile[offset + 2U] = std::byte((value >> 8U) & 0xFFU);
        profile[offset + 3U] = std::byte(value & 0xFFU);
    };
    // Header: profile class 'sRGB' (0), color space 'RGB ' (16), PCS 'XYZ ' (20).
    put(0U, 0x73524742U);  // 'sRGB'
    put(16U, 0x52474220U); // 'RGB '
    put(20U, 0x58595A20U); // 'XYZ '
    // Tag table: 1 tag, entry {sig 'rTRC', offset 160, size 12}.
    put(128U, 1U);
    put(132U, 0x72545243U); // 'rTRC'
    put(136U, 160U);
    put(140U, 12U);
    // rTRC 'curv' with 0 entries then u8Fixed8 gamma 2.2 (0x0233).
    put(160U, 0x63757276U); // 'curv'
    put(164U, 0U);          // entry count = 0
    profile[168] = std::byte{0x02};
    profile[169] = std::byte{0x33};
    const std::string profileBytes(reinterpret_cast<const char*>(profile.data()), profile.size());

    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    std::array<std::size_t, 7> offsets{};
    const auto object = [&](const std::size_t number, const std::string& body) {
        offsets[number] = static_cast<std::size_t>(pdf.tellp());
        pdf << number << " 0 obj\n" << body << "\nendobj\n";
    };
    object(1, "<< /Type /Catalog /Pages 2 0 R >>");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    object(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
              "/Resources << /XObject << /Im1 4 0 R >> >> /Contents 5 0 R >>");
    const std::string imageBytes = "\x80\x80\x80";
    object(4, "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 "
              "/ColorSpace [ /ICCBased 6 0 R ] "
              "/BitsPerComponent 8 /Length " + std::to_string(imageBytes.size()) + " >>\nstream\n"
              + imageBytes + "\nendstream");
    const std::string content = "q 0 0 100 100 cm /Im1 Do Q";
    object(5, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n"
              + content + "endstream");
    object(6, "<< /N 3 /Length " + std::to_string(profileBytes.size()) + " >>\nstream\n"
              + profileBytes + "\nendstream");
    const std::size_t xrefOffset = static_cast<std::size_t>(pdf.tellp());
    pdf << "xref\n0 7\n0000000000 65535 f \n";
    for (std::size_t i = 1; i <= 6U; ++i) {
        pdf << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    pdf << "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    const auto output = TempPath("pdfpp_feature_icc_gamma.pdf");
    {
        std::ofstream file(output, std::ios::binary);
        file << pdf.str();
    }
    const auto document = PdfDocument::Open(output);
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    const auto pixel = bitmap.GetPixel(50U, 25U);
    // 128 raised to 1/2.2 then scaled -> ~210, distinctly above identity 128.
    PDFPP_TEST_CHECK(pixel.red > 170U && pixel.red < 245U);
    std::filesystem::remove(output);
}

void TestOptionalContentLayers() {
    const auto output = TempPath("pdfpp_feature_layers.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 400, 400});

    const std::size_t index = writer.AddOptionalContentGroup(PdfOcgOptions{"Background map"});
    PDFPP_TEST_CHECK(index == 0U);
    PDFPP_TEST_CHECK(writer.GetOptionalContentGroupCount() == 1U);
    writer.AddOptionalContentGroup(PdfOcgOptions{"Annotations", false});
    PDFPP_TEST_CHECK(writer.GetOptionalContentGroupCount() == 2U);
    // Registering the same name reuses the existing group.
    PDFPP_TEST_CHECK(writer.AddOptionalContentGroup(PdfOcgOptions{"Background map"}) == 0U);
    PDFPP_TEST_CHECK(writer.GetOptionalContentGroupCount() == 2U);

    writer.GetCanvas(page).BeginLayer("Background map")
        .SaveState().SetFillColor(PdfColor::Gray(0.9))
        .FillRectangle(10, 10, 380, 380).RestoreState()
        .EndLayer();
    ExpectThrows([&] {
        writer.GetCanvas(page).BeginLayer("missing-layer");
    });
    writer.ClearOptionalContentGroups();
    PDFPP_TEST_CHECK(writer.GetOptionalContentGroupCount() == 0U);

    // Rebuild with a layer so the saved PDF carries the OCG structure.
    writer.AddOptionalContentGroup(PdfOcgOptions{"Visible layer"});
    writer.AddOptionalContentGroup(PdfOcgOptions{"Hidden layer", false});
    writer.GetCanvas(page).BeginLayer("Visible layer")
        .BeginText().SetFontAndSize("Helvetica", 12).MoveText(30, 360)
        .ShowText("Layer one content").EndText()
        .EndLayer();
    writer.Save(output);
    const std::string bytes = ReadText(output);
    PDFPP_TEST_CHECK(bytes.find("/OCProperties") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/Type /OCG") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/Properties") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/OC1 BDC") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("EMC") != std::string::npos);

    auto document = PdfDocument::Open(output);
    PDFPP_TEST_CHECK(document.GetPageCount() == 1U);
    const PdfDictionary* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    PDFPP_TEST_CHECK(catalog != nullptr);
    PDFPP_TEST_CHECK(catalog->Contains(PdfName("OCProperties")));
    std::filesystem::remove(output);
}

void TestPageMutationRemapsDependentFeatures() {
    PdfWriter writer;
    (void)writer.AddPage();
    (void)writer.AddPage();
    (void)writer.AddPage();

    PdfDestinationOptions destination;
    destination.pageIndex = 2;
    writer.AddNamedDestination("last", destination);
    writer.SetOpenAction(destination);

    PdfBookmarkOptions bookmark;
    bookmark.title = "Last";
    bookmark.pageIndex = 2;
    (void)writer.AddBookmark(bookmark);

    PdfPageLabelOptions label;
    writer.AddPageLabel(2, label);
    (void)writer.InsertPage(1);
    PDFPP_TEST_CHECK(writer.GetNamedDestinationCount() == 1);
    PDFPP_TEST_CHECK(writer.HasOpenAction());
    PDFPP_TEST_CHECK(writer.GetBookmarkCount() == 1);
    PDFPP_TEST_CHECK(writer.GetPageLabelCount() == 1);

    writer.RemovePage(3);
    PDFPP_TEST_CHECK(writer.GetNamedDestinationCount() == 0);
    PDFPP_TEST_CHECK(!writer.HasOpenAction());
    PDFPP_TEST_CHECK(writer.GetBookmarkCount() == 0);
}

void TestRegexSearchOptionsAndGeometry() {
    std::vector<PdfTextChunk> chunks{
        {"INV-", {0, 100}, {20, 100}, {0, 95, 20, 105}},
        {"2026", {20, 100}, {45, 100}, {20, 95, 45, 105}},
        {"\n", {0, 80}, {0, 80}, {0, 80, 0, 80}},
        {"Total: 125.50", {0, 60}, {75, 60}, {0, 55, 75, 65}}
    };
    PdfRegexSearchOptions options;
    options.caseInsensitive = false;
    options.allowAcrossLineBreaks = false;
    options.optimize = true;
    const auto invoice = PdfTextSearch::FindRegex(chunks, R"(INV-\d{4})", options);
    PDFPP_TEST_CHECK(invoice.size() == 1);
    const std::regex compiledInvoice(R"(INV-\d{4})", std::regex_constants::ECMAScript);
    const auto compiledMatches = PdfTextSearch::FindRegex(chunks, compiledInvoice, options);

    PdfTextSearchIndex reusableIndex(chunks);
    PDFPP_TEST_CHECK(reusableIndex.GetChunkCount() == chunks.size());
    PDFPP_TEST_CHECK(reusableIndex.GetSearchableByteCount() >= 1U);
    PDFPP_TEST_CHECK(reusableIndex.Find("INV-2026").size() == 1U);
    PDFPP_TEST_CHECK(reusableIndex.FindRegex(compiledInvoice, options).size() == 1U);
    PDFPP_TEST_CHECK(compiledMatches.size() == 1);
    PDFPP_TEST_CHECK(compiledMatches.front().matchedText == invoice.front().matchedText);
    PDFPP_TEST_CHECK(invoice.front().rectangles.size() == 2);
    PDFPP_TEST_CHECK(invoice.front().boundingBox.right == 45);

    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(chunks, R"(2026\s+Total)", options).empty());
    options.allowAcrossLineBreaks = true;
    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(chunks, R"(2026\s+Total)", options).size() == 1);
    options.maxMatches = 0;
    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(chunks, R"(\d+)", options).size() >= 2);
}

void TestTextLayoutAndFallback() {
    // Grapheme clustering: base + combining mark stays together.
    const auto clusters = PdfTextLayout::GraphemeClusters("a\xCC\x81" "bc");
    PDFPP_TEST_CHECK(clusters.size() == 3U);
    PDFPP_TEST_CHECK(clusters[0] == "a\xCC\x81");

    // Bidi reordering: LTR base keeps order; an embedded Hebrew run is reversed.
    const auto latin = PdfTextLayout::ReorderBidi("ABC");
    PDFPP_TEST_CHECK(latin == "ABC");
    const auto hebrew = PdfTextLayout::ReorderBidi("\xD7\x90\xD7\x91"); // alef bet (RTL)
    PDFPP_TEST_CHECK(hebrew == "\xD7\x91\xD7\x90"); // visual order reversed
    // Mixed: "ab<hebrew>" -> Hebrew reversed but Latin prefix order kept.
    const auto mixed = PdfTextLayout::ReorderBidi("ab\xD7\x90\xD7\x91");
    PDFPP_TEST_CHECK(mixed.find("ab") == 0U);
    // Grapheme clusters with an emoji + combining mark.
    const auto emoji = PdfTextLayout::GraphemeClusters("a\xF0\x9F\x98\x80" "b");
    PDFPP_TEST_CHECK(emoji.size() == 3U);

    // Arabic shaping: "ب" (U+0628) followed by "ت" (U+062A) joins into their
    // presentation forms (initial ب FE91, final ت FE96).
    const auto shaped = PdfTextLayout::ShapeArabic("\xD8\xA8\xD8\xAA");
    const std::string expected = "\xEF\xBA\x91" "\xEF\xBA\x96"; // FE91 + FE96
    PDFPP_TEST_CHECK(shaped == expected);

    // Kerning: fonts with a `kern` table expose pair adjustments; Helvetica-less
    // test avoids font-file dependence, so exercise the API shape via the
    // Windows fonts when available.
    const std::array<std::filesystem::path, 3> candidates{
        std::filesystem::path("C:/Windows/Fonts/arial.ttf"),
        std::filesystem::path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        std::filesystem::path("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf")};
    const auto fontIt = std::find_if(candidates.begin(), candidates.end(),
        [](const auto& path) { return std::filesystem::exists(path); });
    if (fontIt != candidates.end()) {
        const auto font = PdfTrueTypeFont::Load(*fontIt);
        const auto gidA = font.GetGlyphId(U'A');
        const auto gidV = font.GetGlyphId(U'V');
        if (gidA && gidV) {
            const double kern = font.GetKerning(*gidA, *gidV, 100.0);
            PDFPP_TEST_CHECK(std::abs(kern) < 100.0);
            PDFPP_TEST_CHECK(font.GetCachedKerning(*gidA, *gidV, 100.0) == kern);
            // Arial and DejaVu place "AV" pairs in the GPOS table; ensure the
            // parser merged them into the kern store.
            PDFPP_TEST_CHECK(kern != 0.0);
        }
        // Ligature substitution: fi -> fi ligature glyph when the font has GSUB.
        const auto gidF = font.GetGlyphId(U'f');
        const auto gidI = font.GetGlyphId(U'i');
        if (gidF && gidI && font.HasLigatures()) {
            const std::array<std::uint16_t, 2> pair{*gidF, *gidI};
            const auto substituted = font.ApplyLigatures(pair);
            PDFPP_TEST_CHECK(substituted.size() == 1U);
            PDFPP_TEST_CHECK(substituted[0] != *gidF);
            PDFPP_TEST_CHECK(font.GetLigatureCount() > 0U);
        }
        // GPOS mark positioning: combining acute (U+0301) attaches to "e".
        if (font.HasMarkBase()) {
            const auto gidE = font.GetGlyphId(U'e');
            const auto gidAcute = font.GetGlyphId(U'\u0301');
            if (gidE && gidAcute) {
                const auto attachment = font.GetMarkBasePosition(*gidAcute, *gidE);
                PDFPP_TEST_CHECK(attachment.has_value());
                if (attachment) {
                    PDFPP_TEST_CHECK(attachment->markX >= -1000);
                    PDFPP_TEST_CHECK(attachment->markY >= -1000);
                }
            }
            PDFPP_TEST_CHECK(font.GetMarkBaseCount() > 0U);
        }
        // Render base + combining mark with GPOS positioning active.
        if (font.HasMarkBase()) {
            const auto markPdf = TempPath("pdfpp_feature_render_mark.pdf");
            PdfWriter writer;
            const auto page = writer.AddPage({0, 0, 200, 100});
            writer.GetCanvas(page).BeginText().SetTrueTypeFontAndSize(font, 40)
                .MoveText(20, 50).ShowTextUtf8("e\u0301").EndText();
            writer.Save(markPdf);
            const auto document = PdfDocument::Open(markPdf);
            PdfRenderOptions options;
            options.dpi = 72.0;
            const auto bitmap = PdfPageRenderer::Render(document, 0U, options);
            PDFPP_TEST_CHECK(bitmap.GetWidth() == 200U);
            std::filesystem::remove(markPdf);
        }
        // Kerning applied at render time: "AV" and "AA" produce different pixels.
        if (gidA && gidV && font.HasKerning()) {            const auto kernPdf = TempPath("pdfpp_feature_render_kern.pdf");
            PdfWriter writer;
            const auto page = writer.AddPage({0, 0, 200, 100});
            writer.GetCanvas(page).BeginText().SetTrueTypeFontAndSize(font, 40)
                .MoveText(20, 50).ShowTextUtf8("AV").EndText();
            writer.Save(kernPdf);
            const auto document = PdfDocument::Open(kernPdf);
            PdfRenderOptions options;
            options.dpi = 72.0;
            const auto bitmap = PdfPageRenderer::Render(document, 0U, options);
            PDFPP_TEST_CHECK(bitmap.GetWidth() == 200U);
            std::filesystem::remove(kernPdf);
        }
        // Fallback: primary font without 'Ω' would fall back; here both fonts
        // support it, so the round trip still works.
        const auto output = TempPath("pdfpp_feature_fallback.pdf");
        PdfWriter writer;
        const auto page = writer.AddPage({0, 0, 300, 300});
        const std::array<PdfTrueTypeFont, 1> fallbacks{font};
        writer.GetCanvas(page).BeginText().SetTrueTypeFontAndSize(font, 12)
            .MoveText(20, 250).ShowTextUtf8WithFallback("AB", fallbacks).EndText();
        // Vertical writing produces a rotated text matrix.
        writer.GetCanvas(page).BeginText().SetTrueTypeFontAndSize(font, 12)
            .MoveText(60, 250).ShowTextVertical("vertical").EndText();
        PDFPP_TEST_CHECK(!writer.GetCanvas(page).IsVerticalWriting());
        writer.GetCanvas(page).SetVerticalWriting(true);
        PDFPP_TEST_CHECK(writer.GetCanvas(page).IsVerticalWriting());
        writer.GetCanvas(page).SetVerticalWriting(false);
        PDFPP_TEST_CHECK(!writer.GetCanvas(page).IsVerticalWriting());
        writer.Save(output);
        const auto document = PdfDocument::Open(output);
        PDFPP_TEST_CHECK(document.GetPageCount() == 1U);
        std::filesystem::remove(output);
    }
}

void TestDocumentLayoutPrimitives() {
    const auto output = TempPath("pdfpp_feature_document_layout.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 400, 500});
    (void)page;
    PdfDocumentLayout layout(writer);
    layout.DrawList(0U, {"First", "Second", "Third"}, 40.0, 460.0,
        PdfDocumentLayout::ListOptions{PdfDocumentLayout::ListStyle::Decimal});
    layout.DrawColumns(0U, {"Column A text", "Column B text"},
        PdfRectangle{40, 40, 360, 400}, 16.0);
    layout.DrawTable(0U, {"Name", "Value"},
        {{"Alpha", "1"}, {"Beta", "2"}}, PdfRectangle{40, 300, 300, 380});
    // Flow long paragraphs across pages.
    std::vector<PdfDocumentLayout::ParagraphOptions> flow;
    for (int i = 0; i < 6; ++i) {
        PdfDocumentLayout::ParagraphOptions p;
        p.text = "Paragraph number " + std::to_string(i + 1) +
                 " with enough text to wrap across the available width repeatedly.";
        p.fontSize = 11.0;
        flow.push_back(p);
    }
    layout.FlowParagraphs(flow, PdfRectangle{40, 40, 360, 260});
    layout.DrawHeader(0U, 0U, PdfRectangle{0, 0, 400, 500},
        PdfDocumentLayout::HeaderFooterOptions{"", "", "Report"});
    layout.DrawFooter(0U, 0U, PdfRectangle{0, 0, 400, 500},
        PdfDocumentLayout::HeaderFooterOptions{"", "Page", "", 9.0, true, true, 36.0, 36.0});
    writer.Save(output);
    const auto document = PdfDocument::Open(output);
    PDFPP_TEST_CHECK(document.GetPageCount() >= 1U);
    const std::string text = document.GetPageText(0U);
    PDFPP_TEST_CHECK(text.find("First") != std::string::npos);
    PDFPP_TEST_CHECK(text.find("Column A text") != std::string::npos);
    PDFPP_TEST_CHECK(text.find("Report") != std::string::npos);
    PDFPP_TEST_CHECK(text.find("1") != std::string::npos); // page number
    std::filesystem::remove(output);
}


void TestRedaction() {
    const auto input = TempPath("pdfpp_feature_redact_input.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 300, 300});
    writer.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 12)
        .MoveText(30, 200).ShowText("SECRET code is hidden").EndText();
    writer.Save(input);

    const auto output = TempPath("pdfpp_feature_redact_output.pdf");
    const auto result = PdfRedactor::RedactText(input, output,
        {PdfRedactor::RedactionRequest{0U, "SECRET", {}}});
    PDFPP_TEST_CHECK(result.redactionCount >= 1U);
    PDFPP_TEST_CHECK(result.modifiedPageCount == 1U);
    const auto document = PdfDocument::Open(output);
    PDFPP_TEST_CHECK(document.GetPageCount() == 1U);
    const std::string pageBytes = document.readIndirectObject(document.GetPageReference(0U).objectNumber);
    PDFPP_TEST_CHECK(pageBytes.find("/Contents") != std::string::npos);
    std::filesystem::remove(input);
    std::filesystem::remove(output);
}

void TestParallelRendering() {
    const auto output = TempPath("pdfpp_feature_parallel.pdf");
    PdfWriter writer;
    for (int p = 0; p < 3; ++p) {
        const auto page = writer.AddPage({0, 0, 120, 120});
        writer.GetCanvas(page).SaveState()
            .SetFillColor(PdfColor::FromRgb(0.2 + 0.2 * p, 0.1, 0.1))
            .FillRectangle(10, 10, 40, 40).RestoreState()
            .BeginText().SetFontAndSize("Helvetica", 10).MoveText(10, 100)
            .ShowText("P" + std::to_string(p + 1)).EndText();
    }
    writer.Save(output);
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto parallel = PdfPageRenderer::RenderAllPagesParallel(output, options, 0U);
    PDFPP_TEST_CHECK(parallel.size() == 3U);
    for (std::size_t i = 0U; i < parallel.size(); ++i) {
        PDFPP_TEST_CHECK(parallel[i].pageIndex == i);
        PDFPP_TEST_CHECK(parallel[i].bitmap.GetWidth() == 120U);
        PDFPP_TEST_CHECK(parallel[i].bitmap.GetHeight() == 120U);
    }
    // The parallel result must match a sequential render pixel-for-pixel.
    const auto document = PdfDocument::Open(output);
    for (std::size_t i = 0U; i < parallel.size(); ++i) {
        const auto sequential = PdfPageRenderer::Render(document, i, options);
        PDFPP_TEST_CHECK(sequential.GetWidth() == parallel[i].bitmap.GetWidth());
        PDFPP_TEST_CHECK(sequential.GetHeight() == parallel[i].bitmap.GetHeight());
        const auto& a = sequential.GetPixel(20U, 20U);
        const auto& b = parallel[i].bitmap.GetPixel(20U, 20U);
        PDFPP_TEST_CHECK(a.red == b.red && a.green == b.green && a.blue == b.blue);
    }
    std::filesystem::remove(output);
}

void TestJpxImageWrite() {
    // Build a minimal JPEG 2000 codestream header (SOC + SIZ) for a 2x1 image
    // and write it through the writer; the output must carry /JPXDecode.
    const std::vector<std::byte> jpx = {
        std::byte{0xFF}, std::byte{0x4F}, std::byte{0xFF}, std::byte{0x51}, // SOC
        std::byte{0xFF}, std::byte{0x51},                                 // SIZ
        std::byte{0x00}, std::byte{0x29},                                 // Lsiz = 41
        std::byte{0x00}, std::byte{0x00},                                 // Rsiz
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, // Xsiz = 2
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, // Ysiz = 1
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // XOsiz
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // YOsiz
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, // XTsiz
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, // YTsiz
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // XTOsiz
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // YTOsiz
        std::byte{0x00}, std::byte{0x03},                                 // Csiz = 3 components
        std::byte{0x01}, std::byte{0x01}, std::byte{0x00},                 // Ssiz
        std::byte{0x01}, std::byte{0x01}, std::byte{0x00},
        std::byte{0x01}, std::byte{0x01}, std::byte{0x00},
    };
    const auto image = PdfImage::FromJpeg2000(2U, 1U, jpx);
    PDFPP_TEST_CHECK(image.GetEncoding() == PdfImageEncoding::Jpx);
    const auto output = TempPath("pdfpp_feature_jpx.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 100, 100});
    writer.GetCanvas(page).DrawImage(image, {10, 10, 50, 30});
    writer.Save(output);
    const std::string bytes = ReadText(output);
    PDFPP_TEST_CHECK(bytes.find("/JPXDecode") != std::string::npos);
    auto document = PdfDocument::Open(output);
    const auto images = document.ExtractImages(0U);
    PDFPP_TEST_CHECK(!images.empty());
    PDFPP_TEST_CHECK(images[0].info.encoding == PdfImageEncoding::Jpx);
    std::filesystem::remove(output);

    // CCITT G4 encoding produces a non-empty payload for a 1-bit image.
    const std::vector<std::byte> oneBit{std::byte{0xAA}, std::byte{0x55}};
    const auto g4 = PdfImage::EncodeCcittG4(16U, 1U, oneBit);
    PDFPP_TEST_CHECK(!g4.empty());
    const auto g4decoded = PdfImage::DecodeCcittG4(16U, 1U, g4);
    PDFPP_TEST_CHECK(!g4decoded.empty());

    // JPEG encoder: a small RGB image round-trips through FromJpeg dimensions.
    std::vector<std::byte> rgb(8 * 8 * 3U, std::byte{0x80});
    const auto jpeg = PdfImage::EncodeJpeg(8U, 8U, rgb, 85);
    PDFPP_TEST_CHECK(!jpeg.empty());
    const auto decoded = PdfImage::FromJpeg(jpeg);
    PDFPP_TEST_CHECK(decoded.GetWidth() == 8U);
    PDFPP_TEST_CHECK(decoded.GetHeight() == 8U);
    PDFPP_TEST_CHECK(decoded.GetEncoding() == PdfImageEncoding::Dct);

    // Indexed palette optimization: a 2-color RGB image writes /Indexed.
    std::vector<std::byte> twoColor(4 * 4 * 3U, std::byte{0});
    for (std::size_t i = 0; i < twoColor.size(); i += 3U) twoColor[i] = std::byte{0xFF};
    const auto indexedImage = PdfImage::FromRgb(4U, 4U, twoColor);
    const auto indexedPdf = TempPath("pdfpp_feature_indexed.pdf");
    PdfWriter indexedWriter;
    const auto indexedPage = indexedWriter.AddPage({0, 0, 100, 100});
    indexedWriter.GetCanvas(indexedPage).DrawImage(indexedImage, {10, 10, 50, 50});
    indexedWriter.Save(indexedPdf);
    const std::string indexedBytes = ReadText(indexedPdf);
    PDFPP_TEST_CHECK(indexedBytes.find("/Indexed") != std::string::npos);
    auto indexedDocument = PdfDocument::Open(indexedPdf);
    const auto indexedImages = indexedDocument.ExtractImages(0U);
    PDFPP_TEST_CHECK(!indexedImages.empty());
    std::filesystem::remove(indexedPdf);
}

void TestType1FontEmbedding() {
    // Build a minimal PFB: segment header + ASCII Type1 program with /FontName.
    std::vector<std::byte> pfb;
    const std::string program =
        "%!FontType1-1.0: TestType1 001.000\n"
        "/FontName /TestType1 def\n"
        "/FontType 1 def\n"
        "/PaintType 0 def\n"
        "/FontMatrix [0.001 0 0 0.001 0 0] readonly def\n";
    pfb.push_back(std::byte{0x80}); pfb.push_back(std::byte{1});
    const std::uint32_t len = static_cast<std::uint32_t>(program.size());
    pfb.push_back(static_cast<std::byte>(len & 0xFFU));
    pfb.push_back(static_cast<std::byte>((len >> 8) & 0xFFU));
    pfb.push_back(static_cast<std::byte>((len >> 16) & 0xFFU));
    pfb.push_back(static_cast<std::byte>((len >> 24) & 0xFFU));
    for (const char c : program) pfb.push_back(static_cast<std::byte>(c));
    // End segment.
    pfb.push_back(std::byte{0x80}); pfb.push_back(std::byte{3});
    pfb.push_back(std::byte{0}); pfb.push_back(std::byte{0});
    pfb.push_back(std::byte{0}); pfb.push_back(std::byte{0});

    std::vector<std::uint8_t> pfbBytes(pfb.size());
    for (std::size_t i = 0; i < pfb.size(); ++i) pfbBytes[i] = std::to_integer<std::uint8_t>(pfb[i]);
    const auto font = PdfType1Font::Parse(std::move(pfbBytes), "test.pfb");
    PDFPP_TEST_CHECK(font.GetFontName() == "TestType1");
    PDFPP_TEST_CHECK(font.GetGlyphWidth('A') > 0U);

    const auto output = TempPath("pdfpp_feature_type1.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 200, 200});
    writer.GetCanvas(page).BeginText().SetType1FontAndSize(font, 12)
        .MoveText(20, 150).ShowType1Text("Type1 text").EndText();
    writer.Save(output);
    const std::string bytes = ReadText(output);
    PDFPP_TEST_CHECK(bytes.find("/Subtype /Type1") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/FontFile") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/WinAnsiEncoding") != std::string::npos);
    auto document = PdfDocument::Open(output);
    PDFPP_TEST_CHECK(document.GetPageCount() == 1U);
    std::filesystem::remove(output);
}

void TestCffFontEmbedding() {
    const auto cffBytes = CPPPdfTest::BuildMinimalCff();
    const auto font = PdfCffParser::ParseFont(std::span<const std::byte>(cffBytes));
    PDFPP_TEST_CHECK(font.name == "Test");
    const auto output = TempPath("pdfpp_feature_cff.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 200, 200});
    writer.GetCanvas(page).BeginText().SetEmbeddedCffFontAndSize(font, 12)
        .MoveText(20, 150).ShowType1Text("CFF").EndText();
    writer.Save(output);
    const std::string bytes = ReadText(output);
    PDFPP_TEST_CHECK(bytes.find("/FontFile3") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/Type1C") != std::string::npos);
    auto document = PdfDocument::Open(output);
    PDFPP_TEST_CHECK(document.GetPageCount() == 1U);
    std::filesystem::remove(output);
}

void TestPolygonAndBezierPaths() {
    const auto output = TempPath("pdfpp_feature_paths.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 200, 200});
    auto canvas = writer.GetCanvas(page);
    const std::vector<PdfPoint> triangle{{10, 10}, {100, 10}, {55, 90}};
    const double dashPattern[] = {4.0, 2.0};
    canvas.SetLineWidth(2.0).SetLineCap(PdfLineCap::Round).SetLineJoin(PdfLineJoin::Round);
    canvas.SetLineDash(dashPattern, 0.0);
    canvas.FillPolygon(triangle);
    canvas.DrawPolygon(triangle);
    canvas.DrawBezier(10, 10, 50, 100, 100, -50, 150, 60);
    canvas.DrawCircle(100, 100, 40);
    canvas.FillEllipse(40, 150, 25, 12);
    canvas.SaveState().EndPath().RestoreState();
    writer.Save(output);
    auto document = PdfDocument::Open(output);
    PDFPP_TEST_CHECK(document.GetPageCount() == 1U);
    // The page must render (paths + bezier) without errors.
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0U, options);
    PDFPP_TEST_CHECK(bitmap.GetWidth() == 200U);
    PDFPP_TEST_CHECK(bitmap.GetHeight() == 200U);
    std::filesystem::remove(output);
}

void TestPngOutput() {
    const auto pdfPath = TempPath("pdfpp_feature_png_src.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 40, 30});
    writer.GetCanvas(page).SetFillColor(PdfColor{1.0, 0.0, 0.0}).FillRectangle(0, 0, 40, 30);
    writer.Save(pdfPath);
    const auto document = PdfDocument::Open(pdfPath);
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0U, options);
    const auto pngPath = TempPath("pdfpp_feature_out.png");
    bitmap.SavePng(pngPath);
    const std::string bytes = ReadText(pngPath);
    PDFPP_TEST_CHECK(bytes.size() > 8U);
    const std::string signature = bytes.substr(0, 8);
    PDFPP_TEST_CHECK(signature[0] == static_cast<char>(0x89) && signature[1] == 'P' &&
                     signature[2] == 'N' && signature[3] == 'G');
    PDFPP_TEST_CHECK(bytes.find("IHDR") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("IDAT") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("IEND") != std::string::npos);
    const auto jpegPath = TempPath("pdfpp_feature_out.jpg");
    bitmap.SaveJpeg(jpegPath, 90);
    const std::string jpegBytes = ReadText(jpegPath);
    PDFPP_TEST_CHECK(jpegBytes.size() > 2U);
    PDFPP_TEST_CHECK(static_cast<unsigned char>(jpegBytes[0]) == 0xFFU &&
                     static_cast<unsigned char>(jpegBytes[1]) == 0xD8U);
    // Bitmap manipulation: resize keeps the area, crop and rotate change shape.
    const auto resized = bitmap.Resize(20U, 0U);
    PDFPP_TEST_CHECK(resized.GetWidth() == 20U);
    PDFPP_TEST_CHECK(resized.GetHeight() == 15U);
    const auto cropped = bitmap.Crop(0U, 0U, 10U, 10U);
    PDFPP_TEST_CHECK(cropped.GetWidth() == 10U);
    PDFPP_TEST_CHECK(cropped.GetHeight() == 10U);
    const auto rotated = bitmap.Rotate90(1);
    PDFPP_TEST_CHECK(rotated.GetWidth() == 30U);
    PDFPP_TEST_CHECK(rotated.GetHeight() == 40U);
    std::filesystem::remove(pdfPath);
    std::filesystem::remove(pngPath);
    std::filesystem::remove(jpegPath);
}

void TestTaggedPdf() {
    const auto output = TempPath("pdfpp_feature_tagged.pdf");
    PdfWriter writer;
    writer.AddPage({0, 0, 200, 200});
    writer.SetTaggedPdf(true);
    PDFPP_TEST_CHECK(writer.IsTaggedPdf());
    writer.SetLanguage("en-US");
    PDFPP_TEST_CHECK(writer.GetLanguage() == "en-US");
    writer.Save(output);
    const std::string bytes = ReadText(output);
    PDFPP_TEST_CHECK(bytes.find("/MarkInfo") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/Marked true") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/Lang (en-US)") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/StructTreeRoot") != std::string::npos);
    auto document = PdfDocument::Open(output);
    const PdfDictionary* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    PDFPP_TEST_CHECK(catalog != nullptr);
    PDFPP_TEST_CHECK(catalog->Contains(PdfName("StructTreeRoot")));
    PDFPP_TEST_CHECK(catalog->Contains(PdfName("MarkInfo")));
    std::filesystem::remove(output);
}

void TestPortfolio() {
    const auto output = TempPath("pdfpp_feature_portfolio.pdf");
    PdfWriter writer;
    writer.AddPage({0, 0, 200, 200});
    const std::array<std::byte, 4> fileBytes{std::byte{'A'}, std::byte{'B'}, std::byte{'C'}, std::byte{'D'}};
    writer.AddEmbeddedFile("report.txt", fileBytes);
    PdfPortfolioOptions portfolio;
    portfolio.title = "Project Documents";
    portfolio.view = "T";
    writer.SetPortfolio(portfolio);
    PDFPP_TEST_CHECK(writer.HasPortfolio());
    writer.Save(output);
    const std::string bytes = ReadText(output);
    PDFPP_TEST_CHECK(bytes.find("/Collection") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/View /T") != std::string::npos);
    PDFPP_TEST_CHECK(bytes.find("/Title (Project Documents)") != std::string::npos);
    auto document = PdfDocument::Open(output);
    const PdfDictionary* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    PDFPP_TEST_CHECK(catalog != nullptr);
    PDFPP_TEST_CHECK(catalog->Contains(PdfName("Collection")));
    writer.ClearPortfolio();
    PDFPP_TEST_CHECK(!writer.HasPortfolio());
    std::filesystem::remove(output);
}

void TestDocumentTextIndexMappedInputAndStreamWriter() {
    const auto output = TempPath("pdfpp_feature_document_text_index.pdf");
    PdfWriter writer;
    for (int page = 0; page < 3; ++page) {
        const auto index = writer.AddPage();
        writer.GetCanvas(index).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(30, 700).ShowText("Invoice INV-2026 page " + std::to_string(page + 1)).EndText();
    }
    std::ostringstream memory;
    writer.Save(memory);
    PDFPP_TEST_CHECK(memory.str().find("%PDF-1.7") == 0U);
    writer.Save(output);

    PdfMappedFileInputSource mappedSource(output);
    PDFPP_TEST_CHECK(mappedSource.Size() == std::filesystem::file_size(output));
    auto document = PdfDocument::OpenMapped(output);
    PDFPP_TEST_CHECK(document.GetPageCount() == 3U);

    PdfDocumentTextIndexOptions indexOptions;
    indexOptions.memoryBudgetBytes = 1024U * 1024U;
    indexOptions.maxConcurrency = 2U;
    PdfTextDocumentIndex textIndex(document, indexOptions);
    textIndex.Preload(0U, 3U);
    PDFPP_TEST_CHECK(textIndex.GetPageCount() == 3U);
    PDFPP_TEST_CHECK(textIndex.FindAll("invoice").size() == 3U);
    const std::regex expression(R"(INV-\d{4})", std::regex_constants::ECMAScript);
    PDFPP_TEST_CHECK(textIndex.FindRegexAll(expression).size() == 3U);
    PDFPP_TEST_CHECK(textIndex.GetPageText(1U).find("page 2") != std::string::npos);
    const auto statistics = textIndex.GetStatistics();
    PDFPP_TEST_CHECK(statistics.cachedPages == 3U);
    PDFPP_TEST_CHECK(statistics.cacheMisses >= 3U);
    textIndex.Clear();
    PDFPP_TEST_CHECK(textIndex.GetStatistics().cachedPages == 0U);
    std::filesystem::remove(output);
}


void TestRenderingFoundation() {
    const auto output = TempPath("pdfpp_feature_rendering.pdf");
    const auto ppm = TempPath("pdfpp_feature_rendering.ppm");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 200, 120});
    auto canvas = writer.GetCanvas(page);
    canvas.SetFillColor(PdfColor::FromRgb(0.85, 0.1, 0.1)).FillRectangle(10, 10, 70, 40);
    canvas.SetStrokeColor(PdfColor::FromRgb(0.05, 0.1, 0.8)).SetLineWidth(3.0)
        .Rectangle(90, 10, 80, 40).Stroke();
    canvas.BeginText().SetFontAndSize("Helvetica", 18).MoveText(15, 80)
        .ShowText("PDF RENDER 42").EndText();
    const std::array<std::byte, 12> imageBytes{
        std::byte{0xFF}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0xFF}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0x00}
    };
    canvas.DrawImage(PdfImage::FromRgb(2U, 2U, imageBytes), {120, 65, 180, 105});

    const auto clippingPage = writer.AddPage({0, 0, 100, 100});
    auto clippingCanvas = writer.GetCanvas(clippingPage);
    clippingCanvas.SaveState()
        .Rectangle(20, 20, 40, 40).Clip().EndPath()
        .SetFillColor(PdfColor::FromRgb(1.0, 0.0, 0.0)).FillRectangle(0, 0, 100, 100)
        .RestoreState()
        .SetFillColor(PdfColor::FromRgb(0.0, 0.0, 1.0)).FillRectangle(75, 75, 10, 10);
    writer.Save(output);

    const auto document = PdfDocument::Open(output);
    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    PDFPP_TEST_CHECK(bitmap.GetWidth() == 200U);
    PDFPP_TEST_CHECK(bitmap.GetHeight() == 120U);

    std::size_t coloredPixels{};
    for (std::size_t y = 0; y < bitmap.GetHeight(); ++y) {
        for (std::size_t x = 0; x < bitmap.GetWidth(); ++x) {
            const auto pixel = bitmap.GetPixel(x, y);
            if (pixel.red != 255U || pixel.green != 255U || pixel.blue != 255U) ++coloredPixels;
        }
    }
    PDFPP_TEST_CHECK(coloredPixels > 2500U);
    const auto redPixel = bitmap.GetPixel(20U, 90U);
    PDFPP_TEST_CHECK(redPixel.red > redPixel.blue);
    const auto imagePixel = bitmap.GetPixel(135U, 30U);
    PDFPP_TEST_CHECK(imagePixel.red != 255U || imagePixel.green != 255U || imagePixel.blue != 255U);

    const auto clipped = PdfPageRenderer::Render(document, 1, options);
    const auto clippedInside = clipped.GetPixel(30U, 70U);
    const auto clippedOutside = clipped.GetPixel(10U, 90U);
    const auto restoredBlue = clipped.GetPixel(80U, 20U);
    PDFPP_TEST_CHECK(clippedInside.red > 200U && clippedInside.green < 40U);
    PDFPP_TEST_CHECK(clippedOutside.red == 255U && clippedOutside.green == 255U && clippedOutside.blue == 255U);
    PDFPP_TEST_CHECK(restoredBlue.blue > 200U && restoredBlue.red < 40U);

    PdfRenderOptions clippingDisabled = options;
    clippingDisabled.honorClippingPaths = false;
    const auto unclipped = PdfPageRenderer::Render(document, 1, clippingDisabled);
    const auto unclippedOutside = unclipped.GetPixel(10U, 90U);
    PDFPP_TEST_CHECK(unclippedOutside.red > 200U && unclippedOutside.green < 40U);

    PdfRenderOptions antialiasedOptions;
    antialiasedOptions.dpi = 72.0;
    antialiasedOptions.antiAliasSamples = 2U;
    const auto antialiased = PdfPageRenderer::Render(document, 0, antialiasedOptions);
    PDFPP_TEST_CHECK(antialiased.GetWidth() == 200U);
    PDFPP_TEST_CHECK(antialiased.GetHeight() == 120U);

    antialiasedOptions.antiAliasSamples = 4U;
    const auto highSample = PdfPageRenderer::Render(document, 0, antialiasedOptions);
    PDFPP_TEST_CHECK(highSample.GetWidth() == 200U);
    PDFPP_TEST_CHECK(highSample.GetHeight() == 120U);

    bitmap.SavePpm(ppm);
    const auto ppmBytes = ReadText(ppm);
    PDFPP_TEST_CHECK(ppmBytes.starts_with("P6\n200 120\n255\n"));

    PdfRenderOptions invalid;
    invalid.dpi = 0.0;
    ExpectThrows([&] { (void)PdfPageRenderer::Render(document, 0, invalid); });
    invalid.dpi = 72.0;
    invalid.maximumDimension = 20U;
    ExpectThrows([&] { (void)PdfPageRenderer::Render(document, 0, invalid); });
    invalid.maximumDimension = 16384U;
    invalid.antiAliasSamples = 5U;
    ExpectThrows([&] { (void)PdfPageRenderer::Render(document, 0, invalid); });

    std::filesystem::remove(output);
    std::filesystem::remove(ppm);
}

void TestSaveValidationAndRoundTrip() {
    const auto output = TempPath("pdfpp_feature_save_roundtrip.pdf");
    PdfWriter empty;
    empty.Save(output);
    PDFPP_TEST_CHECK(std::filesystem::exists(output));

    PdfWriter writer;
    const auto page = writer.AddPage();
    writer.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 12).MoveText(30, 700)
        .ShowText("Round trip").EndText();
    PdfSaveOptions options;
    options.mode = PdfSaveMode::Rewrite;
    options.subsetTrueTypeFonts = false;
    writer.Save(output, options);
    const auto document = PdfDocument::Open(output);
    PDFPP_TEST_CHECK(document.GetPageCount() == 1);
    PDFPP_TEST_CHECK(document.GetPageText(0).find("Round trip") != std::string::npos);
    std::filesystem::remove(output);
}

void TestTransparencyGroupRendering() {
    const auto output = TempPath("pdfpp_feature_transparency_group.pdf");
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 8> offsets{};
    const auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };
    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
                 "/Resources << /XObject << /Fm1 5 0 R >> >> /Contents 4 0 R >>");
    const std::string pageContent = "q 1 0 0 1 0 0 cm /Fm1 Do Q";
    addObject(4, "<< /Length " + std::to_string(pageContent.size()) + ">>\nstream\n" +
                 pageContent + "\nendstream");
    const std::string formContent = "0 0 0 rg 10 10 60 40 re f";
    addObject(5, "<< /Type /XObject /Subtype /Form /FormType 1 /BBox [0 0 80 60] "
                 "/Matrix [1 0 0 1 0 0] "
                 "/Group << /S /Transparency /I true /K false /BM /Normal /CA 0.5 >> "
                 "/Resources << >> "
                 "/Length " + std::to_string(formContent.size()) + ">>\nstream\n" +
                 formContent + "\nendstream");
    addObject(6, "<< >>");
    addObject(7, "<< >>");
    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 8\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();
    std::ofstream file(output, std::ios::binary);
    file.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
    file.close();

    const auto document = PdfDocument::Open(output);
    PDFPP_TEST_CHECK(document.GetPageCount() == 1U);

    const auto list = document.BuildPageDisplayList(0U);
    PDFPP_TEST_CHECK(list.Count(PdfContentEventType::BeginTransparencyGroup) == 1U);
    PDFPP_TEST_CHECK(list.Count(PdfContentEventType::EndTransparencyGroup) == 1U);
    const auto& firstEvent = list.Events().front();
    PDFPP_TEST_CHECK(firstEvent.type == PdfContentEventType::SaveState ||
                     firstEvent.type == PdfContentEventType::BeginTransparencyGroup ||
                     firstEvent.type == PdfContentEventType::ConcatenateMatrix);

    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    PDFPP_TEST_CHECK(bitmap.GetWidth() == 100U);
    PDFPP_TEST_CHECK(bitmap.GetHeight() == 100U);

    const auto insidePixel = bitmap.GetPixel(35U, 65U);
    PDFPP_TEST_CHECK(insidePixel.red < 255U);
    PDFPP_TEST_CHECK(insidePixel.green > 50U);
    PDFPP_TEST_CHECK(std::abs(static_cast<int>(insidePixel.red) - static_cast<int>(insidePixel.green)) < 12);
    PDFPP_TEST_CHECK(std::abs(static_cast<int>(insidePixel.red) - static_cast<int>(insidePixel.blue)) < 12);

    std::filesystem::remove(output);
}

void TestMarkedContentTransparencyGroupRendering() {
    const auto output = TempPath("pdfpp_feature_bdc_group.pdf");
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 5> offsets{};
    const auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };
    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 4 0 R >>");
    const std::string pageContent =
        "0 0 1 rg 70 70 20 20 re f "
        "/G <</Group <</S /Transparency /I true /K false /BM /Multiply /CA 0.8>>>> BDC "
        "1 0 0 rg 10 10 40 30 re f "
        "EMC";
    addObject(4, "<< /Length " + std::to_string(pageContent.size()) + ">>\nstream\n" +
                 pageContent + "\nendstream");
    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 5\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();
    std::ofstream file(output, std::ios::binary);
    file.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
    file.close();

    const auto document = PdfDocument::Open(output);
    const auto list = document.BuildPageDisplayList(0U);
    PDFPP_TEST_CHECK(list.Count(PdfContentEventType::BeginTransparencyGroup) == 1U);
    PDFPP_TEST_CHECK(list.Count(PdfContentEventType::EndTransparencyGroup) == 1U);

    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    const auto redPixel = bitmap.GetPixel(20U, 80U);
    PDFPP_TEST_CHECK(redPixel.red > redPixel.blue);
    const auto bluePixel = bitmap.GetPixel(80U, 20U);
    PDFPP_TEST_CHECK(bluePixel.blue > bluePixel.red);

    std::filesystem::remove(output);
}

std::vector<std::byte> BuildMinimalCffFont() {
    const auto buildIndex = [](const std::vector<std::vector<std::byte>>& objects) {
        std::vector<std::byte> out;
        out.push_back(static_cast<std::byte>(objects.size() >> 8U));
        out.push_back(static_cast<std::byte>(objects.size() & 0xFF));
        if (objects.empty()) return out;
        out.push_back(std::byte{1});
        std::size_t running = 1U;
        for (const auto& object : objects) {
            out.push_back(static_cast<std::byte>(running & 0xFF));
            running += object.size();
        }
        out.push_back(static_cast<std::byte>(running & 0xFF));
        for (const auto& object : objects) out.insert(out.end(), object.begin(), object.end());
        return out;
    };
    const auto fixedShort = [](const int value) {
        return std::vector<std::byte>{std::byte{28},
            static_cast<std::byte>((value >> 8) & 0xFF), static_cast<std::byte>(value & 0xFF)};
    };
    std::vector<std::byte> font;
    font.push_back(std::byte{1}); font.push_back(std::byte{0});
    font.push_back(std::byte{4}); font.push_back(std::byte{4});
    std::vector<std::vector<std::byte>> names;
    names.push_back({std::byte{'C'}, std::byte{'I'}, std::byte{'D'}, std::byte{'F'}, std::byte{'o'}, std::byte{'n'}, std::byte{'t'}});
    const auto nameIndex = buildIndex(names);
    font.insert(font.end(), nameIndex.begin(), nameIndex.end());
    const std::size_t topDictDataSize = 11;
    font.push_back(std::byte{0}); font.push_back(std::byte{1});
    font.push_back(std::byte{2});
    font.push_back(std::byte{0}); font.push_back(std::byte{1});
    font.push_back(static_cast<std::byte>((topDictDataSize + 1U) >> 8U));
    font.push_back(static_cast<std::byte>((topDictDataSize + 1U) & 0xFF));
    const std::size_t topDictDataPos = font.size();
    font.resize(topDictDataPos + topDictDataSize, std::byte{0});
    const auto emptyIndex = buildIndex({});
    font.insert(font.end(), emptyIndex.begin(), emptyIndex.end());
    font.insert(font.end(), emptyIndex.begin(), emptyIndex.end());
    const std::size_t charStringsPos = font.size();
    const std::vector<std::byte> notdef{std::byte{14}};
    const std::vector<std::byte> boxGlyph{
        std::byte{139}, std::byte{139}, std::byte{21},
        std::byte{188}, std::byte{139}, std::byte{5},   // 49 0 rlineto
        std::byte{139}, std::byte{188}, std::byte{5},   // 0 49 rlineto
        std::byte{116}, std::byte{139}, std::byte{5},   // -49 0 rlineto
        std::byte{139}, std::byte{116}, std::byte{5},   // 0 -49 rlineto
        std::byte{14}
    };
    std::vector<std::vector<std::byte>> charStrings;
    charStrings.push_back(notdef);
    charStrings.push_back(boxGlyph);
    const auto charStringsIndex = buildIndex(charStrings);
    font.insert(font.end(), charStringsIndex.begin(), charStringsIndex.end());
    const std::size_t privatePos = font.size();
    const auto privateIndex = buildIndex({});
    font.insert(font.end(), privateIndex.begin(), privateIndex.end());

    auto topDict = fixedShort(static_cast<int>(charStringsPos));
    topDict.push_back(std::byte{17});
    const auto zeroEncoded = fixedShort(0);
    topDict.insert(topDict.end(), zeroEncoded.begin(), zeroEncoded.end());
    const auto privateOffsetEncoded = fixedShort(static_cast<int>(privatePos));
    topDict.insert(topDict.end(), privateOffsetEncoded.begin(), privateOffsetEncoded.end());
    topDict.push_back(std::byte{18});
    for (std::size_t i = 0; i < topDict.size(); ++i) font[topDictDataPos + i] = topDict[i];
    return font;
}

void TestEmbeddedCffFontRendering() {
    const auto output = TempPath("pdfpp_feature_cff_embedded.pdf");
    const auto cffBytes = BuildMinimalCffFont();
    std::string cffData(reinterpret_cast<const char*>(cffBytes.data()), cffBytes.size());

    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 9> offsets{};
    const auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };
    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
                 "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>");
    addObject(4, "<< /Type /Font /Subtype /Type0 /BaseFont /CIDFont /Encoding /Identity-H "
                 "/DescendantFonts [6 0 R] >>");
    const std::string pageContent = "BT /F1 1 Tf 50 0 0 50 10 10 Tm <0001> Tj ET";
    addObject(5, "<< /Length " + std::to_string(pageContent.size()) + " >>\nstream\n" +
                 pageContent + "\nendstream");
    addObject(6, "<< /Type /Font /Subtype /CIDFontType0 /BaseFont /CIDFont "
                 "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
                 "/FontDescriptor 7 0 R /DW 1000 >>");
    addObject(7, "<< /Type /FontDescriptor /FontName /CIDFont /Flags 4 "
                 "/FontBBox [0 0 100 100] /ItalicAngle 0 /Ascent 100 /Descent 0 /CapHeight 100 "
                 "/StemV 80 /FontFile3 8 0 R >>");
    addObject(8, "<< /Length " + std::to_string(cffData.size()) + " /Subtype /CIDFontType0C >>\nstream\n" +
                 cffData + "\nendstream");

    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 9\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 9 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();
    std::ofstream file(output, std::ios::binary);
    file.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
    file.close();

    const auto document = PdfDocument::Open(output);
    const auto font = document.ResolveFont(0U, 0U, "F1");
    PDFPP_TEST_CHECK(font != nullptr);
    PDFPP_TEST_CHECK(font->HasEmbeddedCffFont());
    PDFPP_TEST_CHECK(font->CanRenderEmbeddedGlyphs());
    PDFPP_TEST_CHECK(font->GetEmbeddedCffFont() != nullptr);
    PDFPP_TEST_CHECK(font->GetEmbeddedCffFont()->glyphCount == 2U);
    const auto outline = font->GetCffGlyphOutline(1U);
    PDFPP_TEST_CHECK(!outline.IsEmpty());
    PDFPP_TEST_CHECK(std::abs(outline.xMax - 49.0) < 1.0e-6);
    PDFPP_TEST_CHECK(std::abs(outline.yMax - 49.0) < 1.0e-6);

    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    std::size_t darkPixels{};
    for (std::size_t y = 0; y < bitmap.GetHeight(); ++y) {
        for (std::size_t x = 0; x < bitmap.GetWidth(); ++x) {
            const auto pixel = bitmap.GetPixel(x, y);
            if (pixel.red < 200U) ++darkPixels;
        }
    }
    PDFPP_TEST_CHECK(darkPixels > 100U);
    std::filesystem::remove(output);
}

void TestDashPatternRendering() {
    const auto output = TempPath("pdfpp_feature_dash.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage({0, 0, 200, 100});
    auto canvas = writer.GetCanvas(page);
    canvas.SetStrokeColor(PdfColor::Black());
    canvas.SetLineWidth(3.0);
    // Dashed horizontal line from (10, 50) to (150, 50): 8 on, 8 off.
    const std::array<double, 2> dash{8.0, 8.0};
    canvas.SetDashPattern(dash, 0.0)
        .MoveTo(10, 50)
        .LineTo(150, 50)
        .Stroke();
    writer.Save(output);

    const auto document = PdfDocument::Open(output);
    const auto list = document.BuildPageDisplayList(0U);
    PDFPP_TEST_CHECK(list.Count(PdfContentEventType::SetDashPattern) == 1U);

    PdfRenderOptions options;
    options.dpi = 72.0;
    const auto bitmap = PdfPageRenderer::Render(document, 0, options);
    PDFPP_TEST_CHECK(bitmap.GetWidth() == 200U);
    PDFPP_TEST_CHECK(bitmap.GetHeight() == 100U);

    // The dash pattern must produce alternating on/off runs along the row.
    std::size_t darkPixels = 0;
    bool sawGap = false;
    bool sawDash = false;
    for (std::size_t x = 10; x < 150; ++x) {
        const auto pixel = bitmap.GetPixel(x, 50U);
        if (pixel.red < 100U) {
            ++darkPixels;
            sawDash = true;
        } else {
            sawGap = true;
        }
    }
    PDFPP_TEST_CHECK(sawDash);
    PDFPP_TEST_CHECK(sawGap);
    // Roughly half of the 140pt line is painted by a 8/8 pattern; allow slack
    // for round caps bleeding into gaps.
    PDFPP_TEST_CHECK(darkPixels > 40U && darkPixels < 110U);

    std::filesystem::remove(output);
}

} // namespace

int RunFeatureUnitTests() {
    CPPPdfTest::TestRunner runner;
    runner.Run("Feature.CanvasGraphicsStateAndPaths", TestCanvasGraphicsStateAndPaths);
    runner.Run("Feature.CanvasTextValidation", TestCanvasTextValidation);
    runner.Run("Feature.TextImageStampsAndWatermarks", TestTextImageStampsAndWatermarks);
    runner.Run("Feature.ViewerPreferencesSerialization", TestViewerPreferencesSerialization);
    runner.Run("Feature.ViewerPreferencesValidation", TestViewerPreferencesValidation);
    runner.Run("Feature.PageLabelsLifecycleAndRemapping", TestPageLabelsLifecycleAndRemapping);
    runner.Run("Feature.NamedDestinationsAndLinks", TestNamedDestinationsAndLinks);
    runner.Run("Feature.OpenActionLifecycleAndRemapping", TestOpenActionLifecycleAndRemapping);
    runner.Run("Feature.BookmarkValidationAndLifecycle", TestBookmarkValidationAndLifecycle);
    runner.Run("Feature.EmbeddedFileLifecycleAndValidation", TestEmbeddedFileLifecycleAndValidation);
    runner.Run("Feature.PageMutationRemapsDependentFeatures", TestPageMutationRemapsDependentFeatures);
    runner.Run("Feature.RegexSearchOptionsAndGeometry", TestRegexSearchOptionsAndGeometry);
    runner.Run("Feature.RenderingFoundation", TestRenderingFoundation);
    runner.Run("Feature.TransparencyGroupRendering", TestTransparencyGroupRendering);
    runner.Run("Feature.MarkedContentTransparencyGroupRendering", TestMarkedContentTransparencyGroupRendering);
    runner.Run("Feature.EmbeddedCffFontRendering", TestEmbeddedCffFontRendering);
    runner.Run("Feature.DashPatternRendering", TestDashPatternRendering);
    runner.Run("Feature.ShadingRenderingAndSoftMask", TestShadingRenderingAndSoftMask);
    runner.Run("Feature.TilingPatternRendering", TestTilingPatternRendering);
    runner.Run("Feature.SeparationAndDeviceNRendering", TestSeparationAndDeviceNRendering);
    runner.Run("Feature.IccBasedRendering", TestIccBasedRendering);
    runner.Run("Feature.IccSrgbGammaRendering", TestIccSrgbGammaRendering);
    runner.Run("Feature.OptionalContentLayers", TestOptionalContentLayers);
    runner.Run("Feature.TextLayoutAndFallback", TestTextLayoutAndFallback);
    runner.Run("Feature.DocumentLayoutPrimitives", TestDocumentLayoutPrimitives);
    runner.Run("Feature.Portfolio", TestPortfolio);
    runner.Run("Feature.Redaction", TestRedaction);
    runner.Run("Feature.ParallelRendering", TestParallelRendering);
    runner.Run("Feature.JpxImageWrite", TestJpxImageWrite);
    runner.Run("Feature.Type1FontEmbedding", TestType1FontEmbedding);
    runner.Run("Feature.TaggedPdf", TestTaggedPdf);
    runner.Run("Feature.PngOutput", TestPngOutput);
    runner.Run("Feature.PolygonAndBezierPaths", TestPolygonAndBezierPaths);
    runner.Run("Feature.CffFontEmbedding", TestCffFontEmbedding);
    runner.Run("Feature.SaveValidationAndRoundTrip", TestSaveValidationAndRoundTrip);
    runner.Run("Feature.DocumentTextIndexMappedInputAndStreamWriter", TestDocumentTextIndexMappedInputAndStreamWriter);
    return runner.PrintSummary("Feature unit tests");
}

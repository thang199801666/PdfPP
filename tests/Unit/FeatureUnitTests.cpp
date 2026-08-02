#include <CPPPdf/Api.hpp>
#include "TestRunner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
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
    runner.Run("Feature.SaveValidationAndRoundTrip", TestSaveValidationAndRoundTrip);
    runner.Run("Feature.DocumentTextIndexMappedInputAndStreamWriter", TestDocumentTextIndexMappedInputAndStreamWriter);
    return runner.PrintSummary("Feature unit tests");
}

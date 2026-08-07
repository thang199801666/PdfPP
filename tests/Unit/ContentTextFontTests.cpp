#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Text/PdfTextSearch.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/Rendering/PdfDisplayList.hpp>
#include "TestRunner.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

using namespace CPPPdf;

void TestContentProcessor() {
    PdfContentProcessor processor;
    std::string rendered;
    int beginText = 0;
    int fontEvents = 0;
    int matrixEvents = 0;
    int xobjectEvents = 0;
    int shadingEvents = 0;
    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::BeginText) ++beginText;
        if (event.type == PdfContentEventType::RenderText) rendered += event.text;
        if (event.type == PdfContentEventType::SetFont) {
            ++fontEvents;
            PDFPP_TEST_CHECK(event.textState.fontResource == "F1");
            PDFPP_TEST_CHECK(event.textState.fontSize == 12.0);
        }
        if (event.type == PdfContentEventType::SetTextMatrix) ++matrixEvents;
        if (event.type == PdfContentEventType::InvokeXObject) {
            ++xobjectEvents;
            PDFPP_TEST_CHECK(event.text == "Im1");
        }
        if (event.type == PdfContentEventType::PaintShading) {
            ++shadingEvents;
            PDFPP_TEST_CHECK(event.text == "Sh1");
        }
    });
    processor.Process(
        "q BT /F1 12 Tf 1 0 0 1 72 720 Tm "
        "[(Hel) -120 (lo)] TJ ( World) Tj T* (Next) ' "
         "0 0 (Quoted) \" ET /Im1 Do /Sh1 sh Q");
    PDFPP_TEST_CHECK(beginText == 1);
    PDFPP_TEST_CHECK(fontEvents == 1);
    PDFPP_TEST_CHECK(matrixEvents == 1);
    PDFPP_TEST_CHECK(xobjectEvents == 1);
    PDFPP_TEST_CHECK(shadingEvents == 1);
    PDFPP_TEST_CHECK(rendered == "Hello WorldNextQuoted");
}

void TestDisplayList() {
    PdfContentProcessor processor;
    PdfDisplayList displayList;
    processor.SetHandler([&](const PdfContentEvent& event) { displayList.Add(event); });
    processor.Process("q BT /F1 10 Tf (A) Tj ET Q");
    PDFPP_TEST_CHECK(!displayList.Empty());
    PDFPP_TEST_CHECK(displayList.Events().front().type == PdfContentEventType::SaveState);
    PDFPP_TEST_CHECK(displayList.Events().back().type == PdfContentEventType::RestoreState);
    PDFPP_TEST_CHECK(displayList.Size() == displayList.Events().size());
    PDFPP_TEST_CHECK(displayList.Count(PdfContentEventType::RenderText) == 1U);
    int replayedEvents = 0;
    displayList.Replay([&](const PdfContentEvent&) { ++replayedEvents; });
    PDFPP_TEST_CHECK(replayedEvents == static_cast<int>(displayList.Events().size()));
    int renderTextReplays = 0;
    displayList.ReplayType(PdfContentEventType::RenderText, [&](const PdfContentEvent&) {
        ++renderTextReplays;
    });
    PDFPP_TEST_CHECK(renderTextReplays == 1);
    displayList.Clear();
    PDFPP_TEST_CHECK(displayList.Empty());
    PDFPP_TEST_CHECK(displayList.Size() == 0U);
}

void TestDashPatternParsing() {
    PdfContentProcessor processor;
    int dashEvents = 0;
    std::vector<double> parsedDashPattern;
    double parsedDashPhase = 0.0;
    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::SetDashPattern) {
            ++dashEvents;
            parsedDashPattern = event.numbers;
            parsedDashPhase = event.textState.dashPhase;
        }
    });
    processor.Process("[3 2] 1.5 d 5 w 0 0 m 100 0 l S");
    PDFPP_TEST_CHECK(dashEvents == 1);
    PDFPP_TEST_CHECK(parsedDashPattern.size() == 2U);
    PDFPP_TEST_CHECK_NEAR(parsedDashPattern[0], 3.0, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(parsedDashPattern[1], 2.0, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(parsedDashPhase, 1.5, 1.0e-9);

    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::SetDashPattern) {
            ++dashEvents;
            parsedDashPattern = event.numbers;
            parsedDashPhase = event.textState.dashPhase;
        }
    });
    processor.Process("[] 0 d");
    PDFPP_TEST_CHECK(dashEvents == 2);
    PDFPP_TEST_CHECK(parsedDashPattern.empty());
}

void TestTransparencyGroupEvents() {
    PdfContentProcessor processor;
    int groupBegin = 0;
    int groupEnd = 0;
    int markedBegin = 0;
    int markedEnd = 0;
    PdfTransparencyGroupProperties groupProperties;
    std::string markedTag;
    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::BeginTransparencyGroup) {
            ++groupBegin;
            groupProperties = event.transparencyGroup;
        } else if (event.type == PdfContentEventType::EndTransparencyGroup) {
            ++groupEnd;
        } else if (event.type == PdfContentEventType::BeginMarkedContent) {
            ++markedBegin;
            markedTag = event.text;
        } else if (event.type == PdfContentEventType::EndMarkedContent) {
            ++markedEnd;
        }
    });
    processor.Process(
        "/G1 <</Group <</S /Transparency /I true /K false /BM /Multiply /CA 0.5>>>> BDC "
        "0 0 10 10 re f "
        "/P2 BDC "
        "1 1 2 2 re S "
        "EMC "
        "EMC");
    PDFPP_TEST_CHECK(groupBegin == 1);
    PDFPP_TEST_CHECK(groupEnd == 1);
    PDFPP_TEST_CHECK(groupProperties.isolated);
    PDFPP_TEST_CHECK(!groupProperties.knockout);
    PDFPP_TEST_CHECK(groupProperties.blendMode == "Multiply");
    PDFPP_TEST_CHECK_NEAR(groupProperties.alpha, 0.5, 1.0e-9);
    PDFPP_TEST_CHECK(markedBegin == 1);
    PDFPP_TEST_CHECK(markedEnd == 1);
    PDFPP_TEST_CHECK(markedTag == "P2");

    PdfContentProcessor plainProcessor;
    bool plainMarkedBegin = false;
    bool plainMarkedEnd = false;
    plainProcessor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::BeginMarkedContent) plainMarkedBegin = true;
        if (event.type == PdfContentEventType::EndMarkedContent) plainMarkedEnd = true;
    });
    plainProcessor.Process("/Artifact <</Type /Pagination>> BDC EMC");
    PDFPP_TEST_CHECK(plainMarkedBegin);
    PDFPP_TEST_CHECK(plainMarkedEnd);
}

void TestTextExtractor() {
    PdfTextExtractionRequest textRequest;
    textRequest.strategy = PdfTextExtractionStrategy::Location;
    textRequest.sourceObjectNumber = 77;
    const auto chunks = PdfTextExtractor::ExtractChunks(
        "BT /F1 10 Tf 1 0 0 1 20 100 Tm (World) Tj "
        "1 0 0 1 10 120 Tm (Hello) Tj ET",
        textRequest);
    PDFPP_TEST_CHECK(chunks.size() == 2);
    PDFPP_TEST_CHECK(chunks[0].fontResource == "F1");
    PDFPP_TEST_CHECK(chunks[0].encodedText == "World");
    PDFPP_TEST_CHECK(chunks[0].sourceObjectNumber == 77);
    PDFPP_TEST_CHECK(chunks[0].boundingBox.right > chunks[0].boundingBox.left);
    PDFPP_TEST_CHECK(PdfTextExtractor::BuildText(chunks, textRequest) == "Hello\nWorld");

    PdfTextExtractionRequest regionRequest;
    regionRequest.strategy = PdfTextExtractionStrategy::Region;
    regionRequest.region = PdfTextRegion{PdfRectangle{0, 110, 100, 140}, true};
    const auto regionText = PdfTextExtractor::ExtractText(
        "BT /F1 10 Tf 1 0 0 1 20 100 Tm (World) Tj "
        "1 0 0 1 10 120 Tm (Hello) Tj ET",
        regionRequest);
    PDFPP_TEST_CHECK(regionText == "Hello");

    PdfTextExtractionRequest fillOnly;
    fillOnly.options.renderingMode = 0;
    const auto fillText = PdfTextExtractor::ExtractText(
        "BT /F1 10 Tf 3 Tr (clip) Tj 0 Tr (fill) Tj ET", fillOnly);
    PDFPP_TEST_CHECK(fillText == "fill");

    PdfTextExtractionRequest invalidMode;
    invalidMode.options.renderingMode = 8;
    bool rejectedMode = false;
    try {
        (void)PdfTextExtractor::ExtractChunks("BT /F1 10 Tf (text) Tj ET", invalidMode);
    } catch (const std::invalid_argument&) {
        rejectedMode = true;
    }
    PDFPP_TEST_CHECK(rejectedMode);

    PdfRegexSearchOptions invalidRegexMode;
    invalidRegexMode.renderingMode = -1;
    rejectedMode = false;
    try {
        (void)PdfTextSearch::FindRegex(
            chunks, R"(World)", invalidRegexMode);
    } catch (const std::invalid_argument&) {
        rejectedMode = true;
    }
    PDFPP_TEST_CHECK(rejectedMode);

    const auto transformedChunks = PdfTextExtractor::ExtractChunks(
        "2 0 0 3 100 200 cm BT /F1 10 Tf 1 0 0 1 5 7 Tm (A) Tj ET");
    PDFPP_TEST_CHECK(transformedChunks.size() == 1);
    PDFPP_TEST_CHECK_NEAR(transformedChunks[0].start.x, 110.0, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(transformedChunks[0].start.y, 221.0, 1.0e-9);

    const auto restoredChunks = PdfTextExtractor::ExtractChunks(
        "q 2 0 0 2 10 20 cm BT /F1 10 Tf 1 0 0 1 1 1 Tm (A) Tj ET Q "
        "BT /F1 10 Tf 1 0 0 1 1 1 Tm (B) Tj ET");
    PDFPP_TEST_CHECK(restoredChunks.size() == 2);
    PDFPP_TEST_CHECK_NEAR(restoredChunks[0].start.x, 12.0, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(restoredChunks[0].start.y, 22.0, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(restoredChunks[1].start.x, 1.0, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(restoredChunks[1].start.y, 1.0, 1.0e-9);
}

void TestCMapsAndFonts() {
    const auto cmap = PdfToUnicodeCMap::Parse(
        "1 begincodespacerange <00> <FF> endcodespacerange\n"
        "2 beginbfchar <41> <0041> <42> <03A9> endbfchar");
    PDFPP_TEST_CHECK(cmap.MappingCount() >= 2);
    PDFPP_TEST_CHECK(cmap.Decode(std::string("AB", 2)) == std::string("A\xCE\xA9"));

    const auto advancedCMap = PdfToUnicodeCMap::Parse(
        "2 begincodespacerange <00> <7F> <8100> <81FF> endcodespacerange\n"
        "1 beginbfchar <41> <0041> endbfchar\n"
        "2 beginbfrange <8100> <8102> [<0042> <03A9> <D83DDE00>] "
        "<20> <22> <0061> endbfrange");
    PDFPP_TEST_CHECK(advancedCMap.CodeSpaceRangeCount() == 2);
    const std::string encoded = std::string("A", 1) + std::string("\x81\x00\x81\x01\x81\x02", 6);
    PDFPP_TEST_CHECK(advancedCMap.Decode(encoded) == std::string("AB\xCE\xA9\xF0\x9F\x98\x80"));
    PDFPP_TEST_CHECK(advancedCMap.Decode(std::string(" !\"", 3)) == "abc");

    PdfDictionary encodingDictionary;
    encodingDictionary.Put(PdfName("BaseEncoding"), PdfObject(PdfName("WinAnsiEncoding")));
    PdfArray differences;
    differences.push_back(PdfObject(std::int64_t{65}));
    differences.push_back(PdfObject(PdfName("Omega")));
    encodingDictionary.Put(PdfName("Differences"), PdfObject(std::move(differences)));

    PdfArray simpleWidths;
    simpleWidths.push_back(PdfObject(std::int64_t{600}));
    simpleWidths.push_back(PdfObject(std::int64_t{610}));
    PdfDictionary simpleFontDictionary;
    simpleFontDictionary.Put(PdfName("Subtype"), PdfObject(PdfName("TrueType")));
    simpleFontDictionary.Put(PdfName("BaseFont"), PdfObject(PdfName("TestSans")));
    simpleFontDictionary.Put(PdfName("Encoding"), PdfObject(std::move(encodingDictionary)));
    simpleFontDictionary.Put(PdfName("FirstChar"), PdfObject(std::int64_t{65}));
    simpleFontDictionary.Put(PdfName("Widths"), PdfObject(std::move(simpleWidths)));
    const auto simpleFont = PdfFontResource::Create(simpleFontDictionary);
    PDFPP_TEST_CHECK(simpleFont.GetSubtype() == PdfFontSubtype::TrueType);
    PDFPP_TEST_CHECK(simpleFont.GetCharacterCodes("AZ").size() == 2U);
    PDFPP_TEST_CHECK(simpleFont.GetCharacterCodes("AZ")[0] == 65U);
    PDFPP_TEST_CHECK(simpleFont.Decode("A") == std::string("\xCE\xA9"));
    PDFPP_TEST_CHECK(simpleFont.GetGlyphWidth(65) == 600);
    PDFPP_TEST_CHECK(simpleFont.GetGlyphWidth(90) == 500);

    const std::string fontCMapSource =
        "1 begincodespacerange <0000> <FFFF> endcodespacerange\n"
        "2 beginbfchar <0041> <0041> <03A9> <03A9> endbfchar";
    std::vector<std::byte> cmapBytes(fontCMapSource.size());
    for (std::size_t i = 0; i < fontCMapSource.size(); ++i)
        cmapBytes[i] = static_cast<std::byte>(fontCMapSource[i]);
    PdfStream cmapStream(PdfDictionary{}, std::move(cmapBytes));

    PdfArray cidWidths;
    cidWidths.push_back(PdfObject(std::int64_t{65}));
    PdfArray cidWidthValues;
    cidWidthValues.push_back(PdfObject(std::int64_t{700}));
    cidWidths.push_back(PdfObject(std::move(cidWidthValues)));
    PdfDictionary cidFontDictionary;
    cidFontDictionary.Put(PdfName("Subtype"), PdfObject(PdfName("CIDFontType2")));
    cidFontDictionary.Put(PdfName("DW"), PdfObject(std::int64_t{1000}));
    cidFontDictionary.Put(PdfName("W"), PdfObject(std::move(cidWidths)));
    PdfArray descendants;
    descendants.push_back(PdfObject(std::move(cidFontDictionary)));
    PdfDictionary type0Dictionary;
    type0Dictionary.Put(PdfName("Subtype"), PdfObject(PdfName("Type0")));
    type0Dictionary.Put(PdfName("BaseFont"), PdfObject(PdfName("CompositeTest")));
    type0Dictionary.Put(PdfName("Encoding"), PdfObject(PdfName("Identity-H")));
    type0Dictionary.Put(PdfName("DescendantFonts"), PdfObject(std::move(descendants)));
    type0Dictionary.Put(PdfName("ToUnicode"), PdfObject(std::move(cmapStream)));
    const auto compositeFont = PdfFontResource::Create(type0Dictionary);
    PDFPP_TEST_CHECK(compositeFont.IsComposite());
    PDFPP_TEST_CHECK(compositeFont.HasToUnicode());
    PDFPP_TEST_CHECK(compositeFont.Decode(std::string("\x00\x41\x03\xA9", 4)) == std::string("A\xCE\xA9"));
    PDFPP_TEST_CHECK(compositeFont.GetGlyphWidth(65) == 700);
    PDFPP_TEST_CHECK(compositeFont.GetGlyphWidth(66) == 1000);
}

void TestTextSearch() {
    std::vector<PdfTextChunk> splitKeywordChunks;
    splitKeywordChunks.push_back(PdfTextChunk{
        "open", {10, 100}, {30, 100}, {10, 98, 30, 110}});
    splitKeywordChunks.push_back(PdfTextChunk{
        "XL", {31, 100}, {41, 100}, {31, 98, 41, 110}});
    splitKeywordChunks.push_back(PdfTextChunk{
        " unrelated", {50, 100}, {100, 100}, {50, 98, 100, 110}});
    const auto splitMatches = PdfTextSearch::Find(splitKeywordChunks, "OPENxl");
    PDFPP_TEST_CHECK(splitMatches.size() == 1);
    PDFPP_TEST_CHECK(splitMatches[0].matchedText == "openXL");
    PDFPP_TEST_CHECK(splitMatches[0].rectangles.size() == 2);
    PDFPP_TEST_CHECK(splitMatches[0].firstChunkIndex == 0);
    PDFPP_TEST_CHECK(splitMatches[0].lastChunkIndex == 1);
    PDFPP_TEST_CHECK_NEAR(splitMatches[0].boundingBox.left, 10.0, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(splitMatches[0].boundingBox.right, 41.0, 1.0e-9);

    const auto regexSplitMatches = PdfTextSearch::FindRegex(splitKeywordChunks, R"(OPENx[lL])");
    PDFPP_TEST_CHECK(regexSplitMatches.size() == 1);
    splitKeywordChunks[1].boundingBox = {31, 70, 41, 82};
    PDFPP_TEST_CHECK(PdfTextSearch::Find(splitKeywordChunks, "openXL").empty());
    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(splitKeywordChunks, R"(OPENx[lL])").empty());
}

void TestTextSearchOptions() {
    std::vector<PdfTextChunk> chunks;
    chunks.push_back(PdfTextChunk{"Hello", {0, 100}, {40, 100}, {0, 98, 40, 110}});
    chunks.push_back(PdfTextChunk{" World", {40, 100}, {80, 100}, {40, 98, 80, 110}});
    chunks.push_back(PdfTextChunk{" Hello", {100, 200}, {140, 200}, {100, 198, 140, 210}});

    PdfTextSearchOptions caseSensitive;
    caseSensitive.caseInsensitive = false;
    PDFPP_TEST_CHECK(PdfTextSearch::Find(chunks, "hello").size() == 2U);
    PDFPP_TEST_CHECK(PdfTextSearch::Find(chunks, "hello", caseSensitive).empty());
    PDFPP_TEST_CHECK(PdfTextSearch::Find(chunks, "Hello", caseSensitive).size() == 2U);
    PDFPP_TEST_CHECK(PdfTextSearch::Find(chunks, "Hel").size() == 2U);

    PdfRegexSearchOptions regexOptions;
    regexOptions.maxMatches = 1U;
    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(chunks, R"(Hello)").size() == 2U);
    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(chunks, R"(Hello)", regexOptions).size() == 1U);

    std::vector<PdfTextChunk> moved = chunks;
    PdfTextSearchIndex index(std::move(moved));
    PDFPP_TEST_CHECK(index.GetChunkCount() == 3U);
    PDFPP_TEST_CHECK(index.GetSearchableByteCount() > 0U);
    PDFPP_TEST_CHECK(index.GetSearchableText().find("World") != std::string_view::npos);
    PDFPP_TEST_CHECK(index.Find("hello").size() == 2U);
    PDFPP_TEST_CHECK(index.FindRegex(R"(Hello)").size() == 2U);
    const std::regex expression(R"(World)", std::regex::ECMAScript);
    PDFPP_TEST_CHECK(index.FindRegex(expression).size() == 1U);
    const auto& owned = index.GetChunks();
    PDFPP_TEST_CHECK(owned.size() == 3U);
}

#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/Fonts/PdfFont.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Text/PdfTextSearch.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include <cassert>
#include <cstddef>
#include <string>
#include <array>
#include <span>
#include <vector>
#include <zlib.h>
#include "TestRunner.hpp"

int RunReaderIntegrationTests();
int RunWriterIntegrationTests();
int RunApiCoverageTests();
int RunFeatureUnitTests();

int RunCoreTests() {
    using namespace CPPPdf;
    const auto object = Internal::PdfObjectParser::Parse(
        "12 0 obj << /Type /Page /Rotate 90 /MediaBox [0 0 612 792] /Parent 2 0 R >> endobj");
    const auto* dictionary = object.AsDictionary();
    PDFPP_TEST_CHECK(dictionary != nullptr);
    PDFPP_TEST_CHECK(dictionary->GetAsName(PdfName::Type)->value() == "Page");
    PDFPP_TEST_CHECK(dictionary->Get(PdfName::Rotate).AsInteger() == 90);
    PDFPP_TEST_CHECK(dictionary->GetAsArray(PdfName::MediaBox)->size() == 4);
    PDFPP_TEST_CHECK(dictionary->Get(PdfName("Parent")).AsReference()->first == 2);

    const std::string asciiHex = "48656c6c6f>";
    const auto hex = PdfFilterPipeline::DecodeAsciiHex(std::span(
        reinterpret_cast<const std::byte*>(asciiHex.data()), asciiHex.size()));
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(hex.data()), hex.size()) == "Hello");

    const std::vector<std::byte> runLength{
        std::byte{2}, std::byte{'A'}, std::byte{'B'}, std::byte{'C'},
        std::byte{254}, std::byte{'Z'}, std::byte{128}};
    const auto rl = PdfFilterPipeline::DecodeRunLength(runLength);
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(rl.data()), rl.size()) == "ABCZZZ");

    const std::array<std::byte, 6> sourceBytes{
        std::byte{'P'}, std::byte{'d'}, std::byte{'f'},
        std::byte{'+'}, std::byte{'+'}, std::byte{'!'}};
    PdfMemoryInputSource source(sourceBytes);
    std::array<char, 3> slice{};
    source.Read(3U, slice);
    PDFPP_TEST_CHECK(std::string(slice.data(), slice.size()) == "++!");
    PDFPP_TEST_CHECK(source.ReadAll().size() == sourceBytes.size());

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

    PdfContentProcessor processor;
    std::string rendered;
    int beginText = 0;
    processor.SetHandler([&](const PdfContentEvent& event) {
        if (event.type == PdfContentEventType::BeginText) ++beginText;
        if (event.type == PdfContentEventType::RenderText) rendered += event.text;
    });
    int fontEvents = 0;
    int matrixEvents = 0;
    int xobjectEvents = 0;
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
    });
    processor.Process(
        "q BT /F1 12 Tf 1 0 0 1 72 720 Tm "
        "[(Hel) -120 (lo)] TJ ( World) Tj T* (Next) ' "
        "0 0 (Quoted) \" ET /Im1 Do Q");
    PDFPP_TEST_CHECK(beginText == 1);
    PDFPP_TEST_CHECK(fontEvents == 1);
    PDFPP_TEST_CHECK(matrixEvents == 1);
    PDFPP_TEST_CHECK(xobjectEvents == 1);
    PDFPP_TEST_CHECK(rendered == "Hello WorldNextQuoted");

    PdfTextExtractionRequest textRequest;
    textRequest.strategy = PdfTextExtractionStrategy::Location;
    textRequest.sourceObjectNumber = 77;
    const auto chunks = PdfTextExtractor::ExtractChunks(
        "BT /F1 10 Tf 1 0 0 1 20 100 Tm (World) Tj "
        "1 0 0 1 10 120 Tm (Hello) Tj ET",
        textRequest);
    PDFPP_TEST_CHECK(chunks.size() == 2);
    PDFPP_TEST_CHECK(chunks[0].fontResource == "F1");
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


    const auto transformedChunks = PdfTextExtractor::ExtractChunks(
        "2 0 0 3 100 200 cm BT /F1 10 Tf 1 0 0 1 5 7 Tm (A) Tj ET");
    PDFPP_TEST_CHECK(transformedChunks.size() == 1);
    PDFPP_TEST_CHECK(std::abs(transformedChunks[0].start.x - 110.0) < 1.0e-9);
    PDFPP_TEST_CHECK(std::abs(transformedChunks[0].start.y - 221.0) < 1.0e-9);
    PDFPP_TEST_CHECK(transformedChunks[0].boundingBox.right > transformedChunks[0].start.x);
    PDFPP_TEST_CHECK(transformedChunks[0].boundingBox.top > transformedChunks[0].start.y);

    const std::array<std::byte, 7> pngSubEncoded{
        std::byte{1}, std::byte{10}, std::byte{20}, std::byte{30},
        std::byte{5}, std::byte{5}, std::byte{5}};
    uLongf compressedSize = compressBound(static_cast<uLong>(pngSubEncoded.size()));
    std::vector<std::byte> compressed(compressedSize);
    PDFPP_TEST_CHECK(compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressedSize,
        reinterpret_cast<const Bytef*>(pngSubEncoded.data()),
        static_cast<uLong>(pngSubEncoded.size()), Z_BEST_SPEED) == Z_OK);
    compressed.resize(compressedSize);
    const auto predicted = PdfFilterPipeline{}.Decode(compressed, {
        PdfFilterSpec{"FlateDecode", "<< /Predictor 12 /Colors 3 /BitsPerComponent 8 /Columns 2 >>"}});
    const std::array<std::byte, 6> expectedPredictor{
        std::byte{10}, std::byte{20}, std::byte{30},
        std::byte{15}, std::byte{25}, std::byte{35}};
    PDFPP_TEST_CHECK(std::equal(predicted.begin(), predicted.end(), expectedPredictor.begin(), expectedPredictor.end()));

    bool rejectedDecodedLimit = false;
    try {
        (void)PdfFilterPipeline::DecodeFlate(compressed, 1U);
    } catch (const PdfException&) {
        rejectedDecodedLimit = true;
    }
    PDFPP_TEST_CHECK(rejectedDecodedLimit);

    const auto restoredChunks = PdfTextExtractor::ExtractChunks(
        "q 2 0 0 2 10 20 cm BT /F1 10 Tf 1 0 0 1 1 1 Tm (A) Tj ET Q "
        "BT /F1 10 Tf 1 0 0 1 1 1 Tm (B) Tj ET");
    PDFPP_TEST_CHECK(restoredChunks.size() == 2);
    PDFPP_TEST_CHECK(std::abs(restoredChunks[0].start.x - 12.0) < 1.0e-9);
    PDFPP_TEST_CHECK(std::abs(restoredChunks[0].start.y - 22.0) < 1.0e-9);
    PDFPP_TEST_CHECK(std::abs(restoredChunks[1].start.x - 1.0) < 1.0e-9);
    PDFPP_TEST_CHECK(std::abs(restoredChunks[1].start.y - 1.0) < 1.0e-9);

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
    PDFPP_TEST_CHECK(std::abs(splitMatches[0].boundingBox.left - 10.0) < 1.0e-9);
    PDFPP_TEST_CHECK(std::abs(splitMatches[0].boundingBox.right - 41.0) < 1.0e-9);

    const auto regexSplitMatches = PdfTextSearch::FindRegex(splitKeywordChunks, R"(OPENx[lL])");
    PDFPP_TEST_CHECK(regexSplitMatches.size() == 1);
    splitKeywordChunks[1].boundingBox = {31, 70, 41, 82};
    PDFPP_TEST_CHECK(PdfTextSearch::Find(splitKeywordChunks, "openXL").empty());
    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(splitKeywordChunks, R"(OPENx[lL])").empty());

    return 0;
}


int main() {
    CPPPdfTest::TestRunner runner;
    runner.Run("Core.ParserFiltersFontsContentText", RunCoreTests);
    runner.Run("Reader.Integration", RunReaderIntegrationTests);
    runner.Run("Writer.PageEditingFormsIntegration", RunWriterIntegrationTests);
    runner.Run("PublicAPI.AllFeatureGroups", RunApiCoverageTests);
    runner.Run("Features.AllPublicFeatureUnits", RunFeatureUnitTests);
    return runner.PrintSummary();
}

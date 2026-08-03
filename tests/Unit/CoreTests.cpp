#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/Fonts/PdfFont.hpp>
#include <CPPPdf/Fonts/PdfFontResource.hpp>
#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Text/PdfTextSearch.hpp>
#include <CPPPdf/Rendering/PdfBitmap.hpp>
#include <CPPPdf/Rendering/PdfTransparencyGroup.h>
#include <CPPPdf/Graphics/PdfFunction.hpp>
#include <CPPPdf/Rendering/PdfShading.hpp>
#include <CPPPdf/Fonts/PdfCff.hpp>
#include <CPPPdf/Rendering/PdfDisplayList.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <array>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
#include <zlib.h>
#include "TestRunner.hpp"

int RunReaderIntegrationTests();
int RunWriterIntegrationTests();
int RunApiCoverageTests();
int RunFeatureUnitTests();
int RunSecurityTests();

namespace {

struct LzwTestCode {
    std::uint32_t code{};
    int width{};
};

// Minimal PDF-style LZW encoder (early change on, codes packed LSB-first)
// used to exercise the decoder against a known-good round trip.
std::vector<LzwTestCode> EncodeLzw(const std::string& data) {
    std::unordered_map<std::string, std::uint32_t> table;
    for (std::uint32_t i = 0U; i < 256U; ++i) table[std::string(1, static_cast<char>(i))] = i;
    std::uint32_t nextCode = 258U;
    int width = 9;
    std::vector<LzwTestCode> output;
    const auto push = [&output, &width](std::uint32_t code) {
        output.push_back(LzwTestCode{code, width});
    };
    push(256U);
    std::string current;
    for (const char ch : data) {
        const std::string candidate = current + ch;
        if (table.count(candidate) != 0U) {
            current = candidate;
            continue;
        }
        push(table[current]);
        if (nextCode < 4096U) table[candidate] = nextCode++;
        if (nextCode == ((1U << width) - 1U) && width < 12) ++width;
        current = std::string(1, ch);
    }
    if (!current.empty()) push(table[current]);
    push(257U);
    return output;
}

std::vector<std::byte> PackLzwLsbFirst(const std::vector<LzwTestCode>& codes) {
    std::vector<std::byte> bytes;
    std::size_t bitPosition = 0;
    for (const auto& item : codes) {
        for (int bit = 0; bit < item.width; ++bit) {
            if (bitPosition % 8U == 0U) bytes.push_back(std::byte{0});
            const std::uint32_t value =
                (item.code >> bit) & 1U;
            const std::size_t byteIndex = bitPosition / 8U;
            bytes[byteIndex] = static_cast<std::byte>(
                std::to_integer<unsigned char>(bytes[byteIndex]) |
                static_cast<unsigned char>(value << (bitPosition % 8U)));
            ++bitPosition;
        }
    }
    return bytes;
}

} // namespace

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

    const auto streamObject = Internal::PdfObjectParser::Parse(
        "7 0 obj << /Length 5 /Type /XObject >>\nstream\nhello\nendstream\nendobj");
    const auto* stream = streamObject.AsStream();
    PDFPP_TEST_CHECK(stream != nullptr);
    PDFPP_TEST_CHECK(stream->bytes().size() == 5);
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(stream->bytes().data()),
                                 stream->bytes().size()) == "hello");

    bool rejectedNegativeStreamLength = false;
    try {
        Internal::PdfObjectParser::Parse(
            "<< /Length -1 >>\nstream\nhello\nendstream");
    } catch (const PdfException& error) {
        rejectedNegativeStreamLength = error.code() == PdfErrorCode::MalformedObject;
    }
    PDFPP_TEST_CHECK(rejectedNegativeStreamLength);

    bool rejectedTruncatedStream = false;
    try {
        Internal::PdfObjectParser::Parse(
            "<< /Length 8 >>\nstream\nhello\nendstream");
    } catch (const PdfException& error) {
        rejectedTruncatedStream = error.code() == PdfErrorCode::MalformedObject;
    }
    PDFPP_TEST_CHECK(rejectedTruncatedStream);

    const auto escapedString = Internal::PdfObjectParser::Parse(
        "(A\\053B \\(nested\\) \\\\ C\\r\\nD)");
    PDFPP_TEST_CHECK(escapedString.AsString() != nullptr);
    PDFPP_TEST_CHECK(*escapedString.AsString() == "A+B (nested) \\ C\r\nD");

    const auto boolean = Internal::PdfObjectParser::Parse("true");
    PDFPP_TEST_CHECK(boolean.AsBoolean().value_or(false));
    bool rejectedKeywordPrefix = false;
    try {
        Internal::PdfObjectParser::Parse("trueValue");
    } catch (const PdfException& error) {
        rejectedKeywordPrefix = error.code() == PdfErrorCode::MalformedObject;
    }
    PDFPP_TEST_CHECK(rejectedKeywordPrefix);

    const auto objectWithEnd = Internal::PdfObjectParser::Parse(
        "12 0 obj << /Value 7 >> endobj");
    PDFPP_TEST_CHECK(objectWithEnd.AsDictionary() != nullptr);
    bool rejectedTrailingData = false;
    try {
        Internal::PdfObjectParser::Parse("7 junk");
    } catch (const PdfException& error) {
        rejectedTrailingData = error.code() == PdfErrorCode::MalformedObject;
    }
    PDFPP_TEST_CHECK(rejectedTrailingData);

    const auto negativeNumber = Internal::PdfObjectParser::Parse("-42");
    PDFPP_TEST_CHECK(negativeNumber.AsInteger().value_or(0) == -42);
    const auto reference = Internal::PdfObjectParser::Parse("12 65535 R");
    PDFPP_TEST_CHECK(reference.AsReference().has_value());
    bool rejectedInvalidReference = false;
    try {
        Internal::PdfObjectParser::Parse("12 65536 R");
    } catch (const PdfException& error) {
        rejectedInvalidReference = error.code() == PdfErrorCode::MalformedObject;
    }
    PDFPP_TEST_CHECK(rejectedInvalidReference);

    bool rejectedBitmapOverflow = false;
    try {
        CPPPdf::PdfBitmap invalidBitmap(std::numeric_limits<std::size_t>::max(), 2U);
        (void)invalidBitmap;
    } catch (const PdfException& error) {
        rejectedBitmapOverflow = error.code() == PdfErrorCode::InvalidArgument;
    }
    PDFPP_TEST_CHECK(rejectedBitmapOverflow);

    CPPPdf::PdfBitmap alphaBitmap(1U, 1U, {0U, 0U, 0U, 0U});
    alphaBitmap.BlendPixelInBounds(0U, 0U, {255U, 0U, 0U, 128U});
    PDFPP_TEST_CHECK(alphaBitmap.GetPixel(0U, 0U).alpha == 128U);
    const auto beforeTransparentBlend = alphaBitmap.GetPixel(0U, 0U);
    alphaBitmap.BlendPixelInBounds(0U, 0U, {0U, 255U, 0U, 0U});
    const auto afterTransparentBlend = alphaBitmap.GetPixel(0U, 0U);
    PDFPP_TEST_CHECK(beforeTransparentBlend.red == afterTransparentBlend.red &&
                     beforeTransparentBlend.green == afterTransparentBlend.green &&
                     beforeTransparentBlend.alpha == afterTransparentBlend.alpha);
    alphaBitmap.BlendPixelInBounds(0U, 0U, {0U, 0U, 255U, 128U});
    PDFPP_TEST_CHECK(alphaBitmap.GetPixel(0U, 0U).alpha == 192U);
    PdfBitmap blendBitmap(1U, 1U, {100U, 150U, 200U, 255U});
    blendBitmap.BlendPixel(0, 0, {200U, 100U, 50U, 255U}, PdfBlendMode::Multiply);
    PDFPP_TEST_CHECK(blendBitmap.GetPixel(0U, 0U).red == 78U);
    PDFPP_TEST_CHECK(blendBitmap.GetPixel(0U, 0U).green == 58U);
    PDFPP_TEST_CHECK(blendBitmap.GetPixel(0U, 0U).blue == 39U);
    PdfBitmap screenBitmap(1U, 1U, {100U, 150U, 200U, 255U});
    screenBitmap.BlendPixel(0, 0, {200U, 100U, 50U, 255U}, PdfBlendMode::Screen);
    PDFPP_TEST_CHECK(screenBitmap.GetPixel(0U, 0U).red > 100U);
    PdfBitmap differenceBitmap(1U, 1U, {100U, 150U, 200U, 255U});
    differenceBitmap.BlendPixel(0, 0, {200U, 100U, 50U, 255U}, PdfBlendMode::Difference);
    PDFPP_TEST_CHECK(differenceBitmap.GetPixel(0U, 0U).red == 100U);
    PdfBitmap overlayBitmap(1U, 1U, {100U, 150U, 200U, 255U});
    overlayBitmap.BlendBitmap(blendBitmap, 0, 0, PdfBlendMode::Overlay, 128U);

    PdfBitmap groupTarget(2U, 1U, PdfRgbaColor::White());
    PdfTransparencyGroup group{PdfBitmap(2U, 1U, {0U, 0U, 0U, 0U}), false, false,
                               PdfBlendMode::SourceOver};
    group.bitmap.SetPixel(0, 0, {255U, 0U, 0U, 128U});
    group.CompositeInto(groupTarget);
    PDFPP_TEST_CHECK(groupTarget.GetPixel(0U, 0U).red > 250U);
    PDFPP_TEST_CHECK(groupTarget.GetPixel(0U, 0U).green < 200U);
    group.knockout = true;
    group.bitmap.SetPixel(1, 0, {0U, 0U, 255U, 255U});
    group.BlendInto(groupTarget);
    PDFPP_TEST_CHECK(groupTarget.GetPixel(1U, 0U).blue == 255U);

    PdfExponentialFunction function({0.0, 0.2}, {1.0, 0.8}, 2.0);
    const auto midpoint = function.Evaluate(0.5);
    PDFPP_TEST_CHECK(midpoint.size() == 2U);
    PDFPP_TEST_CHECK(std::abs(midpoint[0] - 0.25) < 1.0e-9);
    PDFPP_TEST_CHECK(std::abs(midpoint[1] - 0.35) < 1.0e-9);

    PdfAxialShading shading;
    shading.coordinates = {0.0, 0.0, 100.0, 0.0};
    shading.function.emplace(std::vector<double>{0.0}, std::vector<double>{1.0}, 1.0);
    const auto sample = shading.Sample(50.0, 0.0);
    PDFPP_TEST_CHECK(sample.has_value());
    PDFPP_TEST_CHECK(std::abs(sample->at(0) - 0.5) < 1.0e-9);
    PDFPP_TEST_CHECK(!shading.Sample(-1.0, 0.0).has_value());
    PdfRadialShading radial;
    radial.coordinates = {50.0, 50.0, 0.0, 50.0, 50.0, 50.0};
    radial.function.emplace(std::vector<double>{0.0}, std::vector<double>{1.0}, 1.0);
    const auto radialSample = radial.Sample(50.0, 25.0);
    PDFPP_TEST_CHECK(radialSample.has_value());
    PDFPP_TEST_CHECK(std::abs(radialSample->at(0) - 0.5) < 1.0e-9);
    radial.coordinates = {0.0, 0.0, 10.0, 10.0, 0.0, 20.0};
    PDFPP_TEST_CHECK(radial.Sample(10.0, 0.0).has_value());
    radial.extendEnd = false;
    PDFPP_TEST_CHECK(!radial.Sample(100.0, 100.0).has_value());
    PdfSampledFunction sampled(1U, 1U, {2U}, {0U, 65535U}, {0.0, 1.0}, {0.0, 1.0}, 16U);
    const auto sampledValue = sampled.Evaluate(std::array<double, 1>{0.75});
    PDFPP_TEST_CHECK(sampledValue.size() == 1U);
    PDFPP_TEST_CHECK(sampledValue[0] > 0.9);
    auto first = std::make_shared<const PdfExponentialFunction>(
        std::vector<double>{0.0}, std::vector<double>{1.0}, 1.0);
    auto second = std::make_shared<const PdfExponentialFunction>(
        std::vector<double>{1.0}, std::vector<double>{0.0}, 1.0);
    PdfStitchedFunction stitched({first, second}, {0.5}, {0.0, 1.0, 0.0, 1.0});
    PDFPP_TEST_CHECK(stitched.Evaluate(0.25)[0] > 0.4);
    PDFPP_TEST_CHECK(stitched.Evaluate(0.75)[0] < 0.6);
    PdfCalculatorFunction calculator("3 3 mul");
    PDFPP_TEST_CHECK(std::abs(calculator.Evaluate({}, 1)[0] - 9.0) < 1.0e-9);
    bool rejectedOperator = false;
    try { (void)PdfCalculatorFunction("exec").Evaluate({}, 1U); }
    catch (const std::runtime_error&) { rejectedOperator = true; }
    PDFPP_TEST_CHECK(rejectedOperator);
    PdfDictionary functionDictionary;
    PdfArray c0, c1;
    c0.push_back(PdfObject(0.0));
    c1.push_back(PdfObject(1.0));
    functionDictionary.Put(PdfName("FunctionType"), PdfObject(static_cast<std::int64_t>(2)));
    functionDictionary.Put(PdfName("C0"), PdfObject(c0));
    functionDictionary.Put(PdfName("C1"), PdfObject(c1));
    functionDictionary.Put(PdfName("N"), PdfObject(1.0));
    const auto parsedFunction = ParsePdfFunction(functionDictionary);
    PDFPP_TEST_CHECK(parsedFunction.has_value());
    const std::array<std::byte, 10> cffIndexBytes{
        std::byte{0}, std::byte{1}, std::byte{1}, std::byte{1}, std::byte{2},
        std::byte{'A'}, std::byte{'B'}, std::byte{0}, std::byte{0}, std::byte{0}};
    std::size_t cffOffset{};
    const auto cffIndex = PdfCffParser::ParseIndex(cffIndexBytes, cffOffset);
    PDFPP_TEST_CHECK(cffIndex.objects.size() == 1U);
    PDFPP_TEST_CHECK(cffIndex.objects[0].size() == 1U);
    const std::array<std::byte, 2> cffDictBytes{std::byte{139}, std::byte{15}};
    const auto cffDict = PdfCffParser::ParseDict(cffDictBytes);
    PDFPP_TEST_CHECK(cffDict.size() == 1U);
    PDFPP_TEST_CHECK(cffDict[0].operands[0] == 0.0);
    PDFPP_TEST_CHECK(overlayBitmap.GetPixel(0U, 0U).alpha == 255U);

    const std::string asciiHex = "48656c6c6f>";
    const auto hex = PdfFilterPipeline::DecodeAsciiHex(std::span(
        reinterpret_cast<const std::byte*>(asciiHex.data()), asciiHex.size()));
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(hex.data()), hex.size()) == "Hello");

    const std::vector<std::byte> runLength{
        std::byte{2}, std::byte{'A'}, std::byte{'B'}, std::byte{'C'},
        std::byte{254}, std::byte{'Z'}, std::byte{128}};
    const auto rl = PdfFilterPipeline::DecodeRunLength(runLength);
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(rl.data()), rl.size()) == "ABCZZZ");

    const std::string lzwSource =
        "TOBEORNOTTOBEORTOBEORNOTTOBEORNOTTOBEORNOTTOBEORNOTTOBEORNOT"
        "The quick brown fox jumps over the lazy dog. 0123456789 "
        "0123456789 0123456789 0123456789 0123456789 0123456789";
    const auto lzwCodes = EncodeLzw(lzwSource);
    const auto lzwPacked = PackLzwLsbFirst(lzwCodes);
    const auto lzw = PdfFilterPipeline::DecodeLzw(lzwPacked);
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(lzw.data()), lzw.size()) == lzwSource);

    const auto lzwViaPipeline = PdfFilterPipeline{}.Decode(
        lzwPacked, {{"LZWDecode", "<< /EarlyChange 1 >>"}});
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(lzwViaPipeline.data()),
                                 lzwViaPipeline.size()) == lzwSource);

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
    runner.Run("Security.PasswordEncryptionRoundTrips", RunSecurityTests);
    return runner.PrintSummary();
}

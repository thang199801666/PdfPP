#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/Fonts/PdfCff.hpp>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/PdfError.hpp>
#include "TestHelpers.hpp"
#include "TestRunner.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>
#include <zlib.h>

using namespace CPPPdf;

void TestCffParser() {
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

    const auto fontBytes = CPPPdfTest::BuildMinimalCff();
    const auto parsedCff = PdfCffParser::ParseFont(std::span<const std::byte>(fontBytes));
    PDFPP_TEST_CHECK(parsedCff.name == "Test");
    PDFPP_TEST_CHECK(parsedCff.glyphCount == 2U);
    PDFPP_TEST_CHECK(parsedCff.charStrings.objects.size() == 2U);
    const auto notdef = PdfCffParser::GetGlyphOutline(parsedCff, 0U);
    PDFPP_TEST_CHECK(notdef.IsEmpty());
    const auto box = PdfCffParser::GetGlyphOutline(parsedCff, 1U);
    PDFPP_TEST_CHECK(!box.IsEmpty());
    PDFPP_TEST_CHECK(box.segments.size() >= 5U);
    PDFPP_TEST_CHECK_NEAR(box.xMax, 3.0, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(box.yMax, 2.0, 1.0e-9);
    bool foundLine = false;
    for (const auto& segment : box.segments) {
        if (segment.type == PdfCffOutlineSegment::Type::Line) foundLine = true;
    }
    PDFPP_TEST_CHECK(foundLine);
}

void TestFilters() {
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
    const auto lzwCodes = CPPPdfTest::EncodeLzw(lzwSource);
    const auto lzwPacked = CPPPdfTest::PackLzwLsbFirst(lzwCodes);
    const auto lzw = PdfFilterPipeline::DecodeLzw(lzwPacked);
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(lzw.data()), lzw.size()) == lzwSource);
    const auto lzwViaPipeline = PdfFilterPipeline{}.Decode(
        lzwPacked, {{"LZWDecode", "<< /EarlyChange 1 >>"}});
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(lzwViaPipeline.data()),
                                 lzwViaPipeline.size()) == lzwSource);

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

    PDFPP_TEST_EXPECT_THROWS(([] {
        const std::array<std::byte, 4> sample{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        (void)PdfFilterPipeline::DecodeFlate(sample, 1U);
    }));
}

void TestInputSources() {
    const std::array<std::byte, 6> sourceBytes{
        std::byte{'P'}, std::byte{'d'}, std::byte{'f'},
        std::byte{'+'}, std::byte{'+'}, std::byte{'!'}};
    PdfMemoryInputSource source(sourceBytes);
    std::array<char, 3> slice{};
    source.Read(3U, slice);
    PDFPP_TEST_CHECK(std::string(slice.data(), slice.size()) == "++!");
    PDFPP_TEST_CHECK(source.ReadAll().size() == sourceBytes.size());
}

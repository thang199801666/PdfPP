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

    const std::string oddAsciiHex = "48 65 6C 6C 6F 7>";
    const auto oddHex = PdfFilterPipeline::DecodeAsciiHex(std::span(
        reinterpret_cast<const std::byte*>(oddAsciiHex.data()), oddAsciiHex.size()));
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(oddHex.data()), oddHex.size()) == "Hello\x70");

    const std::string ascii85Zero = "z~>";
    const auto zeroTuple = PdfFilterPipeline::DecodeAscii85(std::span(
        reinterpret_cast<const std::byte*>(ascii85Zero.data()), ascii85Zero.size()));
    PDFPP_TEST_CHECK(zeroTuple.size() == 4U);
    PDFPP_TEST_CHECK(std::all_of(zeroTuple.begin(), zeroTuple.end(),
                                 [](const std::byte value) { return value == std::byte{0}; }));

    PDFPP_TEST_EXPECT_THROWS(([] {
        const std::string invalid = "486g>";
        (void)PdfFilterPipeline::DecodeAsciiHex(std::span(
            reinterpret_cast<const std::byte*>(invalid.data()), invalid.size()));
    }));

    PDFPP_TEST_EXPECT_THROWS(([] {
        const std::string invalid = "v";
        (void)PdfFilterPipeline::DecodeAscii85(std::span(
            reinterpret_cast<const std::byte*>(invalid.data()), invalid.size()));
    }));

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

    // Round trips through the newly added Encode* counterparts.
    const std::string payload =
        "The quick brown fox jumps over the lazy dog. "
        "\x00\x01\x02\x7F\x80\xFF"          // binary bytes, repeated runs
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAA"       // long run for RunLength
        "Mixed case mixed case mixed case, 0123456789, 0123456789.";
    std::vector<std::byte> bytes;
    bytes.reserve(payload.size());
    for (const char ch : payload) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));

    const auto flateEncoded = PdfFilterPipeline::EncodeFlate(bytes);
    const auto flateRound = PdfFilterPipeline::DecodeFlate(flateEncoded,
        std::numeric_limits<std::size_t>::max());
    PDFPP_TEST_CHECK(std::equal(flateRound.begin(), flateRound.end(), bytes.begin(), bytes.end()));

    const auto hexEncoded = PdfFilterPipeline::EncodeAsciiHex(bytes);
    const auto hexRound = PdfFilterPipeline::DecodeAsciiHex(hexEncoded);
    PDFPP_TEST_CHECK(std::equal(hexRound.begin(), hexRound.end(), bytes.begin(), bytes.end()));

    const auto a85Encoded = PdfFilterPipeline::EncodeAscii85(bytes);
    const auto a85Round = PdfFilterPipeline::DecodeAscii85(a85Encoded);
    PDFPP_TEST_CHECK(std::equal(a85Round.begin(), a85Round.end(), bytes.begin(), bytes.end()));

    const auto rlEncoded = PdfFilterPipeline::EncodeRunLength(bytes);
    const auto rlRound = PdfFilterPipeline::DecodeRunLength(rlEncoded);
    PDFPP_TEST_CHECK(std::equal(rlRound.begin(), rlRound.end(), bytes.begin(), bytes.end()));

    const auto lzwEncoded = PdfFilterPipeline::EncodeLzw(bytes);
    const auto lzwRound = PdfFilterPipeline::DecodeLzw(lzwEncoded);
    PDFPP_TEST_CHECK(std::equal(lzwRound.begin(), lzwRound.end(), bytes.begin(), bytes.end()));

    // Pipeline-level encode then decode round trip with two chained filters.
    const auto chained = PdfFilterPipeline{}.Encode(bytes, {
        PdfFilterSpec{"ASCII85Decode", {}}, PdfFilterSpec{"FlateDecode", {}}});
    const auto chainedRound = PdfFilterPipeline{}.Decode(chained, {
        PdfFilterSpec{"FlateDecode", {}}, PdfFilterSpec{"ASCII85Decode", {}}});
    PDFPP_TEST_CHECK(std::equal(chainedRound.begin(), chainedRound.end(), bytes.begin(), bytes.end()));

    const std::vector<std::byte> rows{
        std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
        std::byte{50}, std::byte{60}, std::byte{70}, std::byte{80}};
    const auto tiffPredictor = PdfFilterPipeline{}.Encode(rows, {
        PdfFilterSpec{"FlateDecode", "/Predictor 2 /Colors 1 /BitsPerComponent 8 /Columns 4"}});
    const auto tiffRound = PdfFilterPipeline{}.Decode(tiffPredictor, {
        PdfFilterSpec{"FlateDecode", "/Predictor 2 /Colors 1 /BitsPerComponent 8 /Columns 4"}});
    PDFPP_TEST_CHECK(std::equal(tiffRound.begin(), tiffRound.end(), rows.begin(), rows.end()));

    const auto pngPredictor = PdfFilterPipeline{}.Encode(rows, {
        PdfFilterSpec{"FlateDecode", "/Predictor 12 /Colors 1 /BitsPerComponent 8 /Columns 4"}});
    const auto pngRound = PdfFilterPipeline{}.Decode(pngPredictor, {
        PdfFilterSpec{"FlateDecode", "/Predictor 12 /Colors 1 /BitsPerComponent 8 /Columns 4"}});
    PDFPP_TEST_CHECK(std::equal(pngRound.begin(), pngRound.end(), rows.begin(), rows.end()));
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

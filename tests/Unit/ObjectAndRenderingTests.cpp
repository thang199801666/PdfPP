#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Rendering/PdfBitmap.hpp>
#include <CPPPdf/Rendering/PdfTransparencyGroup.h>
#include <CPPPdf/Graphics/PdfFunction.hpp>
#include <CPPPdf/Rendering/PdfShading.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "TestRunner.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace CPPPdf;

void TestObjectParser() {
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

    PDFPP_TEST_EXPECT_THROWS(([] {
        (void)Internal::PdfObjectParser::Parse("<< /Length -1 >>\nstream\nhello\nendstream");
    }));
    PDFPP_TEST_EXPECT_THROWS(([] {
        (void)Internal::PdfObjectParser::Parse("<< /Length 8 >>\nstream\nhello\nendstream");
    }));

    const auto escapedString = Internal::PdfObjectParser::Parse(
        "(A\\053B \\(nested\\) \\\\ C\\r\\nD)");
    PDFPP_TEST_CHECK(escapedString.AsString() != nullptr);
    PDFPP_TEST_CHECK(*escapedString.AsString() == "A+B (nested) \\ C\r\nD");

    const auto boolean = Internal::PdfObjectParser::Parse("true");
    PDFPP_TEST_CHECK(boolean.AsBoolean().value_or(false));
    PDFPP_TEST_EXPECT_THROWS(([] { (void)Internal::PdfObjectParser::Parse("trueValue"); }));

    const auto objectWithEnd = Internal::PdfObjectParser::Parse(
        "12 0 obj << /Value 7 >> endobj");
    PDFPP_TEST_CHECK(objectWithEnd.AsDictionary() != nullptr);
    PDFPP_TEST_EXPECT_THROWS(([] { (void)Internal::PdfObjectParser::Parse("7 junk"); }));

    const auto negativeNumber = Internal::PdfObjectParser::Parse("-42");
    PDFPP_TEST_CHECK(negativeNumber.AsInteger().value_or(0) == -42);
    const auto reference = Internal::PdfObjectParser::Parse("12 65535 R");
    PDFPP_TEST_CHECK(reference.AsReference().has_value());
    PDFPP_TEST_EXPECT_THROWS(([] { (void)Internal::PdfObjectParser::Parse("12 65536 R"); }));
}

void TestBitmapBlendModes() {
    PDFPP_TEST_EXPECT_THROWS(([] {
        CPPPdf::PdfBitmap invalidBitmap(std::numeric_limits<std::size_t>::max(), 2U);
        (void)invalidBitmap;
    }));

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
    PDFPP_TEST_CHECK(blendBitmap.GetPixel(0U, 0U).green == 59U);
    PDFPP_TEST_CHECK(blendBitmap.GetPixel(0U, 0U).blue == 39U);
    PdfBitmap screenBitmap(1U, 1U, {100U, 150U, 200U, 255U});
    screenBitmap.BlendPixel(0, 0, {200U, 100U, 50U, 255U}, PdfBlendMode::Screen);
    PDFPP_TEST_CHECK(screenBitmap.GetPixel(0U, 0U).red > 100U);
    PdfBitmap differenceBitmap(1U, 1U, {100U, 150U, 200U, 255U});
    differenceBitmap.BlendPixel(0, 0, {200U, 100U, 50U, 255U}, PdfBlendMode::Difference);
    PDFPP_TEST_CHECK(differenceBitmap.GetPixel(0U, 0U).red == 100U);
    PdfBitmap overlayBitmap(1U, 1U, {100U, 150U, 200U, 255U});
    overlayBitmap.BlendBitmap(blendBitmap, 0, 0, PdfBlendMode::Overlay, 128U);
    PDFPP_TEST_CHECK(overlayBitmap.GetPixel(0U, 0U).alpha == 255U);

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
}

void TestFunctionsAndShading() {
    PdfExponentialFunction function({0.0, 0.2}, {1.0, 0.8}, 2.0);
    const auto midpoint = function.Evaluate(0.5);
    PDFPP_TEST_CHECK(midpoint.size() == 2U);
    PDFPP_TEST_CHECK_NEAR(midpoint[0], 0.25, 1.0e-9);
    PDFPP_TEST_CHECK_NEAR(midpoint[1], 0.35, 1.0e-9);

    PdfAxialShading shading;
    shading.coordinates = {0.0, 0.0, 100.0, 0.0};
    shading.function.emplace(std::vector<double>{0.0}, std::vector<double>{1.0}, 1.0);
    const auto sample = shading.Sample(50.0, 0.0);
    PDFPP_TEST_CHECK(sample.has_value());
    PDFPP_TEST_CHECK_NEAR(sample->at(0), 0.5, 1.0e-9);
    PDFPP_TEST_CHECK(!shading.Sample(-1.0, 0.0).has_value());

    PdfRadialShading radial;
    radial.coordinates = {50.0, 50.0, 0.0, 50.0, 50.0, 50.0};
    radial.function.emplace(std::vector<double>{0.0}, std::vector<double>{1.0}, 1.0);
    const auto radialSample = radial.Sample(50.0, 25.0);
    PDFPP_TEST_CHECK(radialSample.has_value());
    PDFPP_TEST_CHECK_NEAR(radialSample->at(0), 0.5, 1.0e-9);
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
    PDFPP_TEST_CHECK_NEAR(calculator.Evaluate({}, 1)[0], 9.0, 1.0e-9);
    PDFPP_TEST_EXPECT_THROWS(([] { (void)PdfCalculatorFunction("exec").Evaluate({}, 1U); }));

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
}

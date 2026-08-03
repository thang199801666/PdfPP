#include "TestRunner.hpp"

#include <string_view>

// Test case declarations, grouped by domain. Each is a named function that
// runs independently so a failure pinpoints the exact test that broke.

void TestObjectParser();
void TestBitmapBlendModes();
void TestFunctionsAndShading();
void TestCffParser();
void TestFilters();
void TestInputSources();
void TestContentProcessor();
void TestDisplayList();
void TestDashPatternParsing();
void TestTransparencyGroupEvents();
void TestTextExtractor();
void TestCMapsAndFonts();
void TestTextSearch();
void TestTextSearchOptions();
void TestPdfAConforming();
void TestPdfAMissingMetadataAndOutput();
void TestPdfAPartMismatch();
void TestPdfUAStructure();

int RunReaderIntegrationTests();
int RunWriterIntegrationTests();
int RunApiCoverageTests();
int RunFeatureUnitTests();
int RunSecurityTests();

int main() {
    CPPPdfTest::TestRunner runner;

    // Core object model and rendering primitives.
    runner.Run("Object.Parser", TestObjectParser);
    runner.Run("Rendering.BitmapBlendModes", TestBitmapBlendModes);
    runner.Run("Rendering.FunctionsAndShading", TestFunctionsAndShading);

    // CFF fonts, stream filters, and input sources.
    runner.Run("Fonts.CffParser", TestCffParser);
    runner.Run("Filters.Decoders", TestFilters);
    runner.Run("IO.InputSources", TestInputSources);

    // Content stream processing, text extraction, fonts, and search.
    runner.Run("Content.Processor", TestContentProcessor);
    runner.Run("Content.DisplayList", TestDisplayList);
    runner.Run("Content.DashPatternParsing", TestDashPatternParsing);
    runner.Run("Content.TransparencyGroupEvents", TestTransparencyGroupEvents);
    runner.Run("Text.Extractor", TestTextExtractor);
    runner.Run("Fonts.CMapsAndResources", TestCMapsAndFonts);
    runner.Run("Text.Search", TestTextSearch);
    runner.Run("Text.SearchOptions", TestTextSearchOptions);

    // Validation.
    runner.Run("Validation.PdfAConforming", TestPdfAConforming);
    runner.Run("Validation.PdfAMissingMetadataAndOutput", TestPdfAMissingMetadataAndOutput);
    runner.Run("Validation.PdfAPartMismatch", TestPdfAPartMismatch);
    runner.Run("Validation.PdfUAStructure", TestPdfUAStructure);

    // Higher-level integration suites.
    runner.Run("Reader.Integration", RunReaderIntegrationTests);
    runner.Run("Writer.PageEditingFormsIntegration", RunWriterIntegrationTests);
    runner.Run("PublicAPI.AllFeatureGroups", RunApiCoverageTests);
    runner.Run("Features.AllPublicFeatureUnits", RunFeatureUnitTests);
    runner.Run("Security.PasswordEncryptionRoundTrips", RunSecurityTests);

    return runner.PrintSummary();
}

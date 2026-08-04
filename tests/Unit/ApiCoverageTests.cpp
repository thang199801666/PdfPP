#include <CPPPdf/Api.hpp>
#include <CPPPdf/Text/PdfTextLayout.hpp>
#include <CPPPdf/pdfpp_c.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <vector>
#include <type_traits>
#include "TestRunner.hpp"

namespace {

using namespace CPPPdf;

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    PDFPP_TEST_CHECK(input);
    const std::vector<char> chars((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::transform(chars.begin(), chars.end(), bytes.begin(), [](char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return bytes;
}

std::string ReadBytesAsString(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    PDFPP_TEST_CHECK(input);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void ExpectThrows(const auto& operation) {
    bool thrown = false;
    try {
        operation();
    } catch (const std::exception&) {
        thrown = true;
    }
    PDFPP_TEST_CHECK(thrown);
}

void TestObjectModel() {
    PdfObject nullObject;
    PDFPP_TEST_CHECK(nullObject.IsNull());
    PDFPP_TEST_CHECK(nullObject.type() == PdfObjectType::Null);

    PdfObject boolObject(true);
    PdfObject integerObject(std::int64_t{42});
    PdfObject realObject(2.5);
    PdfObject nameObject(PdfName("Example"));
    PdfObject stringObject(std::string("value"));
    PDFPP_TEST_CHECK(boolObject.AsBoolean().value());
    PDFPP_TEST_CHECK(integerObject.AsInteger().value() == 42);
    PDFPP_TEST_CHECK(std::abs(realObject.AsReal().value() - 2.5) < 1e-12);
    PDFPP_TEST_CHECK(nameObject.AsName()->value() == "Example");
    PDFPP_TEST_CHECK(*stringObject.AsString() == "value");

    PdfArray array;
    array.push_back(PdfObject(std::int64_t{1}));
    array.push_back(PdfObject(std::string("two")));
    PDFPP_TEST_CHECK(array.size() == 2);
    PDFPP_TEST_CHECK(array.at(0).AsInteger().value() == 1);
    ExpectThrows([&] { (void)array.at(5); });

    PdfDictionary dictionary;
    dictionary.Put(PdfName("A"), PdfObject(std::int64_t{7}));
    dictionary.Put(PdfName("Items"), PdfObject(array));
    PDFPP_TEST_CHECK(dictionary.Contains(PdfName("A")));
    PDFPP_TEST_CHECK(dictionary.Get(PdfName("A")).AsInteger().value() == 7);
    PDFPP_TEST_CHECK(dictionary.GetAsArray(PdfName("Items"))->size() == 2);
    PDFPP_TEST_CHECK(dictionary.Remove(PdfName("A")));
    PDFPP_TEST_CHECK(!dictionary.Remove(PdfName("Missing")));
    ExpectThrows([&] { (void)dictionary.Get(PdfName("Missing")); });

    const auto reference = PdfObject::IndirectReference(99, 2);
    PDFPP_TEST_CHECK(reference.type() == PdfObjectType::IndirectReference);
    PDFPP_TEST_CHECK(reference.AsReference()->first == 99);
    PDFPP_TEST_CHECK(reference.AsReference()->second == 2);

    const std::array<std::byte, 3> streamBytes{std::byte{'P'}, std::byte{'D'}, std::byte{'F'}};
    PdfStream stream(PdfDictionary{}, std::vector<std::byte>(streamBytes.begin(), streamBytes.end()));
    PdfObject streamObject(std::move(stream));
    PDFPP_TEST_CHECK(streamObject.AsStream()->bytes().size() == 3);
}

void TestInputSources() {
    const std::array<std::byte, 6> bytes{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'}, std::byte{'e'}, std::byte{'f'}};

    PdfMemoryInputSource memory(bytes);
    PDFPP_TEST_CHECK(memory.Size() == bytes.size());
    std::array<char, 3> buffer{};
    memory.Read(2, buffer);
    PDFPP_TEST_CHECK(std::string(buffer.data(), buffer.size()) == "cde");
    ExpectThrows([&] { memory.Read(5, buffer); });

    std::istringstream stream("stream-source");
    PdfStreamInputSource streamSource(stream);
    PDFPP_TEST_CHECK(streamSource.Size() == 13);
    std::array<char, 6> streamBuffer{};
    streamSource.Read(7, streamBuffer);
    PDFPP_TEST_CHECK(std::string(streamBuffer.data(), streamBuffer.size()) == "source");

    const auto path = TempPath("pdfpp_api_input_source.bin");
    {
        std::ofstream output(path, std::ios::binary);
        output << "file-source";
    }
    PdfFileInputSource fileSource(path);
    PDFPP_TEST_CHECK(fileSource.Size() == 11);
    std::array<char, 4> fileBuffer{};
    fileSource.Read(5, fileBuffer);
    PDFPP_TEST_CHECK(std::string(fileBuffer.data(), fileBuffer.size()) == "sour");
    std::filesystem::remove(path);
}

void TestFilters() {
    const std::string hexText = "61 62 6 3>";
    const auto hex = PdfFilterPipeline::DecodeAsciiHex(std::as_bytes(std::span(hexText)));
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(hex.data()), hex.size()) == "abc");

    const std::string ascii85 = "87cURD]j7BEbo80";
    const auto decoded85 = PdfFilterPipeline::DecodeAscii85(std::as_bytes(std::span(ascii85)));
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(decoded85.data()), decoded85.size()) == "Hello world!");

    const std::array<std::byte, 7> runLength{
        std::byte{2}, std::byte{'A'}, std::byte{'B'}, std::byte{'C'},
        std::byte{254}, std::byte{'Z'}, std::byte{128}};
    const auto decodedRunLength = PdfFilterPipeline::DecodeRunLength(runLength);
    PDFPP_TEST_CHECK(std::string(reinterpret_cast<const char*>(decodedRunLength.data()), decodedRunLength.size()) == "ABCZZZ");

    const std::string unknownInput = "abc";
    ExpectThrows([&] {
        (void)PdfFilterPipeline{}.Decode(std::as_bytes(std::span(unknownInput)), {{"Unsupported", {}}});
    });
}

void TestImages() {
    const std::array<std::byte, 6> rgb{
        std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const auto image = PdfImage::FromRgb(2, 1, rgb);
    PDFPP_TEST_CHECK(image.GetWidth() == 2);
    PDFPP_TEST_CHECK(image.GetHeight() == 1);
    PDFPP_TEST_CHECK(image.GetColorSpace() == PdfImageColorSpace::DeviceRGB);
    PDFPP_TEST_CHECK(image.GetEncoding() == PdfImageEncoding::Raw);
    PDFPP_TEST_CHECK(image.GetBytes().size() == rgb.size());

    const std::array<std::byte, 2> gray{std::byte{1}, std::byte{2}};
    const auto grayImage = PdfImage::FromGray(2, 1, gray);
    PDFPP_TEST_CHECK(grayImage.GetColorSpace() == PdfImageColorSpace::DeviceGray);

    ExpectThrows([&] { (void)PdfImage::FromRgb(2, 2, rgb); });
    ExpectThrows([&] { (void)PdfImage::FromGray(3, 1, gray); });
    const std::array<std::byte, 3> badJpeg{std::byte{1}, std::byte{2}, std::byte{3}};
    ExpectThrows([&] { (void)PdfImage::FromJpeg(badJpeg); });
}

void TestTextSearch() {
    std::vector<PdfTextChunk> chunks(3);
    chunks[0].utf8Text = "Hello";
    chunks[0].boundingBox = {0, 0, 25, 10};
    chunks[0].start = {0, 0};
    chunks[0].end = {25, 0};
    chunks[1].utf8Text = " ";
    chunks[1].boundingBox = {25, 0, 28, 10};
    chunks[1].start = {25, 0};
    chunks[1].end = {28, 0};
    chunks[2].utf8Text = "World";
    chunks[2].boundingBox = {28, 0, 55, 10};
    chunks[2].start = {28, 0};
    chunks[2].end = {55, 0};

    auto matches = PdfTextSearch::Find(chunks, "hello world");
    PDFPP_TEST_CHECK(matches.size() == 1);
    PDFPP_TEST_CHECK(matches[0].firstChunkIndex == 0);
    PDFPP_TEST_CHECK(matches[0].lastChunkIndex == 2);
    PDFPP_TEST_CHECK(matches[0].boundingBox.right == 55);

    PdfTextSearchOptions sensitive;
    sensitive.caseInsensitive = false;
    PDFPP_TEST_CHECK(PdfTextSearch::Find(chunks, "hello", sensitive).empty());

    const auto regexMatches = PdfTextSearch::FindRegex(chunks, R"(H[a-z]+\s+W[a-z]+)");
    PDFPP_TEST_CHECK(regexMatches.size() == 1);
    PDFPP_TEST_CHECK(regexMatches[0].matchedText == "Hello World");
    PDFPP_TEST_CHECK(regexMatches[0].firstChunkIndex == 0);
    PDFPP_TEST_CHECK(regexMatches[0].lastChunkIndex == 2);

    // UTF-8 truncation keeps grapheme clusters intact.
    PDFPP_TEST_CHECK(PdfTextLayout::CountCodePoints("he\u0301llo") == 6);
    PDFPP_TEST_CHECK(PdfTextLayout::TruncateUtf8("hello world", 5) == "hello...");
    PDFPP_TEST_CHECK(PdfTextLayout::TruncateUtf8("he\u0301llo", 3) == "he\u0301...");
    PDFPP_TEST_CHECK(PdfTextLayout::StripCombiningMarks("he\u0301llo") == "hello");
    // NFC normalization composes base + combining mark.
    PDFPP_TEST_CHECK(PdfTextLayout::NormalizeNfc("e\u0301") == "\u00e9");
    PDFPP_TEST_CHECK(PdfTextLayout::NormalizeNfc("o\u0308") == "\u00f6");
    PDFPP_TEST_CHECK(PdfTextLayout::NormalizeNfc("c\u0327") == "\u00e7");
    // Unicode-aware case conversion.
    PDFPP_TEST_CHECK(PdfTextLayout::ToUpper("caf\u00e9") == "CAF\u00c9");
    PDFPP_TEST_CHECK(PdfTextLayout::ToLower("CAF\u00c9") == "caf\u00e9");
    // Word wrap with a monospace-ish measure (1 unit per character).
    const auto measure = [](const std::string_view text) {
        return static_cast<double>(text.size());
    };
    const auto wrapped = PdfTextLayout::WordWrap("aaa bb cc", 5.0, measure);
    PDFPP_TEST_CHECK(wrapped.size() == 2U);
    PDFPP_TEST_CHECK(wrapped[0] == "aaa");
    PDFPP_TEST_CHECK(wrapped[1] == "bb cc");

    // Accent-insensitive search: "cafe" matches "caf\u00e9".
    PdfTextChunk accented;
    accented.utf8Text = "caf\u00e9";
    accented.boundingBox = {0, 0, 30, 10};
    accented.start = {0, 0};
    accented.end = {30, 0};
    PdfTextSearchOptions ignoreAccents;
    ignoreAccents.ignoreAccents = true;
    PDFPP_TEST_CHECK(PdfTextSearch::Find({accented}, "cafe", ignoreAccents).size() == 1);
    PDFPP_TEST_CHECK(PdfTextSearch::Find({accented}, "cafe").empty());

    PdfRegexSearchOptions limitedRegex;
    limitedRegex.maxMatches = 1;
    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(chunks, R"([A-Za-z]+)", limitedRegex).size() == 1);

    ExpectThrows([&] { (void)PdfTextSearch::Find(chunks, ""); });
    ExpectThrows([&] { (void)PdfTextSearch::FindRegex(chunks, "["); });

    // Word extraction groups chunks into words by horizontal gaps.
    const auto words = PdfTextExtractor::ExtractWords(chunks, 3.0);
    PDFPP_TEST_CHECK(words.size() == 2);
    PDFPP_TEST_CHECK(words[0].text == "Hello");
    PDFPP_TEST_CHECK(words[1].text == "World");
    PDFPP_TEST_CHECK(words[0].boundingBox.right == 25);
    PDFPP_TEST_CHECK(words[1].boundingBox.left == 28);

    // PdfDocument::SearchText convenience over a written page.
    const auto searchPath = TempPath("pdfpp_api_search.pdf");
    PdfWriter writer;
    const auto searchPage = writer.AddPage({0, 0, 200, 200});
    writer.GetCanvas(searchPage).BeginText().SetFontAndSize("Helvetica", 12)
        .MoveText(20, 150).ShowText("Gamma rays shine").EndText();
    writer.Save(searchPath);
    const auto searchDocument = PdfDocument::Open(searchPath);
    const auto pageMatches = searchDocument.SearchText(0U, "gamma");
    PDFPP_TEST_CHECK(pageMatches.size() == 1);
    PDFPP_TEST_CHECK(pageMatches[0].matchedText == "Gamma");
    ExpectThrows([&] { (void)searchDocument.SearchText(9U, "x"); });
    std::filesystem::remove(searchPath);
}

void TestContentCommands() {
    const auto line = PdfContentCommands::DrawLine({1, 2}, {3, 4}, 2, 1, 0, 0);
    PDFPP_TEST_CHECK(line.find("2 w") != std::string::npos);
    PDFPP_TEST_CHECK(line.find("1 0 0 RG") != std::string::npos);
    PDFPP_TEST_CHECK(line.find("1 2 m") != std::string::npos);

    const auto fill = PdfContentCommands::FillRectangle({10, 20, 30, 40}, 0, 1, 0);
    PDFPP_TEST_CHECK(fill.find("0 1 0 rg") != std::string::npos);
    PDFPP_TEST_CHECK(fill.find("10 20 20 20 re") != std::string::npos);

    const auto stroke = PdfContentCommands::StrokeRectangle({10, 20, 30, 40});
    PDFPP_TEST_CHECK(stroke.find(" re") != std::string::npos);
}

std::filesystem::path CreateBaseDocument() {
    const auto path = TempPath("pdfpp_api_base.pdf");
    PdfWriter writer;
    const auto first = writer.AddPage({0, 0, 300, 400});
    writer.GetCanvas(first)
        .SaveState()
        .SetStrokeColor(PdfColor::Red())
        .SetFillColor(PdfColor::Gray(0.9))
        .SetLineWidth(2)
        .Rectangle(10, 10, 50, 30)
        .FillStroke()
        .RestoreState()
        .BeginText()
        .SetFontAndSize("Helvetica", 12)
        .MoveText(20, 350)
        .ShowText("API page one")
        .EndText();
    const auto second = writer.AddPage({0, 0, 500, 600});
    writer.GetCanvas(second).BeginText().SetFontAndSize("Helvetica", 12)
        .MoveText(20, 550).ShowText("API page two").EndText();
    writer.Save(path);
    return path;
}

void TestDocumentOpenOverloads(const std::filesystem::path& path) {
    auto fileDocument = PdfDocument::Open(path);
    PDFPP_TEST_CHECK(fileDocument.GetVersion() == "1.7");
    PDFPP_TEST_CHECK(fileDocument.GetPageCount() == 2);
    PDFPP_TEST_CHECK(fileDocument.fileSize() > 0);
    PDFPP_TEST_CHECK(fileDocument.xrefEntryCount() > 0);
    PDFPP_TEST_CHECK(fileDocument.GetCatalogReference().objectNumber > 0);
    PDFPP_TEST_CHECK(!fileDocument.IsEncrypted());
    PDFPP_TEST_CHECK(fileDocument.GetPage(0).GetMediaBox().right == 300);
    PDFPP_TEST_CHECK(fileDocument.GetPage(1).GetMediaBox().top == 600);
    PDFPP_TEST_CHECK(fileDocument.GetPageText(0).find("API page one") != std::string::npos);
    PDFPP_TEST_CHECK(fileDocument.ExtractAllPageTextParallel(2) == fileDocument.GetAllPageText());
    PDFPP_TEST_CHECK(fileDocument.GetObjectCacheCapacity() == fileDocument.readerOptions().limits.maxCachedObjects);
    fileDocument.ClearObjectCache();
    PDFPP_TEST_CHECK(fileDocument.GetCachedObjectCount() == 0);
    ExpectThrows([&] { (void)fileDocument.GetPage(50); });

    const auto bytes = ReadBytes(path);
    auto memoryDocument = PdfDocument::Open(bytes);
    PDFPP_TEST_CHECK(memoryDocument.GetPageCount() == 2);

    std::ifstream input(path, std::ios::binary);
    auto streamDocument = PdfDocument::Open(input);
    PDFPP_TEST_CHECK(streamDocument.GetPageCount() == 2);

    auto source = std::make_unique<PdfMemoryInputSource>(bytes);
    auto sourceDocument = PdfDocument::Open(std::move(source));
    PDFPP_TEST_CHECK(sourceDocument.GetPageCount() == 2);
}

void TestWriterValidation() {
    PdfWriter writer;
    PDFPP_TEST_CHECK(writer.GetPageCount() == 0);
    const auto page = writer.AddPage();
    PDFPP_TEST_CHECK(page == 0);
    PDFPP_TEST_CHECK(writer.GetPageCount() == 1);
    PDFPP_TEST_CHECK(writer.GetPageMediaBox(0).right == 595);
    ExpectThrows([&] { (void)writer.GetCanvas(1); });
    ExpectThrows([&] { writer.RemovePage(1); });
    ExpectThrows([&] { writer.MovePage(0, 2); });
    ExpectThrows([&] { (void)writer.InsertPage(3); });
}

void TestPageEditingAndOrganization(const std::filesystem::path& base) {
    const auto edited = TempPath("pdfpp_api_edited.pdf");
    PdfPageEdit edit;
    edit.pageIndex = 0;
    edit.foregroundContent = PdfContentCommands::DrawLine({0, 0}, {100, 100});
    edit.rotation = 90;
    edit.cropBox = PdfRectangle{0, 0, 250, 350};
    const auto result = PdfPageEditor::ApplyEdits(base, edited, {edit});
    PDFPP_TEST_CHECK(result.modifiedPageCount == 1);
    PDFPP_TEST_CHECK(result.appendedContentStreamCount == 1);
    auto document = PdfDocument::Open(edited);
    PDFPP_TEST_CHECK(document.GetPageInfo(0).rotation == 90);
    PDFPP_TEST_CHECK(document.GetPageInfo(0).cropBox.right == 250);
    PDFPP_TEST_CHECK(!document.GetPageMediaBox(0).empty());
    const auto allPagesText = document.GetAllPagesText();
    PDFPP_TEST_CHECK(!allPagesText.empty());

    const auto background = TempPath("pdfpp_api_background.pdf");
    const auto backgroundResult = PdfPageEditor::AddContent(
        edited, background, 1, PdfContentCommands::FillRectangle({0, 0, 10, 10}, 0, 0, 1),
        PdfContentLayer::Background);
    PDFPP_TEST_CHECK(backgroundResult.modifiedPageCount == 1);

    ExpectThrows([&] { (void)PdfPageEditor::AddContent(base, edited, 99, "q Q"); });

    const auto reordered = TempPath("pdfpp_api_reordered.pdf");
    const auto reorderResult = PdfPageOrganizer::ReorderPages(base, reordered, {1, 0});
    PDFPP_TEST_CHECK(reorderResult.outputPageCount == 2);
    PDFPP_TEST_CHECK(PdfDocument::Open(reordered).GetPageText(0).find("page two") != std::string::npos);
    ExpectThrows([&] { (void)PdfPageOrganizer::ReorderPages(base, reordered, {0, 0}); });
    ExpectThrows([&] { (void)PdfPageOrganizer::RemovePages(base, reordered, {0, 1}); });
    ExpectThrows([&] { (void)PdfPageOrganizer::ExtractPages(base, reordered, {}); });
    ExpectThrows([&] { (void)PdfPageOrganizer::SplitEvery(base, TempPath("pdfpp_invalid_split"), 0); });
    ExpectThrows([&] { (void)PdfPageOrganizer::DuplicatePages(base, reordered, {9}); });

    const auto duplicated = TempPath("pdfpp_api_duplicated.pdf");
    const auto duplicateResult = PdfPageOrganizer::DuplicatePages(base, duplicated, {0});
    PDFPP_TEST_CHECK(duplicateResult.originalPageCount == 2U);
    PDFPP_TEST_CHECK(duplicateResult.outputPageCount == 3U);
    PDFPP_TEST_CHECK(PdfDocument::Open(duplicated).GetPageCount() == 3U);

    // Crop and resize a page box.
    const auto cropped = TempPath("pdfpp_api_cropped.pdf");
    PDFPP_TEST_CHECK(PdfPageEditor::SetPageBox(base, cropped, 0U,
        PdfRectangle{0, 0, 200, 300}, true) == 1U);
    auto croppedDocument = PdfDocument::Open(cropped);
    PDFPP_TEST_CHECK(croppedDocument.GetPageInfo(0U).cropBox.right == 200);
    PDFPP_TEST_CHECK(croppedDocument.GetPageInfo(0U).cropBox.top == 300);
    ExpectThrows([&] { (void)PdfPageEditor::SetPageBox(base, cropped, 99U, PdfRectangle{}, true); });

    // Rotate a page and read it back.
    const auto rotated = TempPath("pdfpp_api_rotated.pdf");
    PDFPP_TEST_CHECK(PdfPageEditor::SetPageRotation(base, rotated, 0U, 90) == 1U);
    PDFPP_TEST_CHECK(PdfDocument::Open(rotated).GetPageInfo(0U).rotation == 90);
    ExpectThrows([&] { (void)PdfPageEditor::SetPageRotation(base, rotated, 0U, 45); });

    const auto splitDirectory = TempPath("pdfpp_api_split");
    std::filesystem::remove_all(splitDirectory);
    const auto split = PdfPageOrganizer::SplitEvery(base, splitDirectory, 1, "api");
    PDFPP_TEST_CHECK(split.size() == 2);
    PDFPP_TEST_CHECK(PdfDocument::Open(split[0].outputPath).GetPageCount() == 1);
}

void TestImport(const std::filesystem::path& base) {
    const auto merged = TempPath("pdfpp_api_merged.pdf");
    PdfPageImportOptions options;
    options.preserveAcroForm = false;
    const auto mergeResult = PdfPageImporter::MergeDocuments({base, base}, merged, options);
    PDFPP_TEST_CHECK(mergeResult.sourceDocumentCount == 2);
    PDFPP_TEST_CHECK(mergeResult.importedPageCount == 4);
    PDFPP_TEST_CHECK(PdfDocument::Open(merged).GetPageCount() == 4);

    PdfPageImportSource first{base, {1}};
    PdfPageImportSource second{base, {0}};
    const auto copied = TempPath("pdfpp_api_copied.pdf");
    const auto copyResult = PdfPageImporter::CopyPages({first, second}, copied);
    PDFPP_TEST_CHECK(copyResult.importedPageCount == 2);
    auto copyDocument = PdfDocument::Open(copied);
    PDFPP_TEST_CHECK(copyDocument.GetPageText(0).find("page two") != std::string::npos);
    PDFPP_TEST_CHECK(copyDocument.GetPageText(1).find("page one") != std::string::npos);

    ExpectThrows([&] { (void)PdfPageImporter::MergeDocuments({}, merged); });
    ExpectThrows([&] {
        PdfPageImportSource invalid{base, {99}};
        (void)PdfPageImporter::CopyPages({invalid}, copied);
    });
}

void TestAnnotationsAndHighlight(const std::filesystem::path& base) {
    const auto annotated = TempPath("pdfpp_api_annotated.pdf");
    std::vector<PdfAnnotation> annotations;
    for (const auto type : {PdfAnnotationType::Highlight, PdfAnnotationType::Underline,
                            PdfAnnotationType::StrikeOut, PdfAnnotationType::TextNote,
                            PdfAnnotationType::Link}) {
        PdfAnnotation annotation;
        annotation.pageIndex = 0;
        annotation.type = type;
        annotation.rectangle = {20, 330, 120, 360};
        annotation.quadrilaterals = {{20, 330, 120, 360}};
        annotation.contents = "API annotation";
        annotation.title = "Pdf++";
        annotation.uri = "https://example.com";
        annotations.push_back(annotation);
    }
    const auto annotationResult = PdfAnnotationEditor::AddAnnotations(base, annotated, annotations);
    PDFPP_TEST_CHECK(annotationResult.annotationCount == annotations.size());
    PDFPP_TEST_CHECK(annotationResult.modifiedPageCount == 1);
    const auto readAnnotations = PdfDocument::Open(annotated).GetAnnotations(0U);
    PDFPP_TEST_CHECK(readAnnotations.size() == annotations.size());
    PDFPP_TEST_CHECK(readAnnotations[0].subtype == "Highlight");
    PDFPP_TEST_CHECK(!readAnnotations[0].contents.empty());
    PDFPP_TEST_CHECK(PdfDocument::Open(annotated).GetPageCount() == 2);

    PdfKeywordHighlightOptions highlight;
    highlight.keyword = "API page";
    const auto highlighted = TempPath("pdfpp_api_highlighted.pdf");
    const auto highlightResult = PdfKeywordHighlighter::HighlightFile(base, highlighted, highlight);
    PDFPP_TEST_CHECK(highlightResult.MatchCount() == 2);
    // FindMatches searches without writing a new file.
    const auto findMatches = PdfKeywordHighlighter::FindMatches(base, highlight);
    PDFPP_TEST_CHECK(findMatches.MatchCount() == 2);
    PDFPP_TEST_CHECK(findMatches.matches[0].pageIndex == 0);
    PDFPP_TEST_CHECK(!findMatches.matches[0].rectangle.empty());
    ExpectThrows([&] {
        PdfKeywordHighlightOptions empty;
        (void)PdfKeywordHighlighter::HighlightFile(base, highlighted, empty);
    });
}

void TestAdvancedAnnotationsAndXfdf(const std::filesystem::path& base) {
    // FreeText, Ink, Polygon, Polyline, Square, Circle and Stamp types.
    const auto advanced = TempPath("pdfpp_api_advanced_annotations.pdf");
    std::vector<PdfAnnotation> annotations;
    {
        PdfAnnotation freeText;
        freeText.pageIndex = 0;
        freeText.type = PdfAnnotationType::FreeText;
        freeText.rectangle = {20, 200, 220, 260};
        freeText.contents = "Free text note";
        freeText.textAlignment = 1;
        annotations.push_back(freeText);
    }
    {
        PdfAnnotation ink;
        ink.pageIndex = 0;
        ink.type = PdfAnnotationType::Ink;
        ink.rectangle = {20, 150, 120, 190};
        ink.inkPaths = {{{30, 160}, {50, 180}, {80, 165}, {110, 185}}};
        annotations.push_back(ink);
    }
    {
        PdfAnnotation polygon;
        polygon.pageIndex = 0;
        polygon.type = PdfAnnotationType::Polygon;
        polygon.rectangle = {20, 100, 120, 140};
        polygon.vertices = {{25, 105}, {115, 108}, {110, 135}, {30, 130}};
        polygon.interiorColor = {0.8, 0.9, 0.95};
        polygon.lineStart = PdfLineEndStyle::ClosedArrow;
        polygon.lineEnd = PdfLineEndStyle::Diamond;
        annotations.push_back(polygon);
    }
    {
        PdfAnnotation polyline;
        polyline.pageIndex = 0;
        polyline.type = PdfAnnotationType::Polyline;
        polyline.rectangle = {20, 60, 120, 90};
        polyline.vertices = {{25, 65}, {60, 85}, {115, 62}};
        annotations.push_back(polyline);
    }
    {
        PdfAnnotation square;
        square.pageIndex = 0;
        square.type = PdfAnnotationType::Square;
        square.rectangle = {200, 60, 280, 120};
        square.borderWidth = 2.5;
        annotations.push_back(square);
    }
    {
        PdfAnnotation circle;
        circle.pageIndex = 0;
        circle.type = PdfAnnotationType::Circle;
        circle.rectangle = {200, 130, 280, 190};
        circle.borderWidth = 1.5;
        circle.interiorColor = {0.9, 0.9, 0.9};
        annotations.push_back(circle);
    }
    {
        PdfAnnotation stamp;
        stamp.pageIndex = 0;
        stamp.type = PdfAnnotationType::Stamp;
        stamp.rectangle = {300, 200, 420, 260};
        stamp.stampName = "Draft";
        stamp.rotationDegrees = 15.0;
        annotations.push_back(stamp);
    }
    const auto addResult = PdfAnnotationEditor::AddAnnotations(base, advanced, annotations);
    PDFPP_TEST_CHECK(addResult.annotationCount == annotations.size());
    PDFPP_TEST_CHECK(addResult.modifiedPageCount == 1U);
    auto advancedDocument = PdfDocument::Open(advanced);
    const PdfDictionary* advancedPage = advancedDocument.GetObject(
        advancedDocument.GetPageReference(0U)).AsDictionary();
    PDFPP_TEST_CHECK(advancedPage != nullptr);
    const PdfArray* advancedAnnots = advancedPage->GetAsArray(PdfName("Annots"));
    PDFPP_TEST_CHECK(advancedAnnots != nullptr && advancedAnnots->size() == 7U);
    bool foundFreeText = false;
    bool foundInkList = false;
    bool foundVertices = false;
    bool foundDraft = false;
    for (const auto& value : advancedAnnots->values()) {
        const auto reference = value.AsReference();
        if (!reference) continue;
        const PdfDictionary* annotation = advancedDocument.GetObject(
            PdfReference{reference->first, reference->second}).AsDictionary();
        if (!annotation) continue;
        const PdfObject* subtype = annotation->Find(PdfName("Subtype"));
        const auto subtypeName = subtype ? subtype->AsName() : nullptr;
        if (subtypeName && subtypeName->value() == "FreeText") foundFreeText = true;
        if (annotation->Find(PdfName("InkList"))) foundInkList = true;
        if (annotation->Find(PdfName("Vertices"))) foundVertices = true;
        const PdfObject* stampName = annotation->Find(PdfName("Name"));
        const auto stamp = stampName ? stampName->AsName() : nullptr;
        if (stamp && stamp->value() == "Draft") foundDraft = true;
    }
    PDFPP_TEST_CHECK(foundFreeText);
    PDFPP_TEST_CHECK(foundInkList);
    PDFPP_TEST_CHECK(foundVertices);
    PDFPP_TEST_CHECK(foundDraft);

    // RemoveAnnotations: drop just the FreeText entry, keep the rest.
    const auto removed = TempPath("pdfpp_api_removed_annotations.pdf");
    const auto removeResult = PdfAnnotationEditor::RemoveAnnotations(
        advanced, removed, 0U, {"/FreeText"});
    PDFPP_TEST_CHECK(removeResult.removedCount == 1U);
    PDFPP_TEST_CHECK(removeResult.modifiedPageCount == 1U);
    auto removedDocument = PdfDocument::Open(removed);
    const PdfDictionary* removedPage = removedDocument.GetObject(
        removedDocument.GetPageReference(0U)).AsDictionary();
    const PdfArray* keptAnnots = removedPage->GetAsArray(PdfName("Annots"));
    PDFPP_TEST_CHECK(keptAnnots != nullptr && keptAnnots->size() == 6U);

    // RemoveAnnotations with no filter removes everything.
    const auto removedAll = TempPath("pdfpp_api_removed_all.pdf");
    const auto removeAllResult = PdfAnnotationEditor::RemoveAnnotations(advanced, removedAll, 0U);
    PDFPP_TEST_CHECK(removeAllResult.removedCount == 7U);
    auto removedAllDocument = PdfDocument::Open(removedAll);
    const PdfDictionary* clearedPage = removedAllDocument.GetObject(
        removedAllDocument.GetPageReference(0U)).AsDictionary();
    const PdfArray* clearedAnnots = clearedPage->GetAsArray(PdfName("Annots"));
    PDFPP_TEST_CHECK(clearedAnnots == nullptr || clearedAnnots->empty());

    // UpdateAnnotationContents rewrites the FreeText contents.
    const auto updated = TempPath("pdfpp_api_updated_annotation.pdf");
    const auto updatedCount = PdfAnnotationEditor::UpdateAnnotationContents(
        advanced, updated, 0U, PdfAnnotationType::FreeText, "Revised note", "New title");
    PDFPP_TEST_CHECK(updatedCount == 1U);
    const std::string updatedBytes = ReadBytesAsString(updated);
    PDFPP_TEST_CHECK(updatedBytes.find("Revised note") != std::string::npos);

    // XFDF export then import round trip.
    const auto xfdfPath = TempPath("pdfpp_api_annotations.xfdf");
    const auto exportResult = PdfXfdf::ExportAnnotations(advanced, 0U, xfdfPath);
    PDFPP_TEST_CHECK(exportResult.annotationCount == 7U);
    const std::string xfdfBytes = ReadBytesAsString(xfdfPath);
    PDFPP_TEST_CHECK(xfdfBytes.find("<xfdf") != std::string::npos);
    PDFPP_TEST_CHECK(xfdfBytes.find("<freetext") != std::string::npos);
    PDFPP_TEST_CHECK(xfdfBytes.find("<stamp") != std::string::npos);

    const auto reimported = TempPath("pdfpp_api_xfdf_reimported.pdf");
    const auto importResult = PdfXfdf::ImportAnnotations(base, 0U, xfdfPath, reimported);
    PDFPP_TEST_CHECK(importResult.addedCount >= 7U);
    auto reimportedDocument = PdfDocument::Open(reimported);
    PDFPP_TEST_CHECK(reimportedDocument.GetPageCount() == 2U);

    // GenerateAppearances: every annotation gets an /AP /N Form XObject.
    const auto appeared = TempPath("pdfpp_api_appeared_annotations.pdf");
    const auto appearanceResult = PdfAnnotationEditor::GenerateAppearances(advanced, appeared, 0U);
    PDFPP_TEST_CHECK(appearanceResult.appearanceCount == 7U);
    PDFPP_TEST_CHECK(appearanceResult.modifiedPageCount == 1U);
    auto appearedDocument = PdfDocument::Open(appeared);
    const PdfDictionary* appearedPage = appearedDocument.GetObject(
        appearedDocument.GetPageReference(0U)).AsDictionary();
    const PdfArray* appearedAnnots = appearedPage->GetAsArray(PdfName("Annots"));
    PDFPP_TEST_CHECK(appearedAnnots != nullptr && appearedAnnots->size() == 7U);
    std::size_t apCount = 0U;
    for (const auto& value : appearedAnnots->values()) {
        const auto ref = value.AsReference();
        if (!ref) continue;
        const PdfDictionary* annotation = appearedDocument.GetObject(
            PdfReference{ref->first, ref->second}).AsDictionary();
        if (annotation && annotation->Find(PdfName("AP"))) ++apCount;
    }
    PDFPP_TEST_CHECK(apCount == 7U);

    // FlattenAnnotations: burn annotations into the page and remove them from
    // /Annots, then verify the rendered text is preserved.
    const auto flattened = TempPath("pdfpp_api_flattened_annotations.pdf");
    const auto flattenResult = PdfAnnotationEditor::FlattenAnnotations(advanced, flattened, 0U);
    PDFPP_TEST_CHECK(flattenResult.flattenedCount == 7U);
    PDFPP_TEST_CHECK(flattenResult.removedCount == 7U);
    PDFPP_TEST_CHECK(flattenResult.modifiedPageCount == 1U);
    auto flattenedDocument = PdfDocument::Open(flattened);
    const PdfDictionary* flattenedPage = flattenedDocument.GetObject(
        flattenedDocument.GetPageReference(0U)).AsDictionary();
    const PdfArray* flattenedAnnots = flattenedPage->GetAsArray(PdfName("Annots"));
    PDFPP_TEST_CHECK(flattenedAnnots == nullptr || flattenedAnnots->empty());
    PDFPP_TEST_CHECK(flattenedDocument.GetPageText(0U).find("API page one") != std::string::npos);

    // Flatten with a subtype filter keeps the other annotations.
    const auto partialFlattened = TempPath("pdfpp_api_partial_flattened.pdf");
    const auto partialResult = PdfAnnotationEditor::FlattenAnnotations(
        advanced, partialFlattened, 0U, {"/Square", "/Circle"});
    PDFPP_TEST_CHECK(partialResult.flattenedCount == 2U);
    PDFPP_TEST_CHECK(partialResult.removedCount == 2U);
    auto partialDocument = PdfDocument::Open(partialFlattened);
    const PdfDictionary* partialPage = partialDocument.GetObject(
        partialDocument.GetPageReference(0U)).AsDictionary();
    const PdfArray* partialAnnots = partialPage->GetAsArray(PdfName("Annots"));
    PDFPP_TEST_CHECK(partialAnnots != nullptr && partialAnnots->size() == 5U);

    // Replies and popups: a TextNote with a popup, and a reply targeting it.
    const auto threaded = TempPath("pdfpp_api_threaded.pdf");
    std::vector<PdfAnnotation> thread;
    {
        PdfAnnotation note;
        note.pageIndex = 0;
        note.type = PdfAnnotationType::TextNote;
        note.rectangle = {20, 300, 60, 330};
        note.contents = "Original comment";
        note.hasPopup = true;
        note.open = true;
        thread.push_back(note);
    }
    {
        PdfAnnotation reply;
        reply.pageIndex = 0;
        reply.type = PdfAnnotationType::TextNote;
        reply.rectangle = {70, 300, 110, 330};
        reply.contents = "Reply comment";
        reply.inReplyTo = 1U;
        reply.replyType = PdfAnnotationReplyType::R;
        thread.push_back(reply);
    }
    const auto threadResult = PdfAnnotationEditor::AddAnnotations(base, threaded, thread);
    PDFPP_TEST_CHECK(threadResult.annotationCount == 2U);
    auto threadedDocument = PdfDocument::Open(threaded);
    const PdfDictionary* threadedPage = threadedDocument.GetObject(
        threadedDocument.GetPageReference(0U)).AsDictionary();
    const PdfArray* threadedAnnots = threadedPage->GetAsArray(PdfName("Annots"));
    PDFPP_TEST_CHECK(threadedAnnots != nullptr && threadedAnnots->size() == 3U);
    bool foundIrt = false;
    bool foundPopup = false;
    bool foundPopupParent = false;
    for (const auto& value : threadedAnnots->values()) {
        const auto ref = value.AsReference();
        if (!ref) continue;
        const PdfDictionary* annotation = threadedDocument.GetObject(
            PdfReference{ref->first, ref->second}).AsDictionary();
        if (!annotation) continue;
        const PdfObject* subtype = annotation->Find(PdfName("Subtype"));
        const auto subtypeName = subtype ? subtype->AsName() : nullptr;
        if (subtypeName && subtypeName->value() == "Text") {
            if (annotation->Find(PdfName("IRT"))) foundIrt = true;
            if (annotation->Find(PdfName("Popup"))) foundPopup = true;
        } else if (subtypeName && subtypeName->value() == "Popup") {
            if (annotation->Find(PdfName("Parent"))) foundPopupParent = true;
        }
    }
    PDFPP_TEST_CHECK(foundIrt);
    PDFPP_TEST_CHECK(foundPopup);
    PDFPP_TEST_CHECK(foundPopupParent);

    std::filesystem::remove(advanced);
    std::filesystem::remove(removed);
    std::filesystem::remove(removedAll);
    std::filesystem::remove(updated);
    std::filesystem::remove(xfdfPath);
    std::filesystem::remove(reimported);
    std::filesystem::remove(appeared);
    std::filesystem::remove(flattened);
    std::filesystem::remove(partialFlattened);
    std::filesystem::remove(threaded);
}

void TestAcroFormCalculations() {
    // Build a PDF with an AcroForm: two numeric input fields (A=10, B=5) and a
    // calculated field `total` whose /AA /C script is `total = A + B`.
    std::ostringstream pdf;
    pdf << "%PDF-1.7\n";
    std::array<std::size_t, 11> offsets{};
    const auto object = [&](const std::size_t number, const std::string& body) {
        offsets[number] = static_cast<std::size_t>(pdf.tellp());
        pdf << number << " 0 obj\n" << body << "\nendobj\n";
    };
    object(1, "<< /Type /Catalog /Pages 2 0 R /AcroForm 7 0 R >>");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    object(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] "
              "/Resources << /Font << /F1 6 0 R >> >> /Contents 5 0 R >>");
    const std::string content = "BT /F1 12 Tf 20 350 Td (calc) Tj ET";
    object(5, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n"
              + content + "endstream");
    object(6, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    object(7, "<< /Fields [8 0 R 9 0 R 10 0 R] /DA (/Helv 0 Tf 0 g) "
              "/DR << /Font << /Helv 6 0 R >> >> >>");
    object(8, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (A) /V (10) /Rect [20 20 80 30] /P 3 0 R >>");
    object(10, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (B) /V (5) /Rect [20 40 80 50] /P 3 0 R >>");
    const std::string calcJs = "total = A + B";
    object(9, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (total) /V () /Rect [100 20 200 30] /P 3 0 R "
              "/AA << /C << /Type /Action /S /JavaScript /JS (" + calcJs + ") >> >> >>");
    const std::size_t xrefOffset = static_cast<std::size_t>(pdf.tellp());
    pdf << "xref\n0 11\n0000000000 65535 f \n";
    for (std::size_t i = 1; i <= 10U; ++i) {
        pdf << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    pdf << "trailer\n<< /Size 11 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    const auto input = TempPath("pdfpp_api_calc_input.pdf");
    {
        std::ofstream file(input, std::ios::binary);
        file << pdf.str();
    }
    // Set A=10 and B=5.
    const auto updated = TempPath("pdfpp_api_calc_updated.pdf");
    const auto updateResult = PdfAcroForm::SetFieldValues(input, updated, {{"A", "10"}, {"B", "5"}});
    PDFPP_TEST_CHECK(updateResult.updatedFieldCount == 2U);
    const auto calcOutput = TempPath("pdfpp_api_calc_output.pdf");
    const auto calcResult = PdfAcroForm::CalculateFields(updated, calcOutput);
    PDFPP_TEST_CHECK(calcResult.calculatedFieldCount == 1U);
    const auto fields = PdfAcroForm::GetFields(calcOutput);
    bool foundTotal = false;
    for (const auto& field : fields) {
        if (field.name == "total") {
            foundTotal = true;
            PDFPP_TEST_CHECK(field.value == "15"); // A(10) + B(5)
        }
    }
    PDFPP_TEST_CHECK(foundTotal);
    std::filesystem::remove(input);
    std::filesystem::remove(updated);
    std::filesystem::remove(calcOutput);
}

void TestCApi() {
    // The C ABI mirrors the core through opaque handles.
    PDFPP_TEST_CHECK(std::string(::pdfpp_version()) == CPPPdf::VersionString);
    const auto base = TempPath("pdfpp_capi_base.pdf");
    {
        PdfWriter writer;
        const auto page = writer.AddPage({0, 0, 120, 120});
        writer.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(10, 100).ShowText("C API text").EndText();
        writer.Save(base);
    }
    char errbuf[256] = {};
    int pageCount = 0;
    PdfDocumentHandle doc = ::pdfpp_open(base.string().c_str(), &pageCount, errbuf, sizeof(errbuf));
    PDFPP_TEST_CHECK(doc != nullptr);
    PDFPP_TEST_CHECK(pageCount == 1);
    char text[64] = {};
    const int written = ::pdfpp_page_text(doc, 0, text, sizeof(text), errbuf, sizeof(errbuf));
    PDFPP_TEST_CHECK(written > 0);
    PDFPP_TEST_CHECK(std::string(text).find("C API") != std::string::npos);
    const auto ppm = TempPath("pdfpp_capi_render.ppm");
    PDFPP_TEST_CHECK(::pdfpp_render_ppm(doc, 0, 72.0, ppm.string().c_str(), errbuf, sizeof(errbuf)) == 0);
    PDFPP_TEST_CHECK(std::filesystem::exists(ppm));
    ::pdfpp_close(doc);
    PDFPP_TEST_CHECK(::pdfpp_page_count(nullptr, errbuf, sizeof(errbuf)) == -1);
    std::filesystem::remove(base);
    std::filesystem::remove(ppm);
}

} // namespace


void TestWriterDocumentInfo() {
    const auto output = TempPath("pdfpp_document_info.pdf");
    PdfWriter writer;
    const auto metadataPage = writer.AddPage();
    PDFPP_TEST_CHECK(metadataPage == 0);
    writer.SetTitle("Pdf++ metadata (test)");
    writer.SetAuthor("Thang Nguyen");
    writer.SetSubject("Writer document information API");
    writer.SetKeywords("pdf, metadata, cpp");
    writer.SetCreator("Pdf++.UnitTests");
    writer.SetProducer("Pdf++ Core");
    writer.SetCreationDate("D:20260731215500+07'00'");
    writer.SetModificationDate("D:20260731220000+07'00'");

    PDFPP_TEST_CHECK(writer.GetDocumentInfo().title == "Pdf++ metadata (test)");
    writer.Save(output);

    const auto document = PdfDocument::Open(output);
    const auto info = document.GetDocumentInfo();
    PDFPP_TEST_CHECK(info.title == "Pdf++ metadata (test)");
    PDFPP_TEST_CHECK(info.author == "Thang Nguyen");
    PDFPP_TEST_CHECK(info.subject == "Writer document information API");
    PDFPP_TEST_CHECK(info.keywords == "pdf, metadata, cpp");
    PDFPP_TEST_CHECK(info.creator == "Pdf++.UnitTests");
    PDFPP_TEST_CHECK(info.producer == "Pdf++ Core");
    PDFPP_TEST_CHECK(info.creationDate == "D:20260731215500+07'00'");
    PDFPP_TEST_CHECK(info.modificationDate == "D:20260731220000+07'00'");
    std::filesystem::remove(output);

    // XMP metadata packet embedded in the catalog.
    const auto xmpOutput = TempPath("pdfpp_document_info_xmp.pdf");    PdfWriter xmpWriter;
    xmpWriter.AddPage();
    xmpWriter.SetTitle("XMP test");
    xmpWriter.SetAuthor("Author Name");
    xmpWriter.SetXmpMetadata(true);
    PDFPP_TEST_CHECK(xmpWriter.GetXmpMetadataEnabled());
    xmpWriter.Save(xmpOutput);
    const std::string xmpBytes = ReadBytesAsString(xmpOutput);
    PDFPP_TEST_CHECK(xmpBytes.find("/Metadata") != std::string::npos);
    PDFPP_TEST_CHECK(xmpBytes.find("xpacket") != std::string::npos);
    PDFPP_TEST_CHECK(xmpBytes.find("XMP test") != std::string::npos);
    const std::string readXmp = PdfDocument::Open(xmpOutput).GetXmpMetadata();
    PDFPP_TEST_CHECK(readXmp.find("xpacket") != std::string::npos);
    PDFPP_TEST_CHECK(readXmp.find("XMP test") != std::string::npos);
    std::filesystem::remove(xmpOutput);

    // Resize a page after creation.
    PdfWriter sizeWriter;
    const auto sizePage = sizeWriter.AddPage();
    sizeWriter.SetPageSize(sizePage, PdfRectangle{0, 0, 400, 600});
    PDFPP_TEST_CHECK(sizeWriter.GetPageMediaBox(sizePage).right == 400);
    ExpectThrows([&] { sizeWriter.SetPageSize(9U, PdfRectangle{0, 0, 1, 1}); });
    const auto sizePath = TempPath("pdfpp_page_size.pdf");
    sizeWriter.Save(sizePath);
    PDFPP_TEST_CHECK(PdfDocument::Open(sizePath).GetPageMediaBox(0U).top == 600);
    std::filesystem::remove(sizePath);

    // Rotate a page from the writer.
    PdfWriter rotateWriter;
    const auto rotatePage = rotateWriter.AddPage();
    rotateWriter.SetPageRotation(rotatePage, 90);
    PDFPP_TEST_CHECK(rotateWriter.GetPageRotation(rotatePage) == 90);
    ExpectThrows([&] { rotateWriter.SetPageRotation(rotatePage, 45); });
    const auto rotatePath = TempPath("pdfpp_writer_rotate.pdf");
    rotateWriter.Save(rotatePath);
    PDFPP_TEST_CHECK(PdfDocument::Open(rotatePath).GetPageInfo(0U).rotation == 90);
    std::filesystem::remove(rotatePath);

    // Crop box from the writer.
    PdfWriter cropWriter;
    const auto cropPage = cropWriter.AddPage();
    cropWriter.SetPageCropBox(cropPage, PdfRectangle{0, 0, 300, 400});
    PDFPP_TEST_CHECK(cropWriter.GetPageCropBox(cropPage).right == 300);
    const auto cropPath = TempPath("pdfpp_writer_crop.pdf");
    cropWriter.Save(cropPath);
    const auto cropDocument = PdfDocument::Open(cropPath);
    PDFPP_TEST_CHECK(cropDocument.GetPageInfo(0U).cropBox.right == 300);
    PDFPP_TEST_CHECK(cropDocument.GetPageCropBox(0U).right == 300);
    PDFPP_TEST_CHECK(cropDocument.GetPageRotation(0U) == 0);
    std::filesystem::remove(cropPath);
}


void TestWriterBookmarks() {
    const auto output = TempPath("pdfpp_bookmarks.pdf");
    PdfWriter writer;
    const auto firstPage = writer.AddPage({0, 0, 300, 400});
    const auto secondPage = writer.AddPage({0, 0, 400, 500});
    const auto thirdPage = writer.AddPage({0, 0, 500, 600});
    PDFPP_TEST_CHECK(firstPage == 0 && secondPage == 1 && thirdPage == 2);

    PdfBookmarkOptions chapter;
    chapter.title = "Chapter (1)";
    chapter.pageIndex = 0;
    chapter.bold = true;
    chapter.color = PdfColor::Blue();
    const auto chapterIndex = writer.AddBookmark(chapter);

    PdfBookmarkOptions section;
    section.title = "Section 1.1";
    section.pageIndex = 1;
    section.parentIndex = chapterIndex;
    section.destinationType = PdfBookmarkDestinationType::XYZ;
    section.left = 24.0;
    section.top = 470.0;
    section.zoom = 1.25;
    section.italic = true;
    section.open = false;
    const auto sectionIndex = writer.AddBookmark(section);
    PDFPP_TEST_CHECK(sectionIndex == 1);

    PdfBookmarkOptions appendix;
    appendix.title = "Appendix";
    appendix.pageIndex = 2;
    appendix.destinationType = PdfBookmarkDestinationType::FitWidth;
    appendix.top = 580.0;
    const auto appendixIndex = writer.AddBookmark(appendix);
    PDFPP_TEST_CHECK(appendixIndex == 2);

    PDFPP_TEST_CHECK(writer.GetBookmarkCount() == 3);
    const auto insertedPage = writer.InsertPage(1, {0, 0, 200, 200});
    PDFPP_TEST_CHECK(insertedPage == 1);
    writer.MovePage(3, 1);
    PDFPP_TEST_CHECK(writer.GetPageCount() == 4);
    PDFPP_TEST_CHECK(writer.GetBookmarkCount() == 3);
    writer.Save(output);

    const auto document = PdfDocument::Open(output);
    const auto* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    PDFPP_TEST_CHECK(catalog != nullptr);
    PDFPP_TEST_CHECK(catalog->Contains(PdfName("Outlines")));
    PDFPP_TEST_CHECK(catalog->Get(PdfName("PageMode")).AsName()->value() == "UseOutlines");

    const auto outlinesReference = *catalog->Get(PdfName("Outlines")).AsReference();
    const auto* outlines = document.GetObject({outlinesReference.first, outlinesReference.second}).AsDictionary();
    PDFPP_TEST_CHECK(outlines != nullptr);
    PDFPP_TEST_CHECK(outlines->Get(PdfName("Count")).AsInteger().value() == 3);

    const auto firstReference = *outlines->Get(PdfName("First")).AsReference();
    const auto* first = document.GetObject({firstReference.first, firstReference.second}).AsDictionary();
    PDFPP_TEST_CHECK(first != nullptr);
    PDFPP_TEST_CHECK(*first->Get(PdfName("Title")).AsString() == "Chapter (1)");
    PDFPP_TEST_CHECK(first->Get(PdfName("F")).AsInteger().value() == 2);
    const auto* color = first->Get(PdfName("C")).AsArray();
    PDFPP_TEST_CHECK(color != nullptr && color->size() == 3);
    PDFPP_TEST_CHECK(color->at(2).AsReal().value() == 1.0);

    const auto childReference = *first->Get(PdfName("First")).AsReference();
    const auto* child = document.GetObject({childReference.first, childReference.second}).AsDictionary();
    PDFPP_TEST_CHECK(child != nullptr);
    PDFPP_TEST_CHECK(*child->Get(PdfName("Title")).AsString() == "Section 1.1");
    PDFPP_TEST_CHECK(child->Get(PdfName("F")).AsInteger().value() == 1);
    const auto* destination = child->Get(PdfName("Dest")).AsArray();
    PDFPP_TEST_CHECK(destination != nullptr && destination->size() == 5);
    PDFPP_TEST_CHECK(destination->at(1).AsName()->value() == "XYZ");
    PDFPP_TEST_CHECK(destination->at(4).AsReal().value() == 1.25);

    PdfWriter mutationWriter;
    const auto mutationPage0 = mutationWriter.AddPage();
    const auto mutationPage1 = mutationWriter.AddPage();
    PDFPP_TEST_CHECK(mutationPage0 == 0 && mutationPage1 == 1);
    PdfBookmarkOptions parentBookmark;
    parentBookmark.title = "Parent";
    parentBookmark.pageIndex = 0;
    const auto parentBookmarkIndex = mutationWriter.AddBookmark(parentBookmark);
    PdfBookmarkOptions childBookmark;
    childBookmark.title = "Child";
    childBookmark.pageIndex = 1;
    childBookmark.parentIndex = parentBookmarkIndex;
    const auto childBookmarkIndex = mutationWriter.AddBookmark(childBookmark);
    PDFPP_TEST_CHECK(childBookmarkIndex == 1);
    mutationWriter.RemovePage(0);
    PDFPP_TEST_CHECK(mutationWriter.GetPageCount() == 1);
    PDFPP_TEST_CHECK(mutationWriter.GetBookmarkCount() == 0);
    mutationWriter.ClearBookmarks();

    std::filesystem::remove(output);
}

void TestWriterEmbeddedFiles() {
    const auto output = TempPath("pdfpp_embedded_files.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage();
    const std::array<std::byte, 5> payload{
        std::byte{'H'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}};
    PdfEmbeddedFileOptions fileOptions;
    fileOptions.description = "Test attachment";
    fileOptions.mimeType = "text/plain";
    fileOptions.relationship = PdfAssociatedFileRelationship::Data;
    fileOptions.creationDate = "D:20260731224800+07'00'";
    writer.AddEmbeddedFile("notes.txt", payload, fileOptions);
    PDFPP_TEST_CHECK(writer.GetEmbeddedFileCount() == 1U);

    PdfFileAttachmentOptions attachmentOptions;
    attachmentOptions.rectangle = {36.0, 36.0, 56.0, 56.0};
    attachmentOptions.icon = PdfFileAttachmentIcon::Paperclip;
    attachmentOptions.contents = "Open notes";
    writer.AddFileAttachment(page, "notes.txt", attachmentOptions);
    PDFPP_TEST_CHECK(writer.GetFileAttachmentCount(page) == 1U);
    writer.Save(output);

    std::string pdf;
    {
        std::ifstream input(output, std::ios::binary);
        pdf.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    PDFPP_TEST_CHECK(pdf.find("/EmbeddedFiles") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/Type /EmbeddedFile") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/Type /Filespec") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/AFRelationship /Data") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/Subtype /FileAttachment") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/Name /Paperclip") != std::string::npos);
    PDFPP_TEST_CHECK(pdf.find("/Subtype /text#2Fplain") != std::string::npos);

    writer.ClearFileAttachments(page);
    PDFPP_TEST_CHECK(writer.GetFileAttachmentCount(page) == 0U);
    writer.RemoveEmbeddedFile("notes.txt");
    PDFPP_TEST_CHECK(writer.GetEmbeddedFileCount() == 0U);
    std::filesystem::remove(output);
}

void TestUnicodeTrueTypeWriting() {
    const std::array<std::filesystem::path, 5> candidates{
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/lato/Lato-Medium.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"
    };
    auto fontPath = std::find_if(candidates.begin(), candidates.end(), [](const auto& path) {
        return std::filesystem::exists(path);
    });
    PDFPP_TEST_CHECK(fontPath != candidates.end());

    const auto output = TempPath("pdfpp_unicode_truetype.pdf");
    PdfWriter writer;
    const auto page = writer.AddPage();
    const auto font = PdfTrueTypeFont::Load(*fontPath);
    PDFPP_TEST_CHECK(!font.GetBytes().empty());
    PDFPP_TEST_CHECK(font.HasTable("head"));
    PDFPP_TEST_CHECK(font.HasTable("cmap"));
    (void)font.GetCachedAdvanceWidth(65U, 16.0);
    (void)font.GetCachedAdvanceWidth(65U, 16.0);
    PDFPP_TEST_CHECK(font.GetAdvanceCacheMisses() >= 1U);
    PDFPP_TEST_CHECK(font.GetAdvanceCacheHits() >= 1U);
    (void)font.GetGlyphOutlineCached(65U);
    (void)font.GetGlyphOutlineCached(65U);
    PDFPP_TEST_CHECK(font.GetOutlineCacheMisses() >= 1U);
    PDFPP_TEST_CHECK(font.GetOutlineCacheHits() >= 1U);
    font.ClearGlyphCaches();
    PDFPP_TEST_CHECK(font.GetCachedOutlineCount() == 0U);
    writer.GetCanvas(page)
        .BeginText()
        .SetTrueTypeFontAndSize(font, 16)
        .MoveText(72, 760)
        .ShowTextUtf8("Unicode: Tiếng Việt – Ελληνικά")
        .EndText();
    writer.Save(output);

    const auto document = PdfDocument::Open(output);
    const auto text = document.GetPageText(0);
    PDFPP_TEST_CHECK(text.find("Tiếng Việt") != std::string::npos);
    PDFPP_TEST_CHECK(text.find("Ελληνικά") != std::string::npos);
    std::filesystem::remove(output);
}

void TestPublicApiArchitecture() {
    static_assert(CPPPdf::VersionMajor == 0U);
    static_assert(CPPPdf::VersionMinor == 137U);
    static_assert(CPPPdf::VersionPatch == 0U);
    static_assert(std::is_same_v<CPPPdf::PdfStampPoint, CPPPdf::PdfPoint>);

    const CPPPdf::PdfPoint point{12.0, 24.0};
    const CPPPdf::PdfRectangle rectangle{0.0, 0.0, 10.0, 20.0};
    PDFPP_TEST_CHECK((point == CPPPdf::PdfPoint{12.0, 24.0}));
    PDFPP_TEST_CHECK(rectangle.width() == 10.0);
    PDFPP_TEST_CHECK(rectangle.height() == 20.0);
    PDFPP_TEST_CHECK(!rectangle.empty());
    PDFPP_TEST_CHECK(CPPPdf::VersionString == "0.137.0");
}

int RunApiCoverageTests() {
    CPPPdfTest::TestRunner runner;
    runner.Run("API.PublicArchitecture", TestPublicApiArchitecture);
    runner.Run("API.ObjectModel", TestObjectModel);
    runner.Run("API.InputSources", TestInputSources);
    runner.Run("API.Filters", TestFilters);
    runner.Run("API.Images", TestImages);
    runner.Run("API.TextSearch", TestTextSearch);
    runner.Run("API.ContentCommands", TestContentCommands);
    runner.Run("API.WriterValidation", TestWriterValidation);
    runner.Run("API.WriterDocumentInfo", TestWriterDocumentInfo);
    runner.Run("API.WriterBookmarks", TestWriterBookmarks);
    runner.Run("API.WriterEmbeddedFiles", TestWriterEmbeddedFiles);
    runner.Run("API.UnicodeTrueTypeWriting", TestUnicodeTrueTypeWriting);

    const auto base = CreateBaseDocument();
    runner.Run("API.DocumentOpenOverloads", [&] { TestDocumentOpenOverloads(base); });
    runner.Run("API.PageEditingAndOrganization", [&] { TestPageEditingAndOrganization(base); });
    runner.Run("API.PageImport", [&] { TestImport(base); });
    runner.Run("API.AnnotationsAndHighlight", [&] { TestAnnotationsAndHighlight(base); });
    runner.Run("API.AdvancedAnnotationsAndXfdf", [&] { TestAdvancedAnnotationsAndXfdf(base); });
    runner.Run("API.AcroFormCalculations", TestAcroFormCalculations);
    runner.Run("API.CApi", TestCApi);
    std::filesystem::remove(base);
    return runner.PrintSummary("Public API coverage");
}

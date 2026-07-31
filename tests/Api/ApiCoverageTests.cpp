#include <CPPPdf/CPPPdf.hpp>

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

namespace {

using namespace CPPPdf;

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    const std::vector<char> chars((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::transform(chars.begin(), chars.end(), bytes.begin(), [](char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return bytes;
}

void ExpectThrows(const auto& operation) {
    bool thrown = false;
    try {
        operation();
    } catch (const std::exception&) {
        thrown = true;
    }
    assert(thrown);
}

void TestObjectModel() {
    PdfObject nullObject;
    assert(nullObject.IsNull());
    assert(nullObject.type() == PdfObjectType::Null);

    PdfObject boolObject(true);
    PdfObject integerObject(std::int64_t{42});
    PdfObject realObject(2.5);
    PdfObject nameObject(PdfName("Example"));
    PdfObject stringObject(std::string("value"));
    assert(boolObject.AsBoolean().value());
    assert(integerObject.AsInteger().value() == 42);
    assert(std::abs(realObject.AsReal().value() - 2.5) < 1e-12);
    assert(nameObject.AsName()->value() == "Example");
    assert(*stringObject.AsString() == "value");

    PdfArray array;
    array.push_back(PdfObject(std::int64_t{1}));
    array.push_back(PdfObject(std::string("two")));
    assert(array.size() == 2);
    assert(array.at(0).AsInteger().value() == 1);
    ExpectThrows([&] { (void)array.at(5); });

    PdfDictionary dictionary;
    dictionary.Put(PdfName("A"), PdfObject(std::int64_t{7}));
    dictionary.Put(PdfName("Items"), PdfObject(array));
    assert(dictionary.Contains(PdfName("A")));
    assert(dictionary.Get(PdfName("A")).AsInteger().value() == 7);
    assert(dictionary.GetAsArray(PdfName("Items"))->size() == 2);
    assert(dictionary.Remove(PdfName("A")));
    assert(!dictionary.Remove(PdfName("Missing")));
    ExpectThrows([&] { (void)dictionary.Get(PdfName("Missing")); });

    const auto reference = PdfObject::IndirectReference(99, 2);
    assert(reference.type() == PdfObjectType::IndirectReference);
    assert(reference.AsReference()->first == 99);
    assert(reference.AsReference()->second == 2);

    const std::array<std::byte, 3> streamBytes{std::byte{'P'}, std::byte{'D'}, std::byte{'F'}};
    PdfStream stream(PdfDictionary{}, std::vector<std::byte>(streamBytes.begin(), streamBytes.end()));
    PdfObject streamObject(std::move(stream));
    assert(streamObject.AsStream()->bytes().size() == 3);
}

void TestInputSources() {
    const std::array<std::byte, 6> bytes{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'}, std::byte{'e'}, std::byte{'f'}};

    PdfMemoryInputSource memory(bytes);
    assert(memory.Size() == bytes.size());
    std::array<char, 3> buffer{};
    memory.Read(2, buffer);
    assert(std::string(buffer.data(), buffer.size()) == "cde");
    ExpectThrows([&] { memory.Read(5, buffer); });

    std::istringstream stream("stream-source");
    PdfStreamInputSource streamSource(stream);
    assert(streamSource.Size() == 13);
    std::array<char, 6> streamBuffer{};
    streamSource.Read(7, streamBuffer);
    assert(std::string(streamBuffer.data(), streamBuffer.size()) == "source");

    const auto path = TempPath("pdfpp_api_input_source.bin");
    {
        std::ofstream output(path, std::ios::binary);
        output << "file-source";
    }
    PdfFileInputSource fileSource(path);
    assert(fileSource.Size() == 11);
    std::array<char, 4> fileBuffer{};
    fileSource.Read(5, fileBuffer);
    assert(std::string(fileBuffer.data(), fileBuffer.size()) == "sour");
    std::filesystem::remove(path);
}

void TestFilters() {
    const std::string hexText = "61 62 6 3>";
    const auto hex = PdfFilterPipeline::DecodeAsciiHex(std::as_bytes(std::span(hexText)));
    assert(std::string(reinterpret_cast<const char*>(hex.data()), hex.size()) == "abc");

    const std::string ascii85 = "87cURD]j7BEbo80";
    const auto decoded85 = PdfFilterPipeline::DecodeAscii85(std::as_bytes(std::span(ascii85)));
    assert(std::string(reinterpret_cast<const char*>(decoded85.data()), decoded85.size()) == "Hello world!");

    const std::array<std::byte, 7> runLength{
        std::byte{2}, std::byte{'A'}, std::byte{'B'}, std::byte{'C'},
        std::byte{254}, std::byte{'Z'}, std::byte{128}};
    const auto decodedRunLength = PdfFilterPipeline::DecodeRunLength(runLength);
    assert(std::string(reinterpret_cast<const char*>(decodedRunLength.data()), decodedRunLength.size()) == "ABCZZZ");

    const std::string unknownInput = "abc";
    ExpectThrows([&] {
        (void)PdfFilterPipeline{}.Decode(std::as_bytes(std::span(unknownInput)), {{"Unsupported", {}}});
    });
}

void TestImages() {
    const std::array<std::byte, 6> rgb{
        std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const auto image = PdfImage::FromRgb(2, 1, rgb);
    assert(image.GetWidth() == 2);
    assert(image.GetHeight() == 1);
    assert(image.GetColorSpace() == PdfImageColorSpace::DeviceRGB);
    assert(image.GetEncoding() == PdfImageEncoding::Raw);
    assert(image.GetBytes().size() == rgb.size());

    const std::array<std::byte, 2> gray{std::byte{1}, std::byte{2}};
    const auto grayImage = PdfImage::FromGray(2, 1, gray);
    assert(grayImage.GetColorSpace() == PdfImageColorSpace::DeviceGray);

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
    assert(matches.size() == 1);
    assert(matches[0].firstChunkIndex == 0);
    assert(matches[0].lastChunkIndex == 2);
    assert(matches[0].boundingBox.right == 55);

    PdfTextSearchOptions sensitive;
    sensitive.caseInsensitive = false;
    assert(PdfTextSearch::Find(chunks, "hello", sensitive).empty());
    ExpectThrows([&] { (void)PdfTextSearch::Find(chunks, ""); });
}

void TestContentCommands() {
    const auto line = PdfContentCommands::DrawLine({1, 2}, {3, 4}, 2, 1, 0, 0);
    assert(line.find("2 w") != std::string::npos);
    assert(line.find("1 0 0 RG") != std::string::npos);
    assert(line.find("1 2 m") != std::string::npos);

    const auto fill = PdfContentCommands::FillRectangle({10, 20, 30, 40}, 0, 1, 0);
    assert(fill.find("0 1 0 rg") != std::string::npos);
    assert(fill.find("10 20 20 20 re") != std::string::npos);

    const auto stroke = PdfContentCommands::StrokeRectangle({10, 20, 30, 40});
    assert(stroke.find(" re") != std::string::npos);
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
    assert(fileDocument.GetVersion() == "1.7");
    assert(fileDocument.GetPageCount() == 2);
    assert(fileDocument.fileSize() > 0);
    assert(fileDocument.xrefEntryCount() > 0);
    assert(fileDocument.GetCatalogReference().objectNumber > 0);
    assert(!fileDocument.IsEncrypted());
    assert(fileDocument.GetPage(0).GetMediaBox().right == 300);
    assert(fileDocument.GetPage(1).GetMediaBox().top == 600);
    assert(fileDocument.GetPageText(0).find("API page one") != std::string::npos);
    assert(fileDocument.ExtractAllPageTextParallel(2) == fileDocument.GetAllPageText());
    assert(fileDocument.GetObjectCacheCapacity() == fileDocument.readerOptions().limits.maxCachedObjects);
    fileDocument.ClearObjectCache();
    assert(fileDocument.GetCachedObjectCount() == 0);
    ExpectThrows([&] { (void)fileDocument.GetPage(50); });

    const auto bytes = ReadBytes(path);
    auto memoryDocument = PdfDocument::Open(bytes);
    assert(memoryDocument.GetPageCount() == 2);

    std::ifstream input(path, std::ios::binary);
    auto streamDocument = PdfDocument::Open(input);
    assert(streamDocument.GetPageCount() == 2);

    auto source = std::make_unique<PdfMemoryInputSource>(bytes);
    auto sourceDocument = PdfDocument::Open(std::move(source));
    assert(sourceDocument.GetPageCount() == 2);
}

void TestWriterValidation() {
    PdfWriter writer;
    assert(writer.GetPageCount() == 0);
    const auto page = writer.AddPage();
    assert(page == 0);
    assert(writer.GetPageCount() == 1);
    assert(writer.GetPageMediaBox(0).right == 595);
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
    assert(result.modifiedPageCount == 1);
    assert(result.appendedContentStreamCount == 1);
    auto document = PdfDocument::Open(edited);
    assert(document.GetPageInfo(0).rotation == 90);
    assert(document.GetPageInfo(0).cropBox.right == 250);

    const auto background = TempPath("pdfpp_api_background.pdf");
    const auto backgroundResult = PdfPageEditor::AddContent(
        edited, background, 1, PdfContentCommands::FillRectangle({0, 0, 10, 10}, 0, 0, 1),
        PdfContentLayer::Background);
    assert(backgroundResult.modifiedPageCount == 1);

    ExpectThrows([&] { (void)PdfPageEditor::AddContent(base, edited, 99, "q Q"); });

    const auto reordered = TempPath("pdfpp_api_reordered.pdf");
    const auto reorderResult = PdfPageOrganizer::ReorderPages(base, reordered, {1, 0});
    assert(reorderResult.outputPageCount == 2);
    assert(PdfDocument::Open(reordered).GetPageText(0).find("page two") != std::string::npos);
    ExpectThrows([&] { (void)PdfPageOrganizer::ReorderPages(base, reordered, {0, 0}); });
    ExpectThrows([&] { (void)PdfPageOrganizer::RemovePages(base, reordered, {0, 1}); });
    ExpectThrows([&] { (void)PdfPageOrganizer::ExtractPages(base, reordered, {}); });
    ExpectThrows([&] { (void)PdfPageOrganizer::SplitEvery(base, TempPath("pdfpp_invalid_split"), 0); });

    const auto splitDirectory = TempPath("pdfpp_api_split");
    std::filesystem::remove_all(splitDirectory);
    const auto split = PdfPageOrganizer::SplitEvery(base, splitDirectory, 1, "api");
    assert(split.size() == 2);
    assert(PdfDocument::Open(split[0].outputPath).GetPageCount() == 1);
}

void TestImport(const std::filesystem::path& base) {
    const auto merged = TempPath("pdfpp_api_merged.pdf");
    PdfPageImportOptions options;
    options.preserveAcroForm = false;
    const auto mergeResult = PdfPageImporter::MergeDocuments({base, base}, merged, options);
    assert(mergeResult.sourceDocumentCount == 2);
    assert(mergeResult.importedPageCount == 4);
    assert(PdfDocument::Open(merged).GetPageCount() == 4);

    PdfPageImportSource first{base, {1}};
    PdfPageImportSource second{base, {0}};
    const auto copied = TempPath("pdfpp_api_copied.pdf");
    const auto copyResult = PdfPageImporter::CopyPages({first, second}, copied);
    assert(copyResult.importedPageCount == 2);
    auto copyDocument = PdfDocument::Open(copied);
    assert(copyDocument.GetPageText(0).find("page two") != std::string::npos);
    assert(copyDocument.GetPageText(1).find("page one") != std::string::npos);

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
    assert(annotationResult.annotationCount == annotations.size());
    assert(annotationResult.modifiedPageCount == 1);
    assert(PdfDocument::Open(annotated).GetPageCount() == 2);

    PdfKeywordHighlightOptions highlight;
    highlight.keyword = "API page";
    const auto highlighted = TempPath("pdfpp_api_highlighted.pdf");
    const auto highlightResult = PdfKeywordHighlighter::HighlightFile(base, highlighted, highlight);
    assert(highlightResult.MatchCount() == 2);
    ExpectThrows([&] {
        PdfKeywordHighlightOptions empty;
        (void)PdfKeywordHighlighter::HighlightFile(base, highlighted, empty);
    });
}

} // namespace

int main() {
    TestObjectModel();
    TestInputSources();
    TestFilters();
    TestImages();
    TestTextSearch();
    TestContentCommands();
    TestWriterValidation();

    const auto base = CreateBaseDocument();
    TestDocumentOpenOverloads(base);
    TestPageEditingAndOrganization(base);
    TestImport(base);
    TestAnnotationsAndHighlight(base);

    std::filesystem::remove(base);
    std::cout << "Pdf++ public API coverage tests passed\n";
    return 0;
}

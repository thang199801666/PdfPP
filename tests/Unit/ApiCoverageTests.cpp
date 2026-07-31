#include <CPPPdf/Api.hpp>

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

    PdfRegexSearchOptions limitedRegex;
    limitedRegex.maxMatches = 1;
    PDFPP_TEST_CHECK(PdfTextSearch::FindRegex(chunks, R"([A-Za-z]+)", limitedRegex).size() == 1);

    ExpectThrows([&] { (void)PdfTextSearch::Find(chunks, ""); });
    ExpectThrows([&] { (void)PdfTextSearch::FindRegex(chunks, "["); });
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
    PDFPP_TEST_CHECK(PdfDocument::Open(annotated).GetPageCount() == 2);

    PdfKeywordHighlightOptions highlight;
    highlight.keyword = "API page";
    const auto highlighted = TempPath("pdfpp_api_highlighted.pdf");
    const auto highlightResult = PdfKeywordHighlighter::HighlightFile(base, highlighted, highlight);
    PDFPP_TEST_CHECK(highlightResult.MatchCount() == 2);
    ExpectThrows([&] {
        PdfKeywordHighlightOptions empty;
        (void)PdfKeywordHighlighter::HighlightFile(base, highlighted, empty);
    });
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

    std::ifstream input(output, std::ios::binary);
    const std::string pdf((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
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
    static_assert(CPPPdf::VersionMinor == 43U);
    static_assert(CPPPdf::VersionPatch == 0U);
    static_assert(std::is_same_v<CPPPdf::PdfStampPoint, CPPPdf::PdfPoint>);

    const CPPPdf::PdfPoint point{12.0, 24.0};
    const CPPPdf::PdfRectangle rectangle{0.0, 0.0, 10.0, 20.0};
    PDFPP_TEST_CHECK((point == CPPPdf::PdfPoint{12.0, 24.0}));
    PDFPP_TEST_CHECK(rectangle.width() == 10.0);
    PDFPP_TEST_CHECK(rectangle.height() == 20.0);
    PDFPP_TEST_CHECK(!rectangle.empty());
    PDFPP_TEST_CHECK(CPPPdf::VersionString == "0.43.0");
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

    std::filesystem::remove(base);
    return runner.PrintSummary("Public API coverage");
}

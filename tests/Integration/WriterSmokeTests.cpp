#include <CPPPdf/CPPPdf.hpp>
#include <cassert>
#include <filesystem>
#include <array>
#include <cstddef>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

void WriteNestedPageTreePdf(const std::filesystem::path& path) {
    std::ostringstream output;
    output << "%PDF-1.4\n";
    std::vector<std::uint64_t> offsets(10U, 0U);

    auto writeObject = [&](const std::uint32_t number, const std::string& body) {
        offsets[number] = static_cast<std::uint64_t>(output.tellp());
        output << number << " 0 obj\n" << body << "\nendobj\n";
    };

    writeObject(1U, "<< /Type /Catalog /Pages 2 0 R >>");
    writeObject(2U, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    writeObject(3U,
        "<< /Type /Pages /Parent 2 0 R /Kids [5 0 R] /Count 1 "
        "/MediaBox [0 0 300 400] /Resources << /Font << /F1 9 0 R >> >> >>");
    writeObject(4U,
        "<< /Type /Pages /Parent 2 0 R /Kids [6 0 R] /Count 1 "
        "/MediaBox [0 0 500 600] /Resources << /Font << /F1 9 0 R >> >> >>");
    writeObject(5U, "<< /Type /Page /Parent 3 0 R /Contents 7 0 R >>");
    writeObject(6U, "<< /Type /Page /Parent 4 0 R /Contents 8 0 R >>");

    const std::string firstContent = "BT /F1 12 Tf 20 350 Td (Nested first) Tj ET";
    const std::string secondContent = "BT /F1 12 Tf 20 550 Td (Nested second) Tj ET";
    writeObject(7U, "<< /Length " + std::to_string(firstContent.size()) + " >>\nstream\n" + firstContent + "\nendstream");
    writeObject(8U, "<< /Length " + std::to_string(secondContent.size()) + " >>\nstream\n" + secondContent + "\nendstream");
    writeObject(9U, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

    const std::uint64_t xrefOffset = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n0 10\n0000000000 65535 f \n";
    for (std::uint32_t number = 1U; number <= 9U; ++number) {
        output << std::setw(10) << std::setfill('0') << offsets[number] << " 00000 n \n";
    }
    output << "trailer\n<< /Size 10 /Root 1 0 R >>\nstartxref\n"
           << xrefOffset << "\n%%EOF\n";

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string bytes = output.str();
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}


void WriteCatalogStructuresPdf(const std::filesystem::path& path) {
    std::ostringstream output;
    output << "%PDF-1.7\n";
    std::vector<std::uint64_t> offsets(10U, 0U);

    auto writeObject = [&](const std::uint32_t number, const std::string& body) {
        offsets[number] = static_cast<std::uint64_t>(output.tellp());
        output << number << " 0 obj\n" << body << "\nendobj\n";
    };

    writeObject(1U,
        "<< /Type /Catalog /Pages 2 0 R /Outlines 7 0 R /Metadata 6 0 R "
        "/PageMode /UseOutlines /PageLayout /SinglePage >>");
    writeObject(2U, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    writeObject(3U,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 320 480] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    const std::string content = "BT /F1 12 Tf 20 430 Td (Catalog source page) Tj ET";
    writeObject(4U, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream");
    writeObject(5U, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    const std::string metadata = "<x:xmpmeta>Pdf++ metadata</x:xmpmeta>";
    writeObject(6U,
        "<< /Type /Metadata /Subtype /XML /Length " + std::to_string(metadata.size()) +
        " >>\nstream\n" + metadata + "\nendstream");
    writeObject(7U, "<< /Type /Outlines /First 8 0 R /Last 8 0 R /Count 1 >>");
    writeObject(8U, "<< /Title (Chapter One) /Parent 7 0 R /Dest [3 0 R /Fit] >>");
    writeObject(9U, "<< /Title (Catalog Source) /Author (PdfPP Tests) /Producer (Pdf++) >>");

    const std::uint64_t xrefOffset = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n0 10\n0000000000 65535 f \n";
    for (std::uint32_t number = 1U; number <= 9U; ++number) {
        output << std::setw(10) << std::setfill('0') << offsets[number] << " 00000 n \n";
    }
    output << "trailer\n<< /Size 10 /Root 1 0 R /Info 9 0 R >>\nstartxref\n"
           << xrefOffset << "\n%%EOF\n";

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string bytes = output.str();
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}


void WriteAcroFormPdf(
    const std::filesystem::path& path,
    const std::string& pageText,
    const std::string& fieldName,
    const std::string& fieldValue) {
    std::ostringstream output;
    output << "%PDF-1.7\n";
    std::vector<std::uint64_t> offsets(9U, 0U);

    auto writeObject = [&](const std::uint32_t number, const std::string& body) {
        offsets[number] = static_cast<std::uint64_t>(output.tellp());
        output << number << " 0 obj\n" << body << "\nendobj\n";
    };

    writeObject(1U, "<< /Type /Catalog /Pages 2 0 R /AcroForm 7 0 R >>");
    writeObject(2U, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    writeObject(3U,
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] "
        "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R /Annots [8 0 R] >>");
    const std::string content = "BT /F1 12 Tf 20 350 Td (" + pageText + ") Tj ET";
    writeObject(4U, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream");
    writeObject(5U, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    writeObject(6U, "<< >>");
    writeObject(7U,
        "<< /Fields [8 0 R] /NeedAppearances true "
        "/DA (/Helv 0 Tf 0 g) /DR << /Font << /Helv 5 0 R >> >> >>");
    writeObject(8U,
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (" + fieldName + ") "
        "/V (" + fieldValue + ") /Rect [20 20 200 40] /P 3 0 R /F 4 >>");

    const std::uint64_t xrefOffset = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n0 9\n0000000000 65535 f \n";
    for (std::uint32_t number = 1U; number <= 8U; ++number) {
        output << std::setw(10) << std::setfill('0') << offsets[number] << " 00000 n \n";
    }
    output << "trailer\n<< /Size 9 /Root 1 0 R >>\nstartxref\n"
           << xrefOffset << "\n%%EOF\n";

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string bytes = output.str();
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    using namespace CPPPdf;
    const auto path = std::filesystem::temp_directory_path() / "pdfpp_writer_phase10_test.pdf";

    PdfWriter writer;
    const auto first = writer.AddPage({0, 0, 612, 792});
    writer.GetCanvas(first)
        .SaveState().SetStrokeColor(PdfColor::Red()).SetLineWidth(2)
        .Rectangle(72, 500, 200, 100).Stroke().RestoreState()
        .BeginText().SetFontAndSize("Helvetica", 12).MoveText(72, 720)
        .ShowText("Hello Phase 10").EndText();

    const std::array<std::byte, 12> pixels{
        std::byte{0xFF}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0xFF}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0xFF},
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    const auto image = PdfImage::FromRgb(2, 2, pixels);
    writer.GetCanvas(first).DrawImage(image, {100, 200, 140, 230});

    const std::array<std::byte, 25> jpegBytes{
        std::byte{0xFF}, std::byte{0xD8},
        std::byte{0xFF}, std::byte{0xC0}, std::byte{0x00}, std::byte{0x11},
        std::byte{0x08}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x03},
        std::byte{0x01}, std::byte{0x11}, std::byte{0x00},
        std::byte{0x02}, std::byte{0x11}, std::byte{0x00},
        std::byte{0x03}, std::byte{0x11}, std::byte{0x00},
        std::byte{0xFF}, std::byte{0xD9}, std::byte{0x00}, std::byte{0x00}};
    const auto jpeg = PdfImage::FromJpeg(jpegBytes);
    assert(jpeg.GetWidth() == 2U && jpeg.GetHeight() == 1U);
    writer.GetCanvas(first).DrawImage(jpeg, {200, 200, 260, 230});

    const std::array<double, 2> dash{6.0, 3.0};
    writer.GetCanvas(first)
        .SaveState().SetStrokeColor(PdfColor::Blue()).SetFillColor(PdfColor::Gray(0.9))
        .SetOpacity(0.65).SetLineWidth(3).SetLineCap(PdfLineCap::Round)
        .SetLineJoin(PdfLineJoin::Bevel).SetDashPattern(dash)
        .Rectangle(300, 500, 120, 60).FillStroke().RestoreState()
        .SaveState().SetStrokeColor(PdfColor::Green()).DrawLine(72, 450, 300, 450).RestoreState();

    PdfTextStampOptions stamp;
    stamp.text = "APPROVED";
    stamp.position = {350, 120};
    stamp.fontSize = 18;
    stamp.textColor = PdfColor::Red();
    stamp.opacity = 0.8;
    stamp.rotationDegrees = 12;
    stamp.drawBackground = true;
    stamp.backgroundColor = PdfColor::Gray(0.95);
    stamp.drawBorder = true;
    stamp.borderColor = PdfColor::Red();
    writer.AddTextStamp(first, stamp);

    PdfWatermarkOptions watermark;
    watermark.text = "CONFIDENTIAL";
    watermark.fontSize = 42;
    watermark.opacity = 0.2;
    watermark.rotationDegrees = 35;
    watermark.layer = PdfStampLayer::Background;
    writer.AddWatermark(first, watermark);

    PdfImageStampOptions imageStamp;
    imageStamp.rectangle = {450, 50, 500, 100};
    imageStamp.opacity = 0.5;
    imageStamp.drawBorder = true;
    writer.AddImageStamp(first, image, imageStamp);

    const auto second = writer.AddPage();
    writer.GetCanvas(second).BeginText().SetFontAndSize("Helvetica", 12)
        .MoveText(72, 720).ShowText("Parallel page two").EndText();
    const auto inserted = writer.InsertPage(1);
    writer.GetCanvas(inserted).BeginText().SetFontAndSize("Helvetica", 12)
        .MoveText(72, 720).ShowText("Parallel inserted page").EndText();
    assert(writer.GetPageCount() == 3);
    writer.MovePage(2, 1);
    writer.RemovePage(2);
    assert(writer.GetPageCount() == 2);
    writer.Save(path);

    auto document = PdfDocument::Open(path);
    assert(document.GetPageCount() == 2);
    const auto sequentialText = document.GetAllPageText();
    const auto parallelText = document.ExtractAllPageTextParallel(2U);
    assert(sequentialText == parallelText);
    const auto pageText = document.GetPageText(0);
    assert(pageText.find("Hello Phase 10") != std::string::npos);
    assert(pageText.find("APPROVED") != std::string::npos);
    assert(pageText.find("CONFIDENTIAL") != std::string::npos);
    const auto images = document.ExtractImages(0);
    assert(images.size() == 3);
    assert(images[0].info.width == 2);
    assert(images[0].info.height == 2);
    assert(images[0].info.colorSpace == PdfImageColorSpace::DeviceRGB);
    assert(images[0].info.encoding == PdfImageEncoding::Flate);
    assert(images[0].info.decoded);
    assert(images[0].decodedBytes.size() == pixels.size());
    assert(images[0].info.boundingBox.left == 100.0);
    assert(images[0].info.boundingBox.bottom == 200.0);
    assert(images[0].info.boundingBox.right == 140.0);
    assert(images[0].info.boundingBox.top == 230.0);
    assert(images[1].info.encoding == PdfImageEncoding::Dct);
    assert(!images[1].info.decoded);
    assert(images[1].info.width == 2U && images[1].info.height == 1U);
    assert(images[1].encodedBytes.size() == jpegBytes.size());
    assert(std::equal(images[1].encodedBytes.begin(), images[1].encodedBytes.end(), jpegBytes.begin(), jpegBytes.end()));

    const auto stampedPath = std::filesystem::temp_directory_path() / "pdfpp_existing_stamp_test.pdf";
    PdfTextStampOptions existingStamp;
    existingStamp.text = "EXISTING STAMP";
    existingStamp.position = {80, 650};
    existingStamp.fontSize = 16;
    existingStamp.textColor = PdfColor::Blue();
    existingStamp.opacity = 0.75;
    existingStamp.drawBackground = true;
    existingStamp.backgroundColor = PdfColor::Gray(0.95);
    existingStamp.drawBorder = true;
    existingStamp.borderColor = PdfColor::Blue();
    const auto stampResult = PdfPageEditor::AddTextStamp(path, stampedPath, 0, existingStamp);
    assert(stampResult.modifiedPageCount == 1U);

    const auto watermarkedPath = std::filesystem::temp_directory_path() / "pdfpp_existing_watermark_test.pdf";
    PdfWatermarkOptions existingWatermark;
    existingWatermark.text = "DRAFT";
    existingWatermark.fontSize = 36;
    existingWatermark.opacity = 0.2;
    existingWatermark.layer = PdfStampLayer::Background;
    const auto watermarkResult = PdfPageEditor::AddWatermarkToAllPages(stampedPath, watermarkedPath, existingWatermark);
    assert(watermarkResult.modifiedPageCount == 2U);

    auto editedDocument = PdfDocument::Open(watermarkedPath);
    assert(editedDocument.GetPageText(0).find("EXISTING STAMP") != std::string::npos);
    assert(editedDocument.GetPageText(0).find("DRAFT") != std::string::npos);
    assert(editedDocument.GetPageText(1).find("DRAFT") != std::string::npos);

    const auto imageStampedPath = std::filesystem::temp_directory_path() / "pdfpp_existing_image_stamp_test.pdf";
    PdfImageStampOptions existingImageStamp;
    existingImageStamp.rectangle = {300, 300, 360, 360};
    existingImageStamp.opacity = 0.55;
    existingImageStamp.drawBorder = true;
    existingImageStamp.borderColor = PdfColor::Red();
    existingImageStamp.borderWidth = 2.0;
    const auto imageStampResult = PdfPageEditor::AddImageStamp(
        watermarkedPath, imageStampedPath, 0U, image, existingImageStamp);
    assert(imageStampResult.modifiedPageCount == 1U);

    auto imageStampedDocument = PdfDocument::Open(imageStampedPath);
    const auto editedImages = imageStampedDocument.ExtractImages(0U);
    assert(editedImages.size() >= 4U);
    const auto& lastImage = editedImages.back();
    assert(lastImage.info.width == 2U);
    assert(lastImage.info.height == 2U);
    assert(lastImage.info.boundingBox.left == 300.0);
    assert(lastImage.info.boundingBox.bottom == 300.0);
    assert(lastImage.info.boundingBox.right == 360.0);
    assert(lastImage.info.boundingBox.top == 360.0);

    const auto reorderedPath = std::filesystem::temp_directory_path() / "pdfpp_reordered_pages_test.pdf";
    const auto reorderedResult = PdfPageOrganizer::ReorderPages(path, reorderedPath, {1U, 0U});
    assert(reorderedResult.originalPageCount == 2U);
    assert(reorderedResult.outputPageCount == 2U);
    auto reorderedDocument = PdfDocument::Open(reorderedPath);
    assert(reorderedDocument.GetPageCount() == 2U);
    assert(reorderedDocument.GetPageText(0U).find("Parallel page two") != std::string::npos);
    assert(reorderedDocument.GetPageText(1U).find("Hello Phase 10") != std::string::npos);

    const auto extractedPath = std::filesystem::temp_directory_path() / "pdfpp_extracted_page_test.pdf";
    const auto extractedResult = PdfPageOrganizer::ExtractPages(path, extractedPath, {1U});
    assert(extractedResult.outputPageCount == 1U);
    auto extractedDocument = PdfDocument::Open(extractedPath);
    assert(extractedDocument.GetPageCount() == 1U);
    assert(extractedDocument.GetPageText(0U).find("Parallel page two") != std::string::npos);

    const auto removedPath = std::filesystem::temp_directory_path() / "pdfpp_removed_page_test.pdf";
    const auto removedResult = PdfPageOrganizer::RemovePages(path, removedPath, {0U});
    assert(removedResult.outputPageCount == 1U);
    auto removedDocument = PdfDocument::Open(removedPath);
    assert(removedDocument.GetPageText(0U).find("Parallel page two") != std::string::npos);

    const auto splitDirectory = std::filesystem::temp_directory_path() / "pdfpp_split_pages_test";
    const auto splitResults = PdfPageOrganizer::SplitEvery(path, splitDirectory, 1U, "page");
    assert(splitResults.size() == 2U);
    assert(PdfDocument::Open(splitResults[0].outputPath).GetPageCount() == 1U);
    assert(PdfDocument::Open(splitResults[1].outputPath).GetPageCount() == 1U);

    const auto nestedPath = std::filesystem::temp_directory_path() / "pdfpp_nested_page_tree_test.pdf";
    const auto nestedReorderedPath = std::filesystem::temp_directory_path() / "pdfpp_nested_page_tree_reordered.pdf";
    const auto nestedExtractedPath = std::filesystem::temp_directory_path() / "pdfpp_nested_page_tree_extracted.pdf";
    WriteNestedPageTreePdf(nestedPath);

    auto nestedDocument = PdfDocument::Open(nestedPath);
    assert(nestedDocument.GetPageCount() == 2U);
    assert(nestedDocument.GetPageText(0U).find("Nested first") != std::string::npos);
    assert(nestedDocument.GetPageText(1U).find("Nested second") != std::string::npos);

    const auto nestedReorderResult = PdfPageOrganizer::ReorderPages(nestedPath, nestedReorderedPath, {1U, 0U});
    assert(nestedReorderResult.outputPageCount == 2U);
    auto nestedReordered = PdfDocument::Open(nestedReorderedPath);
    assert(nestedReordered.GetPageCount() == 2U);
    assert(nestedReordered.GetPageText(0U).find("Nested second") != std::string::npos);
    assert(nestedReordered.GetPage(0U).GetMediaBox().right == 500.0);
    assert(nestedReordered.GetPageText(1U).find("Nested first") != std::string::npos);
    assert(nestedReordered.GetPage(1U).GetMediaBox().right == 300.0);

    const auto nestedExtractResult = PdfPageOrganizer::ExtractPages(nestedPath, nestedExtractedPath, {1U});
    assert(nestedExtractResult.outputPageCount == 1U);
    auto nestedExtracted = PdfDocument::Open(nestedExtractedPath);
    assert(nestedExtracted.GetPageCount() == 1U);
    assert(nestedExtracted.GetPageText(0U).find("Nested second") != std::string::npos);
    assert(nestedExtracted.GetPage(0U).GetMediaBox().right == 500.0);

    const auto mergedPath = std::filesystem::temp_directory_path() / "pdfpp_merged_documents_test.pdf";
    const auto mergeResult = PdfPageImporter::MergeDocuments({path, nestedPath}, mergedPath);
    assert(mergeResult.sourceDocumentCount == 2U);
    assert(mergeResult.importedPageCount == 4U);
    assert(mergeResult.importedObjectCount > 4U);
    auto mergedDocument = PdfDocument::Open(mergedPath);
    assert(mergedDocument.GetPageCount() == 4U);
    assert(mergedDocument.GetPageText(0U).find("Hello Phase 10") != std::string::npos);
    assert(mergedDocument.GetPageText(1U).find("Parallel page two") != std::string::npos);
    assert(mergedDocument.GetPageText(2U).find("Nested first") != std::string::npos);
    assert(mergedDocument.GetPageText(3U).find("Nested second") != std::string::npos);
    assert(!mergedDocument.ExtractImages(0U).empty());

    const auto catalogSourcePath = std::filesystem::temp_directory_path() / "pdfpp_catalog_structures_source.pdf";
    const auto catalogMergedPath = std::filesystem::temp_directory_path() / "pdfpp_catalog_structures_merged.pdf";
    WriteCatalogStructuresPdf(catalogSourcePath);
    const auto catalogMergeResult = PdfPageImporter::MergeDocuments(
        {catalogSourcePath, nestedPath}, catalogMergedPath);
    assert(catalogMergeResult.preservedDocumentInfo);
    assert(catalogMergeResult.preservedCatalogEntryCount >= 4U);

    auto catalogMerged = PdfDocument::Open(catalogMergedPath);
    assert(catalogMerged.GetPageCount() == 3U);
    assert(catalogMerged.GetDocumentInfo().title == "Catalog Source");
    const PdfDictionary* mergedCatalog =
        catalogMerged.GetObject(catalogMerged.GetCatalogReference()).AsDictionary();
    assert(mergedCatalog != nullptr);
    assert(mergedCatalog->Contains(PdfName("Outlines")));
    assert(mergedCatalog->Contains(PdfName("Metadata")));
    assert(mergedCatalog->Get(PdfName("PageMode")).AsName()->value() == "UseOutlines");

    const auto outlinesPair = *mergedCatalog->Get(PdfName("Outlines")).AsReference();
    const PdfDictionary* outlines = catalogMerged.GetObject({outlinesPair.first, outlinesPair.second}).AsDictionary();
    assert(outlines != nullptr);
    const auto firstOutlinePair = *outlines->Get(PdfName("First")).AsReference();
    const PdfDictionary* firstOutline =
        catalogMerged.GetObject({firstOutlinePair.first, firstOutlinePair.second}).AsDictionary();
    assert(firstOutline != nullptr);
    const PdfArray* destination = firstOutline->Get(PdfName("Dest")).AsArray();
    assert(destination != nullptr);
    const auto destinationPage = *destination->at(0U).AsReference();
    const PdfReference firstOutputPage = catalogMerged.GetPageReference(0U);
    assert(destinationPage.first == firstOutputPage.objectNumber);

    const auto formOnePath = std::filesystem::temp_directory_path() / "pdfpp_form_one.pdf";
    const auto formTwoPath = std::filesystem::temp_directory_path() / "pdfpp_form_two.pdf";
    const auto mergedFormsPath = std::filesystem::temp_directory_path() / "pdfpp_merged_forms.pdf";
    WriteAcroFormPdf(formOnePath, "Form source one", "Customer", "Alice");
    WriteAcroFormPdf(formTwoPath, "Form source two", "Customer", "Bob");

    const auto formFields = PdfAcroForm::GetFields(formOnePath);
    assert(formFields.size() == 1U);
    assert(formFields[0].name == "Customer");
    assert(formFields[0].type == PdfFormFieldType::Text);
    assert(formFields[0].value == "Alice");
    assert(formFields[0].pageIndex && *formFields[0].pageIndex == 0U);

    const auto updatedFormPath = std::filesystem::temp_directory_path() / "pdfpp_updated_form.pdf";
    const auto formUpdateResult = PdfAcroForm::SetFieldValues(
        formOnePath,
        updatedFormPath,
        {{"Customer", "Carol"}});
    assert(formUpdateResult.updatedFieldCount == 1U);
    const auto updatedFields = PdfAcroForm::GetFields(updatedFormPath);
    assert(updatedFields.size() == 1U);
    assert(updatedFields[0].value == "Carol");

    const auto appearanceFormPath = std::filesystem::temp_directory_path() / "pdfpp_form_appearance.pdf";
    const auto appearanceResult = PdfAcroForm::GenerateAppearances(
        updatedFormPath, appearanceFormPath, {"Customer"});
    assert(appearanceResult.generatedAppearanceCount == 1U);
    auto appearanceDocument = PdfDocument::Open(appearanceFormPath);
    const auto appearanceFields = PdfAcroForm::GetFields(appearanceDocument);
    assert(appearanceFields.size() == 1U);
    const PdfDictionary* appearanceWidget =
        appearanceDocument.GetObject(appearanceFields[0].widgetReferences[0]).AsDictionary();
    assert(appearanceWidget != nullptr && appearanceWidget->Contains(PdfName("AP")));

    const auto flattenedFormPath = std::filesystem::temp_directory_path() / "pdfpp_form_flattened.pdf";
    const auto flattenResult = PdfAcroForm::FlattenFields(
        appearanceFormPath, flattenedFormPath, {"Customer"});
    assert(flattenResult.flattenedFieldCount == 1U);
    assert(flattenResult.removedWidgetCount == 1U);
    auto flattenedDocument = PdfDocument::Open(flattenedFormPath);
    assert(PdfAcroForm::GetFields(flattenedDocument).empty());
    const PdfDictionary* flattenedPage =
        flattenedDocument.GetObject(flattenedDocument.GetPageReference(0U)).AsDictionary();
    assert(flattenedPage != nullptr && !flattenedPage->Contains(PdfName("Annots")));
    assert(flattenedDocument.GetPageText(0U).find("Carol") != std::string::npos);
    const auto formMergeResult = PdfPageImporter::MergeDocuments(
        {formOnePath, formTwoPath}, mergedFormsPath);
    assert(formMergeResult.preservedAcroForm);
    assert(formMergeResult.importedFormFieldCount == 2U);

    auto mergedForms = PdfDocument::Open(mergedFormsPath);
    const PdfDictionary* formCatalog =
        mergedForms.GetObject(mergedForms.GetCatalogReference()).AsDictionary();
    assert(formCatalog != nullptr);
    const auto acroFormPair = *formCatalog->Get(PdfName("AcroForm")).AsReference();
    const PdfDictionary* acroForm =
        mergedForms.GetObject({acroFormPair.first, acroFormPair.second}).AsDictionary();
    assert(acroForm != nullptr);
    const PdfArray* fields = acroForm->GetAsArray(PdfName("Fields"));
    assert(fields != nullptr && fields->size() == 2U);

    const auto firstFieldPair = *fields->at(0U).AsReference();
    const auto secondFieldPair = *fields->at(1U).AsReference();
    const PdfDictionary* firstField =
        mergedForms.GetObject({firstFieldPair.first, firstFieldPair.second}).AsDictionary();
    const PdfDictionary* secondField =
        mergedForms.GetObject({secondFieldPair.first, secondFieldPair.second}).AsDictionary();
    assert(firstField != nullptr && secondField != nullptr);
    assert(*firstField->Get(PdfName("T")).AsString() == "Customer");
    assert(*secondField->Get(PdfName("T")).AsString() == "Source2.Customer");
    assert(*firstField->Get(PdfName("V")).AsString() == "Alice");
    assert(*secondField->Get(PdfName("V")).AsString() == "Bob");
    const auto secondWidgetPage = *secondField->Get(PdfName("P")).AsReference();
    assert(secondWidgetPage.first == mergedForms.GetPageReference(1U).objectNumber);

    const auto copiedPath = std::filesystem::temp_directory_path() / "pdfpp_copied_pages_test.pdf";
    PdfPageImportSource firstSelection{path, {1U}};
    PdfPageImportSource secondSelection{nestedPath, {0U}};
    const auto copyResult = PdfPageImporter::CopyPages({firstSelection, secondSelection}, copiedPath);
    assert(copyResult.importedPageCount == 2U);
    auto copiedDocument = PdfDocument::Open(copiedPath);
    assert(copiedDocument.GetPageCount() == 2U);
    assert(copiedDocument.GetPageText(0U).find("Parallel page two") != std::string::npos);
    assert(copiedDocument.GetPageText(1U).find("Nested first") != std::string::npos);
    assert(copiedDocument.GetPage(1U).GetMediaBox().right == 300.0);

    std::filesystem::remove(mergedFormsPath);
    std::filesystem::remove(flattenedFormPath);
    std::filesystem::remove(appearanceFormPath);
    std::filesystem::remove(updatedFormPath);
    std::filesystem::remove(formTwoPath);
    std::filesystem::remove(formOnePath);
    std::filesystem::remove(catalogMergedPath);
    std::filesystem::remove(catalogSourcePath);
    std::filesystem::remove(copiedPath);
    std::filesystem::remove(mergedPath);
    std::filesystem::remove(nestedExtractedPath);
    std::filesystem::remove(nestedReorderedPath);
    std::filesystem::remove(nestedPath);
    std::filesystem::remove_all(splitDirectory);
    std::filesystem::remove(removedPath);
    std::filesystem::remove(extractedPath);
    std::filesystem::remove(reorderedPath);
    std::filesystem::remove(imageStampedPath);
    std::filesystem::remove(watermarkedPath);
    std::filesystem::remove(stampedPath);
    std::filesystem::remove(path);
    return 0;
}

#include <CPPPdf/CPPPdf.hpp>

#include <array>
#include <cstddef>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> makeMinimalPdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 5> offsets{};

    auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };

    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>");
    addObject(4, "<< /Length 17 >>\nstream\nBT (Hello) Tj ET\nendstream");

    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 0\n0 5\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();

    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) {
        result[i] = static_cast<std::byte>(pdf[i]);
    }
    return result;
}


std::vector<std::byte> makeFontResourcePdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 6> offsets{};

    auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };

    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                 "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>");
    const std::string content = "BT /F1 10 Tf 1 0 0 1 72 720 Tm (AA) Tj ET";
    addObject(4, "<< /Length " + std::to_string(content.size()) + ">>\nstream\n" +
                 content + "\nendstream");
    addObject(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                 "/FirstChar 65 /Widths [600] "
                 "/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /Omega] >> >>");

    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 6\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();

    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(pdf[i]);
    return result;
}


std::vector<std::byte> makeFormXObjectPdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 7> offsets{};

    auto addObject = [&](const std::size_t number, const std::string_view body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n";
        pdf.append(body);
        pdf += "\nendobj\n";
    };

    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                 "/Resources << /XObject << /Fm1 5 0 R >> >> /Contents 4 0 R >>");
    const std::string pageContent = "/Fm1 Do";
    addObject(4, "<< /Length " + std::to_string(pageContent.size()) + ">>\nstream\n" +
                 pageContent + "\nendstream");
    const std::string formContent = "BT /F2 10 Tf 1 0 0 1 5 7 Tm (AA) Tj ET";
    addObject(5, "<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] "
                 "/Matrix [2 0 0 2 100 200] "
                 "/Resources << /Font << /F2 6 0 R >> >> "
                 "/Length " + std::to_string(formContent.size()) + ">>\nstream\n" +
                 formContent + "\nendstream");
    addObject(6, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                 "/FirstChar 65 /Widths [600] "
                 "/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /Omega] >> >>");

    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 7\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) {
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    xref << "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n"
         << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();

    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(pdf[i]);
    return result;
}


std::vector<std::byte> makeInlineImagePdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 5> offsets{};
    auto addObject = [&](const std::size_t number, const std::string& body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
    };
    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 4 0 R >>");
    std::string content = "q 20 0 0 30 10 15 cm BI /W 2 /H 1 /BPC 8 /CS /RGB ID ";
    const std::array<unsigned char, 6> pixels{1, 2, 3, 4, 5, 6};
    for (const auto value : pixels) content.push_back(static_cast<char>(value));
    content += " EI Q";
    addObject(4, "<< /Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref;
    xref << "xref\n0 5\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i)
        xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    xref << "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();
    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(pdf[i]);
    return result;
}

std::vector<std::byte> makeSoftMaskImagePdf() {
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 7> offsets{};
    auto addObject = [&](const std::size_t number, const std::string& body) {
        offsets[number] = pdf.size();
        pdf += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
    };
    addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << /XObject << /Im1 5 0 R >> >> /Contents 4 0 R >>");
    const std::string content = "10 0 0 10 5 6 cm /Im1 Do";
    addObject(4, "<< /Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
    std::string imageBody = "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 6 0 R /Length 3 >>\nstream\n";
    imageBody.push_back(char(10)); imageBody.push_back(char(20)); imageBody.push_back(char(30)); imageBody += "\nendstream";
    addObject(5, imageBody);
    std::string maskBody = "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8 /Length 1 >>\nstream\n";
    maskBody.push_back(char(128)); maskBody += "\nendstream";
    addObject(6, maskBody);
    const std::size_t xrefOffset = pdf.size();
    std::ostringstream xref; xref << "xref\n0 7\n0000000000 65535 f \n";
    for (std::size_t i = 1; i < offsets.size(); ++i) xref << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    xref << "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    pdf += xref.str();
    std::vector<std::byte> result(pdf.size());
    for (std::size_t i = 0; i < pdf.size(); ++i) result[i] = static_cast<std::byte>(static_cast<unsigned char>(pdf[i]));
    return result;
}

} // namespace

int RunReaderIntegrationTests() {
    const auto bytes = makeMinimalPdf();
    auto document = CPPPdf::PdfDocument::Open(std::span<const std::byte>(bytes));

    if (document.GetVersion() != "1.4" || document.GetPageCount() != 1U) {
        std::cerr << "Minimal PDF metadata was parsed incorrectly.\n";
        return 1;
    }

    const auto page = document.GetPage(0U);
    if (page.GetMediaBox().width() != 612.0 || page.GetMediaBox().height() != 792.0) {
        std::cerr << "Page geometry was parsed incorrectly.\n";
        return 2;
    }

    if (document.GetPageText(0U).find("Hello") == std::string::npos) {
        std::cerr << "Page text extraction failed.\n";
        return 3;
    }

    document.ClearObjectCache();
    (void)document.GetObject(CPPPdf::PdfReference{1U, 0U});
    (void)document.GetObject(CPPPdf::PdfReference{1U, 1U});
    if (document.GetCachedObjectCount() != 2U) {
        std::cerr << "Object cache does not distinguish generations.\n";
        return 4;
    }

    document.ClearObjectCache();
    if (document.GetCachedObjectCount() != 0U) {
        std::cerr << "Object cache did not clear.\n";
        return 5;
    }

    CPPPdf::PdfReaderOptions boundedOptions;
    boundedOptions.limits.maxCachedObjects = 1U;
    auto boundedDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(bytes), boundedOptions);
    (void)boundedDocument.GetObject(CPPPdf::PdfReference{1U, 0U});
    (void)boundedDocument.GetObject(CPPPdf::PdfReference{2U, 0U});
    if (boundedDocument.GetObjectCacheCapacity() != 1U ||
        boundedDocument.GetCachedObjectCount() > 1U) {
        std::cerr << "Bounded LRU object cache exceeded its configured capacity.\n";
        return 13;
    }

    CPPPdf::PdfReaderOptions pageLimitedOptions;
    pageLimitedOptions.limits.maxPageCount = 0U;
    auto pageLimitedDocument = CPPPdf::PdfDocument::Open(
        std::span<const std::byte>(bytes), pageLimitedOptions);
    try {
        (void)pageLimitedDocument.GetPageCount();
        std::cerr << "Expected page-count limit to reject the document.\n";
        return 19;
    } catch (const CPPPdf::PdfException& error) {
        if (error.code() != CPPPdf::PdfErrorCode::InvalidPageTree) {
            std::cerr << "Page-count limit returned the wrong error code.\n";
            return 20;
        }
    }

    const auto fontBytes = makeFontResourcePdf();
    auto fontDocument = CPPPdf::PdfDocument::Open(std::span<const std::byte>(fontBytes));
    const auto fontChunks = fontDocument.ExtractTextChunks(0U);
    if (fontChunks.size() != 1U || fontChunks.front().utf8Text != "ΩΩ") {
        std::cerr << "Page font resources were not applied during Unicode extraction.\n";
        return 6;
    }
    const double measuredWidth = fontChunks.front().end.x - fontChunks.front().start.x;
    if (!fontChunks.front().usedEmbeddedFontMetrics ||
        fontChunks.front().glyphCount != 2U ||
        measuredWidth < 11.99 || measuredWidth > 12.01) {
        std::cerr << "Embedded font widths were not applied to text positioning.\n";
        return 7;
    }

    const auto formBytes = makeFormXObjectPdf();
    auto formDocument = CPPPdf::PdfDocument::Open(std::span<const std::byte>(formBytes));
    const auto formChunks = formDocument.ExtractTextChunks(0U);
    if (formChunks.size() != 1U || formChunks.front().utf8Text != "ΩΩ") {
        std::cerr << "Form XObject text or scoped font resources were not extracted.\n";
        return 8;
    }
    if (formChunks.front().sourceObjectNumber != 5U ||
        std::abs(formChunks.front().start.x - 110.0) > 0.01 ||
        std::abs(formChunks.front().start.y - 214.0) > 0.01) {
        std::cerr << "Form XObject matrix was not applied to text geometry.\n";
        return 9;
    }

    const auto inlineBytes = makeInlineImagePdf();
    auto inlineDocument = CPPPdf::PdfDocument::Open(std::span<const std::byte>(inlineBytes));
    const auto inlineImages = inlineDocument.ExtractImages(0U);
    if (inlineImages.size() != 1U || !inlineImages.front().info.inlineImage ||
        inlineImages.front().info.width != 2U || inlineImages.front().info.height != 1U ||
        inlineImages.front().decodedBytes.size() != 6U) {
        std::cerr << "Inline image extraction failed.\n";
        return 10;
    }
    if (std::abs(inlineImages.front().info.boundingBox.left - 10.0) > 0.01 ||
        std::abs(inlineImages.front().info.boundingBox.bottom - 15.0) > 0.01 ||
        std::abs(inlineImages.front().info.boundingBox.right - 30.0) > 0.01 ||
        std::abs(inlineImages.front().info.boundingBox.top - 45.0) > 0.01) {
        std::cerr << "Inline image CTM was not applied.\n";
        return 11;
    }

    const auto softMaskBytes = makeSoftMaskImagePdf();
    auto softMaskDocument = CPPPdf::PdfDocument::Open(std::span<const std::byte>(softMaskBytes));
    const auto maskedImages = softMaskDocument.ExtractImages(0U);
    if (maskedImages.size() != 1U || !maskedImages.front().info.hasSoftMask ||
        maskedImages.front().info.softMaskReference.objectNumber != 6U ||
        maskedImages.front().alphaBytes.size() != 1U ||
        std::to_integer<unsigned int>(maskedImages.front().alphaBytes.front()) != 128U) {
        std::cerr << "Soft-mask image extraction failed.\n";
        return 12;
    }


    const auto annotationInput = std::filesystem::temp_directory_path() / "pdfpp_annotation_input.pdf";
    const auto annotationOutput = std::filesystem::temp_directory_path() / "pdfpp_annotation_output.pdf";
    {
        std::ofstream file(annotationInput, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::vector<CPPPdf::PdfAnnotation> annotations;
    annotations.push_back(CPPPdf::PdfAnnotation{
        0U, CPPPdf::PdfAnnotationType::Underline, {10, 10, 80, 25}, {{10, 10, 80, 25}},
        {0.0, 0.0, 1.0}, 0.8, "Underline test", "Pdf++", {}, false});
    annotations.push_back(CPPPdf::PdfAnnotation{
        0U, CPPPdf::PdfAnnotationType::TextNote, {100, 100, 120, 120}, {},
        {1.0, 1.0, 0.0}, 1.0, "Review this", "Pdf++", {}, true});
    annotations.push_back(CPPPdf::PdfAnnotation{
        0U, CPPPdf::PdfAnnotationType::Link, {30, 30, 130, 50}, {},
        {0.0, 0.0, 1.0}, 1.0, {}, {}, "https://example.com", false});
    const auto editResult = CPPPdf::PdfAnnotationEditor::AddAnnotations(
        annotationInput, annotationOutput, annotations);
    if (editResult.annotationCount != 3U || editResult.modifiedPageCount != 1U) {
        std::cerr << "Generic annotation editor returned incorrect counts.\n";
        return 14;
    }
    auto annotatedDocument = CPPPdf::PdfDocument::Open(annotationOutput);
    const auto annotatedPage = annotatedDocument.GetObject(annotatedDocument.GetPageReference(0U));
    const auto* annotatedDictionary = annotatedPage.AsDictionary();
    const auto* annotationArray = annotatedDictionary ? annotatedDictionary->GetAsArray(CPPPdf::PdfName("Annots")) : nullptr;
    if (!annotationArray || annotationArray->size() != 3U) {
        std::cerr << "Generic annotations were not persisted through incremental update.\n";
        return 15;
    }
    std::error_code cleanupError;
    std::filesystem::remove(annotationInput, cleanupError);
    std::filesystem::remove(annotationOutput, cleanupError);


    const auto pageEditInput = std::filesystem::temp_directory_path() / "pdfpp_page_edit_input.pdf";
    const auto pageEditOutput = std::filesystem::temp_directory_path() / "pdfpp_page_edit_output.pdf";
    {
        std::ofstream file(pageEditInput, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    CPPPdf::PdfPageEdit pageEdit;
    pageEdit.pageIndex = 0U;
    pageEdit.foregroundContent = CPPPdf::PdfContentCommands::StrokeRectangle(
        {20.0, 30.0, 120.0, 80.0}, 2.0, 1.0, 0.0, 0.0);
    pageEdit.backgroundContent = CPPPdf::PdfContentCommands::FillRectangle(
        {5.0, 5.0, 15.0, 15.0}, 0.9, 0.9, 0.9);
    pageEdit.rotation = 90;
    pageEdit.cropBox = CPPPdf::PdfRectangle{10.0, 20.0, 500.0, 700.0};
    const auto pageEditResult = CPPPdf::PdfPageEditor::ApplyEdits(
        pageEditInput, pageEditOutput, {pageEdit});
    if (pageEditResult.modifiedPageCount != 1U ||
        pageEditResult.appendedContentStreamCount != 2U) {
        std::cerr << "Incremental page editor returned incorrect counts.\n";
        return 16;
    }
    auto editedDocument = CPPPdf::PdfDocument::Open(pageEditOutput);
    const auto editedInfo = editedDocument.GetPageInfo(0U);
    if (editedInfo.rotation != 90 ||
        std::abs(editedInfo.cropBox.left - 10.0) > 0.01 ||
        std::abs(editedInfo.cropBox.top - 700.0) > 0.01 ||
        editedDocument.GetPageText(0U).find("Hello") == std::string::npos) {
        std::cerr << "Page geometry/content incremental edit did not persist correctly.\n";
        return 17;
    }
    const auto& editedPageObject = editedDocument.GetObject(editedDocument.GetPageReference(0U));
    const auto* editedPageDictionary = editedPageObject.AsDictionary();
    const auto* editedContents = editedPageDictionary
        ? editedPageDictionary->GetAsArray(CPPPdf::PdfName::Contents)
        : nullptr;
    if (!editedContents || editedContents->size() != 3U) {
        std::cerr << "Background/original/foreground content ordering was not stored as an array.\n";
        return 18;
    }
    std::filesystem::remove(pageEditInput, cleanupError);
    std::filesystem::remove(pageEditOutput, cleanupError);

    constexpr std::string_view invalid = "%PDF-1.7\nnot-a-complete-pdf";
    std::array<std::byte, invalid.size()> invalidBytes{};
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        invalidBytes[i] = static_cast<std::byte>(invalid[i]);
    }

    try {
        (void)CPPPdf::PdfDocument::Open(std::span<const std::byte>(invalidBytes));
        std::cerr << "Expected malformed input to fail.\n";
        return 6;
    } catch (const CPPPdf::PdfException&) {
        return 0;
    }
}

#include <CPPPdf/CPPPdf.h>
#include "Internal/Writer/PdfIncrementalWriter.hpp"
#include <cassert>
#include <filesystem>
#include <array>
#include <cstddef>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include "TestRunner.hpp"

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

void TestUnicodeTrueTypeWriter() {
    using namespace CPPPdf;
    using namespace CPPPdf;
    const auto path = std::filesystem::temp_directory_path() / "pdfpp_writer_phase10_test.pdf";

    const std::array<std::filesystem::path, 4> unicodeFontCandidates{
        std::filesystem::path("C:/Windows/Fonts/arial.ttf"),
        std::filesystem::path("C:/Windows/Fonts/segoeui.ttf"),
        std::filesystem::path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        std::filesystem::path("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf")};
    const auto fontIt = std::find_if(unicodeFontCandidates.begin(), unicodeFontCandidates.end(),
        [](const auto& candidate) { return std::filesystem::exists(candidate); });
    if (fontIt != unicodeFontCandidates.end()) {
        const auto unicodePath = std::filesystem::temp_directory_path() / "pdfpp_unicode_writer_test.pdf";
        const auto trueTypeFont = PdfTrueTypeFont::Load(*fontIt);
        PDFPP_TEST_CHECK(trueTypeFont.GetMetrics().unitsPerEm > 0);
        PDFPP_TEST_CHECK(trueTypeFont.GetMetrics().glyphCount > 100);
        PDFPP_TEST_CHECK(trueTypeFont.MeasureTextUtf8("WWW", 12) > trueTypeFont.MeasureTextUtf8("iii", 12));
        PDFPP_TEST_CHECK(trueTypeFont.GetLineHeight(12) > 0);
        const std::array<std::uint16_t, 4> subsetGlyphs{0,
            *trueTypeFont.GetGlyphId(U'A'), *trueTypeFont.GetGlyphId(U'ế'), *trueTypeFont.GetGlyphId(U'Ω')};
        const auto fontSubset = trueTypeFont.BuildSubset(subsetGlyphs);
        PDFPP_TEST_CHECK(fontSubset.subsetApplied);
        PDFPP_TEST_CHECK(fontSubset.GetByteSize() < trueTypeFont.GetBytes().size());
        PDFPP_TEST_CHECK(fontSubset.GetReductionRatio() > 0.0);
        PdfWriter unicodeWriter;
        const auto unicodePage = unicodeWriter.AddPage({0, 0, 595, 842});
        unicodeWriter.GetCanvas(unicodePage)
            .BeginText().SetTrueTypeFontAndSize(trueTypeFont, 16).MoveText(72, 760)
            .ShowTextUtf8("Tiếng Việt: Trường đại học – Ω").EndText();
        PdfTextLayoutOptions layout;
        layout.box = {72, 600, 420, 710};
        layout.fontSize = 13;
        layout.alignment = PdfTextAlignment::Center;
        unicodeWriter.GetCanvas(unicodePage).DrawTextUtf8(
            trueTypeFont, "Đo chiều rộng chính xác và tự động xuống dòng Unicode.", layout);
        unicodeWriter.Save(unicodePath);
        const auto fullFontPath = std::filesystem::temp_directory_path() / "pdfpp_unicode_writer_full_font_test.pdf";
        PdfSaveOptions fullFontOptions; fullFontOptions.subsetTrueTypeFonts = false;
        unicodeWriter.Save(fullFontPath, fullFontOptions);
        PDFPP_TEST_CHECK(std::filesystem::file_size(unicodePath) < std::filesystem::file_size(fullFontPath));
        auto unicodeDocument = PdfDocument::Open(unicodePath);
        const auto extracted = unicodeDocument.GetPageText(0);
        PDFPP_TEST_CHECK(extracted.find("Tiếng Việt") != std::string::npos);
        PDFPP_TEST_CHECK(extracted.find("Trường đại học") != std::string::npos);
        PDFPP_TEST_CHECK(extracted.find("Ω") != std::string::npos);
        PDFPP_TEST_CHECK(extracted.find("tự động xuống dòng") != std::string::npos);
        std::filesystem::remove(unicodePath);
        std::filesystem::remove(fullFontPath);
    }
}

void TestXrefStreamAndClassicOutput() {
    using namespace CPPPdf;
    {
        PdfWriter streamWriter;
        const auto streamPage = streamWriter.AddPage({0, 0, 400, 500});
        streamWriter.GetCanvas(streamPage).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(36, 460).ShowText("XRef stream output").EndText();
        std::ostringstream xrefStreamOutput;
        streamWriter.Save(xrefStreamOutput);
        const std::string xrefStreamBytes = xrefStreamOutput.str();
        PDFPP_TEST_CHECK(xrefStreamBytes.find("/Type /XRef") != std::string::npos);
        PDFPP_TEST_CHECK(xrefStreamBytes.find("xref\n0 ") == std::string::npos);
        PDFPP_TEST_CHECK(xrefStreamBytes.find("trailer\n<< /Size") == std::string::npos);
        const auto xrefStreamPath = std::filesystem::temp_directory_path() / "pdfpp_xref_stream_test.pdf";
        {
            std::ofstream file(xrefStreamPath, std::ios::binary);
            file.write(xrefStreamBytes.data(), static_cast<std::streamsize>(xrefStreamBytes.size()));
        }
        auto xrefStreamDocument = PdfDocument::Open(xrefStreamPath);
        PDFPP_TEST_CHECK(xrefStreamDocument.GetPageText(0U).find("XRef stream output") != std::string::npos);

        PdfSaveOptions classicOptions;
        classicOptions.writeXrefStream = false;
        std::ostringstream classicOutput;
        streamWriter.Save(classicOutput, classicOptions);
        const std::string classicBytes = classicOutput.str();
        PDFPP_TEST_CHECK(classicBytes.find("xref\n0 ") != std::string::npos);
        PDFPP_TEST_CHECK(classicBytes.find("/Type /XRef") == std::string::npos);
        const auto classicPath = std::filesystem::temp_directory_path() / "pdfpp_classic_xref_test.pdf";
        {
            std::ofstream file(classicPath, std::ios::binary);
            file.write(classicBytes.data(), static_cast<std::streamsize>(classicBytes.size()));
        }
        auto classicDocument = PdfDocument::Open(classicPath);
        PDFPP_TEST_CHECK(classicDocument.GetPageText(0U).find("XRef stream output") != std::string::npos);
        std::filesystem::remove(xrefStreamPath);
        std::filesystem::remove(classicPath);
    }
}

void TestObjectStreamRoundTrip() {
    using namespace CPPPdf;
    {
        // Object streams must round-trip every feature through the reader.
        PdfWriter objectStreamWriter;
        const auto labelPage = objectStreamWriter.AddPage({0, 0, 400, 500});
        objectStreamWriter.GetCanvas(labelPage).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(36, 460).ShowText("Object stream round trip").EndText();
        const auto secondLabelPage = objectStreamWriter.AddPage({0, 0, 400, 500});
        objectStreamWriter.GetCanvas(secondLabelPage).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(36, 460).ShowText("Second label page").EndText();
        objectStreamWriter.AddPageLabel(0U, PdfPageLabelOptions{});
        objectStreamWriter.AddPageLabel(1U, {PdfPageLabelStyle::LowerRoman, "P-", 5U});
        const auto bookmarkIndex = objectStreamWriter.AddBookmark(PdfBookmarkOptions{"ObjStm bookmark", 0U});
        (void)bookmarkIndex;
        objectStreamWriter.AddNamedDestination("objstm-dest", PdfDestinationOptions{});
        objectStreamWriter.AddUriLink(0U, "https://example.com/objstm", PdfLinkOptions{});

        PdfSaveOptions objectStreamOptions;
        objectStreamOptions.writeObjectStreams = true;
        const auto objectStreamPath = std::filesystem::temp_directory_path() / "pdfpp_objstm_test.pdf";
        objectStreamWriter.Save(objectStreamPath, objectStreamOptions);
        auto objectStreamDocument = PdfDocument::Open(objectStreamPath);
        PDFPP_TEST_CHECK(objectStreamDocument.GetPageCount() == 2U);
        PDFPP_TEST_CHECK(objectStreamDocument.GetPageText(0U).find("Object stream round trip") != std::string::npos);
        PDFPP_TEST_CHECK(objectStreamDocument.GetPageText(1U).find("Second label page") != std::string::npos);
        std::size_t compressedCount = 0U;
        bool compressedObjectReadable = false;
        for (std::uint32_t number = 1U; number < 80U; ++number) {
            const auto entry = objectStreamDocument.GetXrefEntry(number);
            if (!entry || entry->type != PdfXrefEntry::Type::Compressed) continue;
            ++compressedCount;
            const auto& object = objectStreamDocument.GetObject(PdfReference{number, 0U});
            if (const PdfDictionary* dictionary = object.AsDictionary()) {
                if (const PdfDictionary* action = dictionary->GetAsDictionary(PdfName("A"))) {
                    const auto subtype = action->GetAsName(PdfName("S"));
                    if (subtype && subtype->value() == "URI") {
                        const PdfObject* uri = action->Find(PdfName("URI"));
                        compressedObjectReadable = uri && uri->AsString() &&
                            *uri->AsString() == "https://example.com/objstm";
                    }
                }
            }
        }
        PDFPP_TEST_CHECK(compressedCount > 0U);
        PDFPP_TEST_CHECK(compressedObjectReadable);
        std::filesystem::remove(objectStreamPath);
    }
}

void TestIncrementalObjectStream() {
    using namespace CPPPdf;
    {
        // An incremental revision can pack its small objects into an object
        // stream and reference them from a new xref stream that chains through
        // /Prev to the original file.
        const auto incrementalInput =
            std::filesystem::temp_directory_path() / "pdfpp_incremental_objstm_input.pdf";
        const auto incrementalOutput =
            std::filesystem::temp_directory_path() / "pdfpp_incremental_objstm_output.pdf";
        PdfWriter baseWriter;
        const auto basePage = baseWriter.AddPage({0, 0, 300, 400});
        baseWriter.GetCanvas(basePage).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(24, 360).ShowText("Base document").EndText();
        baseWriter.Save(incrementalInput);

        auto baseDocument = CPPPdf::PdfDocument::Open(incrementalInput);
        const auto first = CPPPdf::Internal::PdfIncrementalWriter::NextObjectNumber(baseDocument);
        {
            CPPPdf::Internal::PdfIncrementalWriterOptions options;
            options.writeObjectStreams = true;
            CPPPdf::Internal::PdfIncrementalWriter incremental(incrementalInput, incrementalOutput,
                                                               baseDocument, options);
            CPPPdf::PdfDictionary alpha;
            alpha.Put(CPPPdf::PdfName("Kind"), CPPPdf::PdfObject(CPPPdf::PdfName("Alpha")));
            CPPPdf::PdfDictionary beta;
            beta.Put(CPPPdf::PdfName("Kind"), CPPPdf::PdfObject(CPPPdf::PdfName("Beta")));
            CPPPdf::PdfDictionary gamma;
            gamma.Put(CPPPdf::PdfName("Kind"), CPPPdf::PdfObject(CPPPdf::PdfName("Gamma")));
            incremental.WriteDictionary(CPPPdf::PdfReference{first, 0U}, alpha);
            incremental.WriteDictionary(CPPPdf::PdfReference{first + 1U, 0U}, beta);
            incremental.WriteDictionary(CPPPdf::PdfReference{first + 2U, 0U}, gamma);
            incremental.WriteRawObject(CPPPdf::PdfReference{first + 3U, 0U},
                "<< /Length 5 >>\nstream\nhello\nendstream");
            incremental.Finish(first + 4U);
        }

        auto updatedDocument = CPPPdf::PdfDocument::Open(incrementalOutput);
        PDFPP_TEST_CHECK(updatedDocument.GetPageText(0U).find("Base document") != std::string::npos);
        bool compressedSeen = false;
        for (std::uint32_t number = 1U; number < 80U; ++number) {
            const auto entry = updatedDocument.GetXrefEntry(number);
            if (!entry || entry->type != CPPPdf::PdfXrefEntry::Type::Compressed) continue;
            compressedSeen = true;
            const auto& object = updatedDocument.GetObject(CPPPdf::PdfReference{number, 0U});
            const auto* dictionary = object.AsDictionary();
            const auto kind = dictionary ? dictionary->GetAsName(CPPPdf::PdfName("Kind")) : std::nullopt;
            if (number == first) PDFPP_TEST_CHECK(kind && kind->value() == "Alpha");
            if (number == first + 1U) PDFPP_TEST_CHECK(kind && kind->value() == "Beta");
            if (number == first + 2U) PDFPP_TEST_CHECK(kind && kind->value() == "Gamma");
        }
        PDFPP_TEST_CHECK(compressedSeen);
        const auto& streamObject = updatedDocument.GetObject(CPPPdf::PdfReference{first + 3U, 0U});
        PDFPP_TEST_CHECK(streamObject.AsStream() != nullptr);
        PDFPP_TEST_CHECK(updatedDocument.GetTrailerDictionary().find("/Type /XRef") != std::string::npos);
        std::filesystem::remove(incrementalInput);
        std::filesystem::remove(incrementalOutput);
    }
}

void TestIncrementalEncryptedObjectStream() {
    using namespace CPPPdf;
    {
        // An encrypted document accepts an object-stream incremental revision;
        // the object stream itself is encrypted with the stream object number.
        const auto encryptedInput =
            std::filesystem::temp_directory_path() / "pdfpp_incremental_encrypted_input.pdf";
        const auto encryptedOutput =
            std::filesystem::temp_directory_path() / "pdfpp_incremental_encrypted_output.pdf";
        PdfWriter encryptedWriter;
        const auto encryptedPage = encryptedWriter.AddPage({0, 0, 300, 400});
        encryptedWriter.GetCanvas(encryptedPage).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(24, 360).ShowText("Encrypted base").EndText();
        PdfEncryptionOptions encryptionOptions;
        encryptionOptions.userPassword = "objstm-user";
        encryptionOptions.ownerPassword = "objstm-owner";
        encryptionOptions.algorithm = PdfEncryptionAlgorithm::Aes128;
        encryptedWriter.SetEncryption(encryptionOptions);
        encryptedWriter.Save(encryptedInput);

        PdfReaderOptions readerOptions;
        readerOptions.password = "objstm-user";
        auto encryptedDocument = CPPPdf::PdfDocument::Open(encryptedInput, readerOptions);
        const auto first = CPPPdf::Internal::PdfIncrementalWriter::NextObjectNumber(encryptedDocument);
        {
            CPPPdf::Internal::PdfIncrementalWriterOptions options;
            options.writeObjectStreams = true;
            CPPPdf::Internal::PdfIncrementalWriter incremental(encryptedInput, encryptedOutput,
                                                               encryptedDocument, options);
            CPPPdf::PdfDictionary alpha;
            alpha.Put(CPPPdf::PdfName("Secret"), CPPPdf::PdfObject(CPPPdf::PdfName("HiddenValue")));
            incremental.WriteDictionary(CPPPdf::PdfReference{first, 0U}, alpha);
            incremental.Finish(first + 1U);
        }

        PdfReaderOptions reopenOptions;
        reopenOptions.password = "objstm-user";
        auto reopened = CPPPdf::PdfDocument::Open(encryptedOutput, reopenOptions);
        PDFPP_TEST_CHECK(reopened.GetPageText(0U).find("Encrypted base") != std::string::npos);
        const auto& secret = reopened.GetObject(CPPPdf::PdfReference{first, 0U});
        const auto* secretDictionary = secret.AsDictionary();
        const auto secretValue = secretDictionary
            ? secretDictionary->GetAsName(CPPPdf::PdfName("Secret")) : std::nullopt;
        PDFPP_TEST_CHECK(secretValue && secretValue->value() == "HiddenValue");
        std::filesystem::remove(encryptedInput);
        std::filesystem::remove(encryptedOutput);
    }
}

void TestResaveCollapsesIncremental() {
    using namespace CPPPdf;
    {
        // Resave rewrites a document through the writer pipeline: incremental
        // revisions collapse into one clean file, orphan objects are dropped,
        // and reachable objects (including ones added incrementally) survive.
        const auto resaveInput =
            std::filesystem::temp_directory_path() / "pdfpp_resave_input.pdf";
        const auto resaveIncremental =
            std::filesystem::temp_directory_path() / "pdfpp_resave_incremental.pdf";
        const auto resaveOutput =
            std::filesystem::temp_directory_path() / "pdfpp_resave_output.pdf";
        const auto resaveOutputObjStm =
            std::filesystem::temp_directory_path() / "pdfpp_resave_output_objstm.pdf";
        PdfWriter sourceWriter;
        const auto sourcePage = sourceWriter.AddPage({0, 0, 400, 500});
        sourceWriter.GetCanvas(sourcePage).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(36, 460).ShowText("Resave target text").EndText();
        PdfSaveOptions sourceOptions;
        sourceOptions.writeObjectStreams = true;
        sourceWriter.Save(resaveInput, sourceOptions);

        auto sourceDocument = PdfDocument::Open(resaveInput);
        const auto pageReference = sourceDocument.GetPageReference(0U);
        const auto& pageObject = sourceDocument.GetObject(pageReference);
        const auto* pageDictionary = pageObject.AsDictionary();
        PdfDictionary newPage;
        if (pageDictionary) {
            for (const auto& [key, value] : pageDictionary->values()) newPage.Put(key, value);
        }
        {
            CPPPdf::Internal::PdfIncrementalWriterOptions incrementalOptions;
            incrementalOptions.writeObjectStreams = true;
            CPPPdf::Internal::PdfIncrementalWriter incremental(resaveInput, resaveIncremental,
                                                               sourceDocument, incrementalOptions);
            const auto first = CPPPdf::Internal::PdfIncrementalWriter::NextObjectNumber(sourceDocument);
            CPPPdf::PdfDictionary action;
            action.Put(PdfName("S"), PdfObject(PdfName("URI")));
            action.Put(PdfName("URI"), PdfObject(std::string("https://example.com/resaved")));
            CPPPdf::PdfDictionary link;
            link.Put(PdfName("Type"), PdfObject(PdfName("Annot")));
            link.Put(PdfName("Subtype"), PdfObject(PdfName("Link")));
            CPPPdf::PdfArray rectangle;
            rectangle.push_back(PdfObject(std::int64_t{10}));
            rectangle.push_back(PdfObject(std::int64_t{10}));
            rectangle.push_back(PdfObject(std::int64_t{100}));
            rectangle.push_back(PdfObject(std::int64_t{50}));
            link.Put(PdfName("Rect"), PdfObject(std::move(rectangle)));
            link.Put(PdfName("A"), PdfObject(std::move(action)));
            incremental.WriteDictionary(PdfReference{first, 0U}, link);
            CPPPdf::PdfArray annots;
            annots.push_back(PdfObject::IndirectReference(first, 0U));
            newPage.Put(PdfName("Annots"), PdfObject(std::move(annots)));
            incremental.WriteDictionary(pageReference, newPage);
            incremental.Finish(first + 1U);
        }

        PdfWriter::Resave(resaveIncremental, resaveOutput);
        auto resaved = PdfDocument::Open(resaveOutput);
        PDFPP_TEST_CHECK(resaved.GetPageText(0U).find("Resave target text") != std::string::npos);
        PDFPP_TEST_CHECK(resaved.GetTrailerDictionary().find("/Prev") == std::string::npos);
        const auto& resavedPage = resaved.GetObject(resaved.GetPageReference(0U));
        const auto* resavedPageDictionary = resavedPage.AsDictionary();
        const auto* annots = resavedPageDictionary
            ? resavedPageDictionary->GetAsArray(PdfName("Annots")) : nullptr;
        PDFPP_TEST_CHECK(annots != nullptr && annots->size() == 1U);

        PdfSaveOptions objStmResaveOptions;
        objStmResaveOptions.writeObjectStreams = true;
        PdfWriter::Resave(resaveIncremental, resaveOutputObjStm, objStmResaveOptions);
        auto resavedObjStm = PdfDocument::Open(resaveOutputObjStm);
        PDFPP_TEST_CHECK(resavedObjStm.GetPageText(0U).find("Resave target text") != std::string::npos);
        bool objStmPresent = false;
        for (std::uint32_t number = 1U; number < 80U; ++number) {
            const auto entry = resavedObjStm.GetXrefEntry(number);
            if (entry && entry->type == CPPPdf::PdfXrefEntry::Type::Compressed) {
                objStmPresent = true;
                break;
            }
        }
        PDFPP_TEST_CHECK(objStmPresent);
        std::filesystem::remove(resaveInput);
        std::filesystem::remove(resaveIncremental);
        std::filesystem::remove(resaveOutput);
        std::filesystem::remove(resaveOutputObjStm);
    }
}

void TestResaveDeduplicatesStreams() {
    using namespace CPPPdf;
    {
        // Resave with deduplicateObjects merges byte-identical stream objects:
        // two pages with identical content share a single content stream.
        const auto dedupInput =
            std::filesystem::temp_directory_path() / "pdfpp_dedup_input.pdf";
        const auto dedupPlain =
            std::filesystem::temp_directory_path() / "pdfpp_dedup_plain.pdf";
        const auto dedupOutput =
            std::filesystem::temp_directory_path() / "pdfpp_dedup_output.pdf";
        PdfWriter dedupWriter;
        const std::size_t dedupPage0 = dedupWriter.AddPage({0, 0, 400, 500});
        const std::size_t dedupPage1 = dedupWriter.AddPage({0, 0, 400, 500});
        const auto drawDupText = [&dedupWriter](const std::size_t page) {
            dedupWriter.GetCanvas(page).BeginText().SetFontAndSize("Helvetica", 12)
                .MoveText(36, 460).ShowText("Shared stream text").EndText();
        };
        drawDupText(dedupPage0);
        drawDupText(dedupPage1);
        dedupWriter.Save(dedupInput);

        auto dedupSource = PdfDocument::Open(dedupInput);
        const auto contentRefOf = [](const PdfDocument& document, const PdfReference page) -> PdfReference {
            const auto& object = document.GetObject(page);
            const auto* dictionary = object.AsDictionary();
            const auto* contents = dictionary ? dictionary->Find(PdfName("Contents")) : nullptr;
            const auto reference = contents ? contents->AsReference() : std::nullopt;
            if (!reference) return {0U, 0U};
            return {reference->first, reference->second};
        };
        const auto sourceContent0 = contentRefOf(dedupSource, dedupSource.GetPageReference(0U));
        const auto sourceContent1 = contentRefOf(dedupSource, dedupSource.GetPageReference(1U));
        PDFPP_TEST_CHECK(sourceContent0.objectNumber != 0U);
        PDFPP_TEST_CHECK(sourceContent0.objectNumber != sourceContent1.objectNumber);

        PdfSaveOptions plainOptions;
        PdfWriter::Resave(dedupInput, dedupPlain, plainOptions);
        auto plainResaved = PdfDocument::Open(dedupPlain);
        PDFPP_TEST_CHECK(plainResaved.GetPageText(0U).find("Shared stream text") != std::string::npos);
        PDFPP_TEST_CHECK(plainResaved.GetPageText(1U).find("Shared stream text") != std::string::npos);
        const auto plainContent0 = contentRefOf(plainResaved, plainResaved.GetPageReference(0U));
        const auto plainContent1 = contentRefOf(plainResaved, plainResaved.GetPageReference(1U));
        PDFPP_TEST_CHECK(plainContent0.objectNumber != plainContent1.objectNumber);

        PdfSaveOptions dedupOptions;
        dedupOptions.deduplicateObjects = true;
        PdfWriter::Resave(dedupInput, dedupOutput, dedupOptions);
        auto dedupResaved = PdfDocument::Open(dedupOutput);
        PDFPP_TEST_CHECK(dedupResaved.GetPageText(0U).find("Shared stream text") != std::string::npos);
        PDFPP_TEST_CHECK(dedupResaved.GetPageText(1U).find("Shared stream text") != std::string::npos);
        const auto dedupContent0 = contentRefOf(dedupResaved, dedupResaved.GetPageReference(0U));
        const auto dedupContent1 = contentRefOf(dedupResaved, dedupResaved.GetPageReference(1U));
        PDFPP_TEST_CHECK(dedupContent0.objectNumber != 0U);
        PDFPP_TEST_CHECK(dedupContent0.objectNumber == dedupContent1.objectNumber);
        std::filesystem::remove(dedupInput);
        std::filesystem::remove(dedupPlain);
        std::filesystem::remove(dedupOutput);
    }
}

void TestResaveEncryptedPreservesPasswords() {
    using namespace CPPPdf;
    {
        // Resaving an encrypted document preserves the same passwords.
        const auto encryptedInput =
            std::filesystem::temp_directory_path() / "pdfpp_resave_encrypted_input.pdf";
        const auto encryptedOutput =
            std::filesystem::temp_directory_path() / "pdfpp_resave_encrypted_output.pdf";
        PdfWriter encryptedWriter;
        const auto encryptedPage = encryptedWriter.AddPage({0, 0, 300, 400});
        encryptedWriter.GetCanvas(encryptedPage).BeginText().SetFontAndSize("Helvetica", 12)
            .MoveText(24, 360).ShowText("Encrypted resave text").EndText();
        PdfEncryptionOptions encryptionOptions;
        encryptionOptions.userPassword = "resave-user";
        encryptionOptions.ownerPassword = "resave-owner";
        encryptionOptions.algorithm = PdfEncryptionAlgorithm::Aes128;
        encryptedWriter.SetEncryption(encryptionOptions);
        encryptedWriter.Save(encryptedInput);

        PdfReaderOptions readerOptions;
        readerOptions.password = "resave-user";
        PdfWriter::Resave(encryptedInput, encryptedOutput, readerOptions);

        PdfReaderOptions reopenOptions;
        reopenOptions.password = "resave-user";
        auto resavedEncrypted = PdfDocument::Open(encryptedOutput, reopenOptions);
        PDFPP_TEST_CHECK(resavedEncrypted.IsEncrypted());
        PDFPP_TEST_CHECK(resavedEncrypted.GetPageText(0U).find("Encrypted resave text") != std::string::npos);
        std::filesystem::remove(encryptedInput);
        std::filesystem::remove(encryptedOutput);
    }
}

void TestCanvasCatalogAndPageOrganizer() {
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
    PDFPP_TEST_CHECK(jpeg.GetWidth() == 2U && jpeg.GetHeight() == 1U);
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
    PDFPP_TEST_CHECK(writer.GetPageCount() == 3);
    writer.MovePage(2, 1);
    writer.RemovePage(2);
    PDFPP_TEST_CHECK(writer.GetPageCount() == 2);

    PdfDestinationOptions namedDestination;
    namedDestination.pageIndex = 1U;
    namedDestination.destinationType = PdfDestinationType::XYZ;
    namedDestination.left = 72.0;
    namedDestination.top = 760.0;
    namedDestination.zoom = 1.25;
    writer.AddNamedDestination("details", namedDestination);
    PDFPP_TEST_CHECK(writer.GetNamedDestinationCount() == 1U);

    PdfLinkOptions internalLink;
    internalLink.rectangle = {72, 680, 180, 705};
    writer.AddNamedDestinationLink(0U, "details", internalLink);
    PdfLinkOptions uriLink;
    uriLink.rectangle = {72, 640, 220, 665};
    uriLink.drawBorder = true;
    uriLink.borderWidth = 1.5;
    writer.AddUriLink(0U, "https://example.com/pdfpp", uriLink);
    PDFPP_TEST_CHECK(writer.GetLinkCount(0U) == 2U);

    PdfViewerPreferences viewerPreferences;
    viewerPreferences.pageLayout = PdfPageLayout::TwoColumnLeft;
    viewerPreferences.pageMode = PdfPageMode::UseOutlines;
    viewerPreferences.readingDirection = PdfReadingDirection::RightToLeft;
    viewerPreferences.fitWindow = true;
    viewerPreferences.centerWindow = true;
    viewerPreferences.displayDocumentTitle = true;
    viewerPreferences.nonFullScreenPageMode = PdfPageMode::UseThumbs;
    viewerPreferences.printScaling = PdfPrintScaling::None;
    viewerPreferences.duplex = PdfDuplexMode::DuplexFlipLongEdge;
    viewerPreferences.pickTrayByPdfSize = true;
    viewerPreferences.numberOfCopies = 2U;
    writer.SetViewerPreferences(viewerPreferences);
    PDFPP_TEST_CHECK(writer.GetViewerPreferences().fitWindow);
    PdfDestinationOptions openAction;
    openAction.pageIndex = 1U;
    openAction.destinationType = PdfDestinationType::XYZ;
    openAction.left = 24.0;
    openAction.top = 800.0;
    openAction.zoom = 1.5;
    writer.SetOpenAction(openAction);
    PDFPP_TEST_CHECK(writer.HasOpenAction());

    PdfPageLabelOptions frontMatterLabel;
    frontMatterLabel.style = PdfPageLabelStyle::LowerRoman;
    frontMatterLabel.prefix = "Front-";
    writer.AddPageLabel(0U, frontMatterLabel);
    PdfPageLabelOptions bodyLabel;
    bodyLabel.style = PdfPageLabelStyle::Decimal;
    bodyLabel.prefix = "P-";
    bodyLabel.startNumber = 5U;
    writer.AddPageLabel(1U, bodyLabel);
    PDFPP_TEST_CHECK(writer.GetPageLabelCount() == 2U);
    writer.Save(path);

    auto document = PdfDocument::Open(path);
    PDFPP_TEST_CHECK(document.GetPageCount() == 2);
    const auto catalogObject = document.readIndirectObject(document.GetCatalogReference().objectNumber);
    PDFPP_TEST_CHECK(catalogObject.find("/Names") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/PageLabels") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/PageLayout /TwoColumnLeft") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/PageMode /UseOutlines") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/ViewerPreferences") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/Direction /R2L") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/OpenAction [") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/XYZ 24 800 1.5") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/NonFullScreenPageMode /UseThumbs") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/PrintScaling /None") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/Duplex /DuplexFlipLongEdge") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/PickTrayByPDFSize true") != std::string::npos);
    PDFPP_TEST_CHECK(catalogObject.find("/NumCopies 2") != std::string::npos);
    const auto firstPageObject = document.readIndirectObject(document.GetPageReference(0U).objectNumber);
    PDFPP_TEST_CHECK(firstPageObject.find("/Annots") != std::string::npos);
    const auto allObjectNumbers = document.objectNumbers();
    bool foundNamedDestination = false;
    bool foundUriAction = false;
    bool foundNamedLink = false;
    bool foundPageLabels = false;
    for (const auto objectNumber : allObjectNumbers) {
        const auto object = document.readIndirectObject(objectNumber);
        foundNamedDestination = foundNamedDestination ||
            (object.find("(details)") != std::string::npos && object.find("/XYZ") != std::string::npos);
        foundUriAction = foundUriAction || object.find("/S /URI") != std::string::npos;
        foundNamedLink = foundNamedLink ||
            (object.find("/Subtype /Link") != std::string::npos && object.find("/Dest (details)") != std::string::npos);
        foundPageLabels = foundPageLabels ||
            (object.find("/Nums [") != std::string::npos && object.find("/S /r") != std::string::npos &&
             object.find("/P (P-)") != std::string::npos && object.find("/St 5") != std::string::npos);
    }
    PDFPP_TEST_CHECK(foundNamedDestination);
    PDFPP_TEST_CHECK(foundUriAction);
    PDFPP_TEST_CHECK(foundNamedLink);
    PDFPP_TEST_CHECK(foundPageLabels);
    const auto sequentialText = document.GetAllPageText();
    const auto parallelText = document.ExtractAllPageTextParallel(2U);
    PDFPP_TEST_CHECK(sequentialText == parallelText);
    const auto pageText = document.GetPageText(0);
    PDFPP_TEST_CHECK(pageText.find("Hello Phase 10") != std::string::npos);
    PDFPP_TEST_CHECK(pageText.find("APPROVED") != std::string::npos);
    PDFPP_TEST_CHECK(pageText.find("CONFIDENTIAL") != std::string::npos);
    const auto images = document.ExtractImages(0);
    PDFPP_TEST_CHECK(images.size() == 3);
    PDFPP_TEST_CHECK(images[0].info.width == 2);
    PDFPP_TEST_CHECK(images[0].info.height == 2);
    PDFPP_TEST_CHECK(images[0].info.colorSpace == PdfImageColorSpace::DeviceRGB);
    PDFPP_TEST_CHECK(images[0].info.encoding == PdfImageEncoding::Flate);
    PDFPP_TEST_CHECK(images[0].info.decoded);
    PDFPP_TEST_CHECK(images[0].decodedBytes.size() == pixels.size());
    PDFPP_TEST_CHECK(images[0].info.boundingBox.left == 100.0);
    PDFPP_TEST_CHECK(images[0].info.boundingBox.bottom == 200.0);
    PDFPP_TEST_CHECK(images[0].info.boundingBox.right == 140.0);
    PDFPP_TEST_CHECK(images[0].info.boundingBox.top == 230.0);
    PDFPP_TEST_CHECK(images[1].info.encoding == PdfImageEncoding::Dct);
    PDFPP_TEST_CHECK(!images[1].info.decoded);
    PDFPP_TEST_CHECK(images[1].info.width == 2U && images[1].info.height == 1U);
    PDFPP_TEST_CHECK(images[1].encodedBytes.size() == jpegBytes.size());
    PDFPP_TEST_CHECK(std::equal(images[1].encodedBytes.begin(), images[1].encodedBytes.end(), jpegBytes.begin(), jpegBytes.end()));

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
    PDFPP_TEST_CHECK(stampResult.modifiedPageCount == 1U);

    const auto watermarkedPath = std::filesystem::temp_directory_path() / "pdfpp_existing_watermark_test.pdf";
    PdfWatermarkOptions existingWatermark;
    existingWatermark.text = "DRAFT";
    existingWatermark.fontSize = 36;
    existingWatermark.opacity = 0.2;
    existingWatermark.layer = PdfStampLayer::Background;
    const auto watermarkResult = PdfPageEditor::AddWatermarkToAllPages(stampedPath, watermarkedPath, existingWatermark);
    PDFPP_TEST_CHECK(watermarkResult.modifiedPageCount == 2U);

    auto editedDocument = PdfDocument::Open(watermarkedPath);
    PDFPP_TEST_CHECK(editedDocument.GetPageText(0).find("EXISTING STAMP") != std::string::npos);
    PDFPP_TEST_CHECK(editedDocument.GetPageText(0).find("DRAFT") != std::string::npos);
    PDFPP_TEST_CHECK(editedDocument.GetPageText(1).find("DRAFT") != std::string::npos);

    const auto imageStampedPath = std::filesystem::temp_directory_path() / "pdfpp_existing_image_stamp_test.pdf";
    PdfImageStampOptions existingImageStamp;
    existingImageStamp.rectangle = {300, 300, 360, 360};
    existingImageStamp.opacity = 0.55;
    existingImageStamp.drawBorder = true;
    existingImageStamp.borderColor = PdfColor::Red();
    existingImageStamp.borderWidth = 2.0;
    const auto imageStampResult = PdfPageEditor::AddImageStamp(
        watermarkedPath, imageStampedPath, 0U, image, existingImageStamp);
    PDFPP_TEST_CHECK(imageStampResult.modifiedPageCount == 1U);

    auto imageStampedDocument = PdfDocument::Open(imageStampedPath);
    const auto editedImages = imageStampedDocument.ExtractImages(0U);
    PDFPP_TEST_CHECK(editedImages.size() >= 4U);
    const auto& lastImage = editedImages.back();
    PDFPP_TEST_CHECK(lastImage.info.width == 2U);
    PDFPP_TEST_CHECK(lastImage.info.height == 2U);
    PDFPP_TEST_CHECK(lastImage.info.boundingBox.left == 300.0);
    PDFPP_TEST_CHECK(lastImage.info.boundingBox.bottom == 300.0);
    PDFPP_TEST_CHECK(lastImage.info.boundingBox.right == 360.0);
    PDFPP_TEST_CHECK(lastImage.info.boundingBox.top == 360.0);

    const auto reorderedPath = std::filesystem::temp_directory_path() / "pdfpp_reordered_pages_test.pdf";
    const auto reorderedResult = PdfPageOrganizer::ReorderPages(path, reorderedPath, {1U, 0U});
    PDFPP_TEST_CHECK(reorderedResult.originalPageCount == 2U);
    PDFPP_TEST_CHECK(reorderedResult.outputPageCount == 2U);
    auto reorderedDocument = PdfDocument::Open(reorderedPath);
    PDFPP_TEST_CHECK(reorderedDocument.GetPageCount() == 2U);
    PDFPP_TEST_CHECK(reorderedDocument.GetPageText(0U).find("Parallel page two") != std::string::npos);
    PDFPP_TEST_CHECK(reorderedDocument.GetPageText(1U).find("Hello Phase 10") != std::string::npos);

    const auto extractedPath = std::filesystem::temp_directory_path() / "pdfpp_extracted_page_test.pdf";
    const auto extractedResult = PdfPageOrganizer::ExtractPages(path, extractedPath, {1U});
    PDFPP_TEST_CHECK(extractedResult.outputPageCount == 1U);
    auto extractedDocument = PdfDocument::Open(extractedPath);
    PDFPP_TEST_CHECK(extractedDocument.GetPageCount() == 1U);
    PDFPP_TEST_CHECK(extractedDocument.GetPageText(0U).find("Parallel page two") != std::string::npos);

    const auto removedPath = std::filesystem::temp_directory_path() / "pdfpp_removed_page_test.pdf";
    const auto removedResult = PdfPageOrganizer::RemovePages(path, removedPath, {0U});
    PDFPP_TEST_CHECK(removedResult.outputPageCount == 1U);
    auto removedDocument = PdfDocument::Open(removedPath);
    PDFPP_TEST_CHECK(removedDocument.GetPageText(0U).find("Parallel page two") != std::string::npos);

    const auto splitDirectory = std::filesystem::temp_directory_path() / "pdfpp_split_pages_test";
    const auto splitResults = PdfPageOrganizer::SplitEvery(path, splitDirectory, 1U, "page");
    PDFPP_TEST_CHECK(splitResults.size() == 2U);
    PDFPP_TEST_CHECK(PdfDocument::Open(splitResults[0].outputPath).GetPageCount() == 1U);
    PDFPP_TEST_CHECK(PdfDocument::Open(splitResults[1].outputPath).GetPageCount() == 1U);

    const auto nestedPath = std::filesystem::temp_directory_path() / "pdfpp_nested_page_tree_test.pdf";
    const auto nestedReorderedPath = std::filesystem::temp_directory_path() / "pdfpp_nested_page_tree_reordered.pdf";
    const auto nestedExtractedPath = std::filesystem::temp_directory_path() / "pdfpp_nested_page_tree_extracted.pdf";
    WriteNestedPageTreePdf(nestedPath);

    auto nestedDocument = PdfDocument::Open(nestedPath);
    PDFPP_TEST_CHECK(nestedDocument.GetPageCount() == 2U);
    PDFPP_TEST_CHECK(nestedDocument.GetPageText(0U).find("Nested first") != std::string::npos);
    PDFPP_TEST_CHECK(nestedDocument.GetPageText(1U).find("Nested second") != std::string::npos);

    const auto nestedReorderResult = PdfPageOrganizer::ReorderPages(nestedPath, nestedReorderedPath, {1U, 0U});
    PDFPP_TEST_CHECK(nestedReorderResult.outputPageCount == 2U);
    auto nestedReordered = PdfDocument::Open(nestedReorderedPath);
    PDFPP_TEST_CHECK(nestedReordered.GetPageCount() == 2U);
    PDFPP_TEST_CHECK(nestedReordered.GetPageText(0U).find("Nested second") != std::string::npos);
    PDFPP_TEST_CHECK(nestedReordered.GetPage(0U).GetMediaBox().right == 500.0);
    PDFPP_TEST_CHECK(nestedReordered.GetPageText(1U).find("Nested first") != std::string::npos);
    PDFPP_TEST_CHECK(nestedReordered.GetPage(1U).GetMediaBox().right == 300.0);

    const auto nestedExtractResult = PdfPageOrganizer::ExtractPages(nestedPath, nestedExtractedPath, {1U});
    PDFPP_TEST_CHECK(nestedExtractResult.outputPageCount == 1U);
    auto nestedExtracted = PdfDocument::Open(nestedExtractedPath);
    PDFPP_TEST_CHECK(nestedExtracted.GetPageCount() == 1U);
    PDFPP_TEST_CHECK(nestedExtracted.GetPageText(0U).find("Nested second") != std::string::npos);
    PDFPP_TEST_CHECK(nestedExtracted.GetPage(0U).GetMediaBox().right == 500.0);

    const auto mergedPath = std::filesystem::temp_directory_path() / "pdfpp_merged_documents_test.pdf";
    const auto mergeResult = PdfPageImporter::MergeDocuments({path, nestedPath}, mergedPath);
    PDFPP_TEST_CHECK(mergeResult.sourceDocumentCount == 2U);
    PDFPP_TEST_CHECK(mergeResult.importedPageCount == 4U);
    PDFPP_TEST_CHECK(mergeResult.importedObjectCount > 4U);
    auto mergedDocument = PdfDocument::Open(mergedPath);
    PDFPP_TEST_CHECK(mergedDocument.GetPageCount() == 4U);
    PDFPP_TEST_CHECK(mergedDocument.GetPageText(0U).find("Hello Phase 10") != std::string::npos);
    PDFPP_TEST_CHECK(mergedDocument.GetPageText(1U).find("Parallel page two") != std::string::npos);
    PDFPP_TEST_CHECK(mergedDocument.GetPageText(2U).find("Nested first") != std::string::npos);
    PDFPP_TEST_CHECK(mergedDocument.GetPageText(3U).find("Nested second") != std::string::npos);
    PDFPP_TEST_CHECK(!mergedDocument.ExtractImages(0U).empty());

    const auto catalogSourcePath = std::filesystem::temp_directory_path() / "pdfpp_catalog_structures_source.pdf";
    const auto catalogMergedPath = std::filesystem::temp_directory_path() / "pdfpp_catalog_structures_merged.pdf";
    WriteCatalogStructuresPdf(catalogSourcePath);
    const auto catalogMergeResult = PdfPageImporter::MergeDocuments(
        {catalogSourcePath, nestedPath}, catalogMergedPath);
    PDFPP_TEST_CHECK(catalogMergeResult.preservedDocumentInfo);
    PDFPP_TEST_CHECK(catalogMergeResult.preservedCatalogEntryCount >= 4U);

    auto catalogMerged = PdfDocument::Open(catalogMergedPath);
    PDFPP_TEST_CHECK(catalogMerged.GetPageCount() == 3U);
    PDFPP_TEST_CHECK(catalogMerged.GetDocumentInfo().title == "Catalog Source");
    const PdfDictionary* mergedCatalog =
        catalogMerged.GetObject(catalogMerged.GetCatalogReference()).AsDictionary();
    PDFPP_TEST_CHECK(mergedCatalog != nullptr);
    PDFPP_TEST_CHECK(mergedCatalog->Contains(PdfName("Outlines")));
    PDFPP_TEST_CHECK(mergedCatalog->Contains(PdfName("Metadata")));
    PDFPP_TEST_CHECK(mergedCatalog->Get(PdfName("PageMode")).AsName()->value() == "UseOutlines");

    const auto outlinesPair = *mergedCatalog->Get(PdfName("Outlines")).AsReference();
    const PdfDictionary* outlines = catalogMerged.GetObject({outlinesPair.first, outlinesPair.second}).AsDictionary();
    PDFPP_TEST_CHECK(outlines != nullptr);
    const auto firstOutlinePair = *outlines->Get(PdfName("First")).AsReference();
    const PdfDictionary* firstOutline =
        catalogMerged.GetObject({firstOutlinePair.first, firstOutlinePair.second}).AsDictionary();
    PDFPP_TEST_CHECK(firstOutline != nullptr);
    const PdfArray* destination = firstOutline->Get(PdfName("Dest")).AsArray();
    PDFPP_TEST_CHECK(destination != nullptr);
    const auto destinationPage = *destination->at(0U).AsReference();
    const PdfReference firstOutputPage = catalogMerged.GetPageReference(0U);
    PDFPP_TEST_CHECK(destinationPage.first == firstOutputPage.objectNumber);

    const auto formOnePath = std::filesystem::temp_directory_path() / "pdfpp_form_one.pdf";
    const auto formTwoPath = std::filesystem::temp_directory_path() / "pdfpp_form_two.pdf";
    const auto mergedFormsPath = std::filesystem::temp_directory_path() / "pdfpp_merged_forms.pdf";
    WriteAcroFormPdf(formOnePath, "Form source one", "Customer", "Alice");
    WriteAcroFormPdf(formTwoPath, "Form source two", "Customer", "Bob");

    const auto formFields = PdfAcroForm::GetFields(formOnePath);
    PDFPP_TEST_CHECK(formFields.size() == 1U);
    PDFPP_TEST_CHECK(formFields[0].name == "Customer");
    PDFPP_TEST_CHECK(formFields[0].type == PdfFormFieldType::Text);
    PDFPP_TEST_CHECK(formFields[0].value == "Alice");
    PDFPP_TEST_CHECK(formFields[0].pageIndex && *formFields[0].pageIndex == 0U);

    const auto updatedFormPath = std::filesystem::temp_directory_path() / "pdfpp_updated_form.pdf";
    const auto formUpdateResult = PdfAcroForm::SetFieldValues(
        formOnePath,
        updatedFormPath,
        {{"Customer", "Carol"}});
    PDFPP_TEST_CHECK(formUpdateResult.updatedFieldCount == 1U);
    const auto updatedFields = PdfAcroForm::GetFields(updatedFormPath);
    PDFPP_TEST_CHECK(updatedFields.size() == 1U);
    PDFPP_TEST_CHECK(updatedFields[0].value == "Carol");

    const auto appearanceFormPath = std::filesystem::temp_directory_path() / "pdfpp_form_appearance.pdf";
    const auto appearanceResult = PdfAcroForm::GenerateAppearances(
        updatedFormPath, appearanceFormPath, {"Customer"});
    PDFPP_TEST_CHECK(appearanceResult.generatedAppearanceCount == 1U);
    auto appearanceDocument = PdfDocument::Open(appearanceFormPath);
    const auto appearanceFields = PdfAcroForm::GetFields(appearanceDocument);
    PDFPP_TEST_CHECK(appearanceFields.size() == 1U);
    const PdfDictionary* appearanceWidget =
        appearanceDocument.GetObject(appearanceFields[0].widgetReferences[0]).AsDictionary();
    PDFPP_TEST_CHECK(appearanceWidget != nullptr && appearanceWidget->Contains(PdfName("AP")));

    const auto flattenedFormPath = std::filesystem::temp_directory_path() / "pdfpp_form_flattened.pdf";
    const auto flattenResult = PdfAcroForm::FlattenFields(
        appearanceFormPath, flattenedFormPath, {"Customer"});
    PDFPP_TEST_CHECK(flattenResult.flattenedFieldCount == 1U);
    PDFPP_TEST_CHECK(flattenResult.removedWidgetCount == 1U);
    auto flattenedDocument = PdfDocument::Open(flattenedFormPath);
    PDFPP_TEST_CHECK(PdfAcroForm::GetFields(flattenedDocument).empty());
    const PdfDictionary* flattenedPage =
        flattenedDocument.GetObject(flattenedDocument.GetPageReference(0U)).AsDictionary();
    PDFPP_TEST_CHECK(flattenedPage != nullptr && !flattenedPage->Contains(PdfName("Annots")));
    PDFPP_TEST_CHECK(flattenedDocument.GetPageText(0U).find("Carol") != std::string::npos);
    const auto formMergeResult = PdfPageImporter::MergeDocuments(
        {formOnePath, formTwoPath}, mergedFormsPath);
    PDFPP_TEST_CHECK(formMergeResult.preservedAcroForm);
    PDFPP_TEST_CHECK(formMergeResult.importedFormFieldCount == 2U);

    auto mergedForms = PdfDocument::Open(mergedFormsPath);
    const PdfDictionary* formCatalog =
        mergedForms.GetObject(mergedForms.GetCatalogReference()).AsDictionary();
    PDFPP_TEST_CHECK(formCatalog != nullptr);
    const auto acroFormPair = *formCatalog->Get(PdfName("AcroForm")).AsReference();
    const PdfDictionary* acroForm =
        mergedForms.GetObject({acroFormPair.first, acroFormPair.second}).AsDictionary();
    PDFPP_TEST_CHECK(acroForm != nullptr);
    const PdfArray* fields = acroForm->GetAsArray(PdfName("Fields"));
    PDFPP_TEST_CHECK(fields != nullptr && fields->size() == 2U);

    const auto firstFieldPair = *fields->at(0U).AsReference();
    const auto secondFieldPair = *fields->at(1U).AsReference();
    const PdfDictionary* firstField =
        mergedForms.GetObject({firstFieldPair.first, firstFieldPair.second}).AsDictionary();
    const PdfDictionary* secondField =
        mergedForms.GetObject({secondFieldPair.first, secondFieldPair.second}).AsDictionary();
    PDFPP_TEST_CHECK(firstField != nullptr && secondField != nullptr);
    PDFPP_TEST_CHECK(*firstField->Get(PdfName("T")).AsString() == "Customer");
    PDFPP_TEST_CHECK(*secondField->Get(PdfName("T")).AsString() == "Source2.Customer");
    PDFPP_TEST_CHECK(*firstField->Get(PdfName("V")).AsString() == "Alice");
    PDFPP_TEST_CHECK(*secondField->Get(PdfName("V")).AsString() == "Bob");
    const auto secondWidgetPage = *secondField->Get(PdfName("P")).AsReference();
    PDFPP_TEST_CHECK(secondWidgetPage.first == mergedForms.GetPageReference(1U).objectNumber);

    const auto copiedPath = std::filesystem::temp_directory_path() / "pdfpp_copied_pages_test.pdf";
    PdfPageImportSource firstSelection{path, {1U}};
    PdfPageImportSource secondSelection{nestedPath, {0U}};
    const auto copyResult = PdfPageImporter::CopyPages({firstSelection, secondSelection}, copiedPath);
    PDFPP_TEST_CHECK(copyResult.importedPageCount == 2U);
    auto copiedDocument = PdfDocument::Open(copiedPath);
    PDFPP_TEST_CHECK(copiedDocument.GetPageCount() == 2U);
    PDFPP_TEST_CHECK(copiedDocument.GetPageText(0U).find("Parallel page two") != std::string::npos);
    PDFPP_TEST_CHECK(copiedDocument.GetPageText(1U).find("Nested first") != std::string::npos);
    PDFPP_TEST_CHECK(copiedDocument.GetPage(1U).GetMediaBox().right == 300.0);

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

}

int RunWriterIntegrationTests() {
    CPPPdfTest::TestRunner runner;
    runner.Run("Writer.UnicodeTrueType", TestUnicodeTrueTypeWriter);
    runner.Run("Writer.XrefStreamAndClassic", TestXrefStreamAndClassicOutput);
    runner.Run("Writer.ObjectStreamRoundTrip", TestObjectStreamRoundTrip);
    runner.Run("Writer.IncrementalObjectStream", TestIncrementalObjectStream);
    runner.Run("Writer.IncrementalEncryptedObjectStream", TestIncrementalEncryptedObjectStream);
    runner.Run("Writer.ResaveCollapsesIncremental", TestResaveCollapsesIncremental);
    runner.Run("Writer.ResaveDeduplicatesStreams", TestResaveDeduplicatesStreams);
    runner.Run("Writer.ResaveEncryptedPreservesPasswords", TestResaveEncryptedPreservesPasswords);
    runner.Run("Writer.CanvasCatalogAndPageOrganizer", TestCanvasCatalogAndPageOrganizer);
    return runner.PrintSummary("Writer integration");
}

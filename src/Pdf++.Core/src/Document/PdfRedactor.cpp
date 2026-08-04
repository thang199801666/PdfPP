#include <CPPPdf/Document/PdfRedactor.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Text/PdfTextSearch.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Writer/PdfIncrementalWriter.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace CPPPdf {
namespace {

std::string readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open input PDF: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// Builds the black-fill content stream over the given rectangles (PDF coords,
// already adjusted for the page's rotation).
std::string redactionCommands(const std::vector<PdfRectangle>& rectangles) {
    std::ostringstream stream;
    stream << "q 0 0 0 rg\n";
    for (const auto& rect : rectangles) {
        stream << rect.left << ' ' << rect.bottom << ' '
               << (rect.right - rect.left) << ' ' << (rect.top - rect.bottom) << " re f\n";
    }
    stream << "Q\n";
    return stream.str();
}

} // namespace

PdfRedactor::RedactionResult PdfRedactor::RedactRegex(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<RegexRedactionRequest>& requests,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    RedactionResult result{outputPath, 0U, 0U};
    if (requests.empty()) {
        const std::string source = readFileBytes(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        return result;
    }
    std::map<std::size_t, std::vector<PdfRectangle>> pageRectangles;
    for (const auto& request : requests) {
        if (request.pageIndex >= document.GetPageCount()) continue;
        const auto chunks = document.ExtractTextChunks(request.pageIndex);
        PdfRegexSearchOptions options;
        options.caseInsensitive = request.caseInsensitive;
        const auto matches = PdfTextSearch::FindRegex(chunks, request.pattern, options);
        for (const auto& match : matches) {
            PdfRectangle covered = match.boundingBox;
            covered.left -= request.padding;
            covered.bottom -= request.padding;
            covered.right += request.padding;
            covered.top += request.padding;
            pageRectangles[request.pageIndex].push_back(covered);
            ++result.redactionCount;
        }
    }
    if (result.redactionCount == 0U) {
        const std::string source = readFileBytes(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        return result;
    }
    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t nextObject = Internal::PdfIncrementalWriter::NextObjectNumber(document);
    result.modifiedPageCount = pageRectangles.size();
    for (const auto& [pageIndex, rectangles] : pageRectangles) {
        const PdfReference pageReference = document.GetPageReference(pageIndex);
        const PdfObject& pageObject = document.GetObject(pageReference);
        const PdfDictionary* dictionary = pageObject.AsDictionary();
        if (!dictionary) continue;
        PdfDictionary updatedPage = *dictionary;
        const std::string commands = redactionCommands(rectangles);
        const PdfReference contentReference{nextObject++, 0U};
        writer.WriteRawObject(contentReference,
            "<< /Length " + std::to_string(commands.size()) + " >>\nstream\n"
            + commands + "endstream");
        PdfArray contents;
        if (const PdfObject* oldContents = dictionary->Find(PdfName("Contents"))) {
            if (const PdfArray* array = oldContents->AsArray()) {
                for (const auto& item : array->values()) contents.push_back(item);
            } else {
                contents.push_back(*oldContents);
            }
        }
        contents.push_back(PdfObject::IndirectReference(contentReference.objectNumber, 0U));
        updatedPage.Put(PdfName("Contents"), PdfObject(std::move(contents)));
        writer.WriteDictionary(pageReference, updatedPage);
    }
    writer.Finish(nextObject);
    return result;
}

PdfRedactor::RedactionResult PdfRedactor::RedactText(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<RedactionRequest>& requests,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    RedactionResult result{outputPath, 0U, 0U};
    if (requests.empty()) {
        const std::string source = readFileBytes(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        return result;
    }

    // Group rectangles by page.
    std::map<std::size_t, std::vector<PdfRectangle>> pageRectangles;
    for (const auto& request : requests) {
        if (request.pageIndex >= document.GetPageCount()) continue;
        if (request.region.empty()) {
            // Locate the text and use its geometry.
            const auto chunks = document.ExtractTextChunks(request.pageIndex);
            const auto matches = PdfTextSearch::Find(chunks, request.text);
            for (const auto& match : matches) {
                PdfRectangle covered = match.boundingBox;
                covered.left -= request.padding;
                covered.bottom -= request.padding;
                covered.right += request.padding;
                covered.top += request.padding;
                pageRectangles[request.pageIndex].push_back(covered);
                ++result.redactionCount;
            }
        } else {
            pageRectangles[request.pageIndex].push_back(request.region);
            ++result.redactionCount;
        }
    }
    if (result.redactionCount == 0U) {
        const std::string source = readFileBytes(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        return result;
    }

    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t nextObject = Internal::PdfIncrementalWriter::NextObjectNumber(document);
    result.modifiedPageCount = pageRectangles.size();
    for (const auto& [pageIndex, rectangles] : pageRectangles) {
        const PdfReference pageReference = document.GetPageReference(pageIndex);
        const PdfObject& pageObject = document.GetObject(pageReference);
        const PdfDictionary* dictionary = pageObject.AsDictionary();
        if (!dictionary) continue;
        PdfDictionary updatedPage = *dictionary;
        const std::string commands = redactionCommands(rectangles);
        const PdfReference contentReference{nextObject++, 0U};
        writer.WriteRawObject(contentReference,
            "<< /Length " + std::to_string(commands.size()) + " >>\nstream\n"
            + commands + "endstream");
        PdfArray contents;
        if (const PdfObject* oldContents = dictionary->Find(PdfName("Contents"))) {
            if (const PdfArray* array = oldContents->AsArray()) {
                for (const auto& item : array->values()) contents.push_back(item);
            } else {
                contents.push_back(*oldContents);
            }
        }
        contents.push_back(PdfObject::IndirectReference(contentReference.objectNumber, 0U));
        updatedPage.Put(PdfName("Contents"), PdfObject(std::move(contents)));
        writer.WriteDictionary(pageReference, updatedPage);
    }
    writer.Finish(nextObject);
    return result;
}

} // namespace CPPPdf

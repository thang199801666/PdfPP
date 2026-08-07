#include <CPPPdf/Annotations/PdfKeywordHighlighter.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Text/PdfTextSearch.hpp>
#include <CPPPdf/Writer/PdfIncrementalUpdate.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <utility>

namespace CPPPdf {
namespace {

PdfArray rectangleArray(const PdfRectangle& rectangle) {
    PdfArray result;
    result.push_back(PdfObject(rectangle.left));
    result.push_back(PdfObject(rectangle.bottom));
    result.push_back(PdfObject(rectangle.right));
    result.push_back(PdfObject(rectangle.top));
    return result;
}

PdfArray quadPointsArray(const PdfKeywordHighlightMatch& match) {
    PdfArray result;
    const auto append = [&result](const PdfRectangle& quad) {
        result.push_back(PdfObject(quad.left));
        result.push_back(PdfObject(quad.top));
        result.push_back(PdfObject(quad.right));
        result.push_back(PdfObject(quad.top));
        result.push_back(PdfObject(quad.left));
        result.push_back(PdfObject(quad.bottom));
        result.push_back(PdfObject(quad.right));
        result.push_back(PdfObject(quad.bottom));
    };
    if (match.rectangles.empty()) append(match.rectangle);
    else for (const auto& quad : match.rectangles) append(quad);
    return result;
}

PdfArray colorArray(const PdfHighlightColor& color) {
    PdfArray result;
    result.push_back(PdfObject(std::clamp(color.red, 0.0, 1.0)));
    result.push_back(PdfObject(std::clamp(color.green, 0.0, 1.0)));
    result.push_back(PdfObject(std::clamp(color.blue, 0.0, 1.0)));
    return result;
}

PdfDictionary makeHighlightAnnotation(const PdfKeywordHighlightMatch& match,
                                      const PdfKeywordHighlightOptions& options) {
    PdfDictionary annotation;
    annotation.Put(PdfName("Type"), PdfObject(PdfName("Annot")));
    annotation.Put(PdfName("Subtype"), PdfObject(PdfName("Highlight")));
    annotation.Put(PdfName("Rect"), PdfObject(rectangleArray(match.rectangle)));
    annotation.Put(PdfName("QuadPoints"), PdfObject(quadPointsArray(match)));
    annotation.Put(PdfName("C"), PdfObject(colorArray(options.color)));
    annotation.Put(PdfName("CA"), PdfObject(std::clamp(options.opacity, 0.0, 1.0)));
    annotation.Put(PdfName("F"), PdfObject(static_cast<std::int64_t>(4)));
    annotation.Put(PdfName("Contents"), PdfObject("Matched: " + options.keyword));
    return annotation;
}

PdfDictionary copyPageDictionary(const PdfDocument& document, const PdfReference& pageReference) {
    const PdfObject& pageObject = document.GetObject(pageReference);
    const PdfDictionary* dictionary = pageObject.AsDictionary();
    if (!dictionary) {
        throw PdfException(PdfErrorCode::MalformedObject, "Page object is not a dictionary.");
    }
    return *dictionary;
}

PdfArray collectExistingAnnotations(const PdfDocument& document,
                                    const PdfDictionary& pageDictionary) {
    PdfArray result;
    const PdfObject* annots = pageDictionary.Find(PdfName("Annots"));
    if (!annots) return result;

    if (const PdfArray* direct = annots->AsArray()) {
        for (const auto& value : direct->values()) result.push_back(value);
        return result;
    }
    if (const auto reference = annots->AsReference()) {
        const PdfObject& resolved = document.GetObject(
            PdfReference{reference->first, reference->second});
        if (const PdfArray* array = resolved.AsArray()) {
            for (const auto& value : array->values()) result.push_back(value);
        }
    }
    return result;
}

std::vector<PdfKeywordHighlightMatch> findMatches(
    const PdfDocument& document,
    const PdfKeywordHighlightOptions& options) {
    if (options.keyword.empty()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Highlight keyword cannot be empty.");
    }

    std::vector<PdfKeywordHighlightMatch> matches;
    PdfTextSearchOptions searchOptions;
    searchOptions.caseInsensitive = options.caseInsensitive;
    searchOptions.lineTolerance = options.lineTolerance;
    searchOptions.maxHorizontalGap = options.maxHorizontalGap;

    for (std::size_t pageIndex = 0; pageIndex < document.GetPageCount(); ++pageIndex) {
        PdfTextExtractionRequest request;
        request.strategy = PdfTextExtractionStrategy::Simple;
        const auto chunks = document.ExtractTextChunks(pageIndex, request);
        const auto pageMatches = PdfTextSearch::Find(chunks, options.keyword, searchOptions);

        for (const auto& pageMatch : pageMatches) {
            std::vector<PdfRectangle> rectangles = pageMatch.rectangles;
            for (auto& rectangle : rectangles) {
                rectangle.bottom -= options.verticalPadding;
                rectangle.top += options.verticalPadding;
            }
            PdfRectangle bounding = pageMatch.boundingBox;
            bounding.bottom -= options.verticalPadding;
            bounding.top += options.verticalPadding;
            matches.push_back(PdfKeywordHighlightMatch{
                pageIndex, pageMatch.matchedText, bounding, std::move(rectangles)});
        }
    }
    return matches;
}

} // namespace

PdfKeywordHighlightResult PdfKeywordHighlighter::FindMatches(
    const std::filesystem::path& inputPath,
    const PdfKeywordHighlightOptions& options,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    PdfKeywordHighlightResult result;
    result.outputPath = inputPath;
    result.matches = findMatches(document, options);
    return result;
}

PdfKeywordHighlightResult PdfKeywordHighlighter::HighlightFile(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfKeywordHighlightOptions& options,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);

    PdfKeywordHighlightResult result;
    result.outputPath = outputPath;
    result.matches = findMatches(document, options);
    if (result.matches.empty()) {
        std::error_code error;
        std::filesystem::copy_file(
            inputPath, outputPath, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            throw PdfException(PdfErrorCode::FileOpenFailed,
                               "Cannot copy unchanged PDF: " + error.message());
        }
        return result;
    }

    std::map<std::size_t, std::vector<const PdfKeywordHighlightMatch*>> byPage;
    for (const auto& match : result.matches) byPage[match.pageIndex].push_back(&match);

    PdfIncrementalUpdate update(document, outputPath);
    for (const auto& [pageIndex, pageMatches] : byPage) {
        const PdfReference pageReference = document.GetPageReference(pageIndex);
        PdfDictionary pageDictionary = copyPageDictionary(document, pageReference);
        PdfArray annotations = collectExistingAnnotations(document, pageDictionary);
        for (const PdfKeywordHighlightMatch* match : pageMatches) {
            const PdfReference annotationReference = update.AddDictionary(
                makeHighlightAnnotation(*match, options));
            annotations.push_back(PdfObject::IndirectReference(
                annotationReference.objectNumber, annotationReference.generation));
        }
        pageDictionary.Put(PdfName("Annots"), PdfObject(std::move(annotations)));
        update.ReplaceDictionary(pageReference, pageDictionary);
    }
    update.Commit();
    return result;
}

} // namespace CPPPdf

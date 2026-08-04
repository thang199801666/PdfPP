#include <CPPPdf/Annotations/PdfKeywordHighlighter.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Text/PdfTextSearch.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace CPPPdf {
namespace {


std::string escapeLiteral(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8U);
    for (const char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '(': result += "\\("; break;
        case ')': result += "\\)"; break;
        case '\r': result += "\\r"; break;
        case '\n': result += "\\n"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

std::string escapeName(std::string_view value) {
    std::ostringstream output;
    output << '/';
    for (const unsigned char ch : value) {
        const bool regular = ch >= 33U && ch <= 126U &&
            ch != '#' && ch != '/' && ch != '%' && ch != '(' && ch != ')' &&
            ch != '<' && ch != '>' && ch != '[' && ch != ']' && ch != '{' && ch != '}';
        if (regular) {
            output << static_cast<char>(ch);
        } else {
            output << '#' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<int>(ch) << std::dec;
        }
    }
    return output.str();
}

void serializeObject(std::ostream& output, const PdfObject& object);

void serializeArray(std::ostream& output, const PdfArray& array) {
    output << '[';
    bool first = true;
    for (const auto& value : array.values()) {
        if (!first) output << ' ';
        first = false;
        serializeObject(output, value);
    }
    output << ']';
}

void serializeDictionary(std::ostream& output, const PdfDictionary& dictionary) {
    output << "<<";
    for (const auto& [name, value] : dictionary.values()) {
        output << '\n' << escapeName(name.value()) << ' ';
        serializeObject(output, value);
    }
    output << "\n>>";
}

void serializeObject(std::ostream& output, const PdfObject& object) {
    switch (object.type()) {
    case PdfObjectType::Null:
        output << "null";
        break;
    case PdfObjectType::Boolean:
        output << (*object.AsBoolean() ? "true" : "false");
        break;
    case PdfObjectType::Integer:
        output << *object.AsInteger();
        break;
    case PdfObjectType::Real:
        output << std::setprecision(12) << *object.AsReal();
        break;
    case PdfObjectType::Name:
        output << escapeName(object.AsName()->value());
        break;
    case PdfObjectType::String:
        output << '(' << escapeLiteral(*object.AsString()) << ')';
        break;
    case PdfObjectType::Array:
        serializeArray(output, *object.AsArray());
        break;
    case PdfObjectType::Dictionary:
        serializeDictionary(output, *object.AsDictionary());
        break;
    case PdfObjectType::Stream:
        serializeDictionary(output, object.AsStream()->dictionary());
        break;
    case PdfObjectType::IndirectReference: {
        const auto reference = object.AsReference();
        output << reference->first << ' ' << reference->second << " R";
        break;
    }
    }
}

PdfDictionary copyPageDictionary(const PdfDocument& document, const PdfReference& pageReference) {
    const PdfObject& pageObject = document.GetObject(pageReference);
    const PdfDictionary* dictionary = pageObject.AsDictionary();
    if (!dictionary) {
        throw PdfException(PdfErrorCode::MalformedObject, "Page object is not a dictionary.");
    }
    return *dictionary;
}

PdfArray collectExistingAnnotations(const PdfDocument& document, const PdfDictionary& pageDictionary) {
    PdfArray result;
    const PdfObject* annots = pageDictionary.Find(PdfName("Annots"));
    if (!annots) return result;

    if (const PdfArray* direct = annots->AsArray()) {
        for (const auto& value : direct->values()) result.push_back(value);
        return result;
    }

    if (const auto reference = annots->AsReference()) {
        const PdfObject& resolved = document.GetObject(PdfReference{reference->first, reference->second});
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
                pageIndex,
                pageMatch.matchedText,
                bounding,
                std::move(rectangles)});
        }
    }
    return matches;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open input PDF: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::uint32_t nextObjectNumber(const PdfDocument& document) {
    std::uint32_t maximum = 0U;
    for (const auto number : document.objectNumbers()) maximum = std::max(maximum, number);
    if (maximum == std::numeric_limits<std::uint32_t>::max()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature, "No free PDF object number remains.");
    }
    return maximum + 1U;
}

PdfDictionary parseTrailerDictionary(const PdfDocument& document) {
    const PdfObject object = Internal::PdfObjectParser::Parse(document.trailerDictionary(), 256U);
    const PdfDictionary* dictionary = object.AsDictionary();
    if (!dictionary) {
        throw PdfException(PdfErrorCode::MalformedXref, "Trailer is not a PDF dictionary.");
    }
    return *dictionary;
}

void writeXrefEntry(std::ostream& output, const std::uint64_t offset, const std::uint16_t generation) {
    output << std::setw(10) << std::setfill('0') << offset << ' '
           << std::setw(5) << std::setfill('0') << generation << " n \n";
}

} // namespace

PdfKeywordHighlightResult PdfKeywordHighlighter::FindMatches(
    const std::filesystem::path& inputPath,
    const PdfKeywordHighlightOptions& options,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (document.IsEncrypted()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Keyword search does not support encrypted PDFs yet.");
    }
    PdfKeywordHighlightResult result;
    result.outputPath = inputPath;
    result.matches = findMatches(document, options);
    return result;
}

PdfKeywordHighlightResult PdfKeywordHighlighter::HighlightFile(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfKeywordHighlightOptions& options) {    PdfDocument document = PdfDocument::Open(inputPath);
    if (document.IsEncrypted()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Incremental keyword highlighting does not support encrypted PDFs yet.");
    }

    PdfKeywordHighlightResult result;
    result.outputPath = outputPath;
    result.matches = findMatches(document, options);

    const std::string original = readFile(inputPath);
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot create highlighted PDF: " + outputPath.string());
    }
    output.write(original.data(), static_cast<std::streamsize>(original.size()));
    if (original.empty() || (original.back() != '\n' && original.back() != '\r')) output << '\n';

    if (result.matches.empty()) {
        return result;
    }

    std::map<std::size_t, std::vector<const PdfKeywordHighlightMatch*>> byPage;
    for (const auto& match : result.matches) byPage[match.pageIndex].push_back(&match);

    std::uint32_t newObjectNumber = nextObjectNumber(document);
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint16_t>> xrefEntries;
    std::unordered_map<std::size_t, std::vector<PdfReference>> annotationReferences;

    for (const auto& [pageIndex, pageMatches] : byPage) {
        auto& references = annotationReferences[pageIndex];
        for (const PdfKeywordHighlightMatch* match : pageMatches) {
            const std::uint32_t annotationObject = newObjectNumber++;
            const std::uint64_t offset = static_cast<std::uint64_t>(output.tellp());
            xrefEntries[annotationObject] = {offset, static_cast<std::uint16_t>(0U)};
            references.push_back(PdfReference{annotationObject, 0U});

            const auto& r = match->rectangle;
            output << annotationObject << " 0 obj\n"
                   << "<< /Type /Annot /Subtype /Highlight\n"
                   << "/Rect [" << r.left << ' ' << r.bottom << ' ' << r.right << ' ' << r.top << "]\n"
                   << "/QuadPoints [";
            if (match->rectangles.empty()) {
                output << r.left << ' ' << r.top << ' '
                       << r.right << ' ' << r.top << ' '
                       << r.left << ' ' << r.bottom << ' '
                       << r.right << ' ' << r.bottom << ' ';
            } else {
                for (const auto& quad : match->rectangles) {
                    output << quad.left << ' ' << quad.top << ' '
                           << quad.right << ' ' << quad.top << ' '
                           << quad.left << ' ' << quad.bottom << ' '
                           << quad.right << ' ' << quad.bottom << ' ';
                }
            }
            output << "]\n"
                   << "/C [" << options.color.red << ' ' << options.color.green << ' ' << options.color.blue << "]\n"
                   << "/CA " << std::clamp(options.opacity, 0.0, 1.0) << "\n"
                   << "/F 4 /Contents (Matched: " << escapeLiteral(options.keyword) << ") >>\n"
                   << "endobj\n";
        }
    }

    for (const auto& [pageIndex, references] : annotationReferences) {
        const PdfReference pageReference = document.GetPageReference(pageIndex);
        PdfDictionary pageDictionary = copyPageDictionary(document, pageReference);
        PdfArray annotations = collectExistingAnnotations(document, pageDictionary);
        for (const auto& reference : references) {
            annotations.push_back(PdfObject::IndirectReference(reference.objectNumber, reference.generation));
        }
        pageDictionary.Put(PdfName("Annots"), PdfObject(std::move(annotations)));

        const std::uint64_t offset = static_cast<std::uint64_t>(output.tellp());
        xrefEntries[pageReference.objectNumber] = {offset, pageReference.generation};
        output << pageReference.objectNumber << ' ' << pageReference.generation << " obj\n";
        serializeDictionary(output, pageDictionary);
        output << "\nendobj\n";
    }

    const std::uint64_t xrefOffset = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n";
    auto iterator = xrefEntries.begin();
    while (iterator != xrefEntries.end()) {
        const std::uint32_t first = iterator->first;
        auto end = iterator;
        std::uint32_t expected = first;
        while (end != xrefEntries.end() && end->first == expected) {
            ++end;
            ++expected;
        }
        output << first << ' ' << (expected - first) << '\n';
        for (auto current = iterator; current != end; ++current) {
            writeXrefEntry(output, current->second.first, current->second.second);
        }
        iterator = end;
    }

    PdfDictionary trailer = parseTrailerDictionary(document);
    trailer.Put(PdfName("Size"), PdfObject(static_cast<std::int64_t>(newObjectNumber)));
    trailer.Put(PdfName("Prev"), PdfObject(static_cast<std::int64_t>(document.GetStartXrefOffset())));
    trailer.Remove(PdfName("XRefStm"));

    output << "trailer\n";
    serializeDictionary(output, trailer);
    output << "\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    return result;
}

} // namespace CPPPdf

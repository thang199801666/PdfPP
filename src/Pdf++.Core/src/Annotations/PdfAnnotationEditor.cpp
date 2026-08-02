#include <CPPPdf/Annotations/PdfAnnotationEditor.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Writer/PdfIncrementalWriter.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>

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
        if (regular) output << static_cast<char>(ch);
        else output << '#' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(ch) << std::dec;
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
    case PdfObjectType::Null: output << "null"; break;
    case PdfObjectType::Boolean: output << (*object.AsBoolean() ? "true" : "false"); break;
    case PdfObjectType::Integer: output << *object.AsInteger(); break;
    case PdfObjectType::Real: output << std::setprecision(12) << *object.AsReal(); break;
    case PdfObjectType::Name: output << escapeName(object.AsName()->value()); break;
    case PdfObjectType::String: output << '(' << escapeLiteral(*object.AsString()) << ')'; break;
    case PdfObjectType::Array: serializeArray(output, *object.AsArray()); break;
    case PdfObjectType::Dictionary: serializeDictionary(output, *object.AsDictionary()); break;
    case PdfObjectType::Stream: serializeDictionary(output, object.AsStream()->dictionary()); break;
    case PdfObjectType::IndirectReference: {
        const auto reference = object.AsReference();
        output << reference->first << ' ' << reference->second << " R";
        break;
    }
    }
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open input PDF: " + path.string());
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
    if (!dictionary) throw PdfException(PdfErrorCode::MalformedXref, "Trailer is not a PDF dictionary.");
    return *dictionary;
}

PdfDictionary copyPageDictionary(const PdfDocument& document, const PdfReference& pageReference) {
    const PdfObject& pageObject = document.GetObject(pageReference);
    const PdfDictionary* dictionary = pageObject.AsDictionary();
    if (!dictionary) throw PdfException(PdfErrorCode::MalformedObject, "Page object is not a dictionary.");
    return *dictionary;
}

PdfArray collectExistingAnnotations(const PdfDocument& document, const PdfDictionary& pageDictionary) {
    PdfArray result;
    const PdfObject* annots = pageDictionary.Find(PdfName("Annots"));
    if (!annots) return result;
    if (const PdfArray* direct = annots->AsArray()) {
        for (const auto& value : direct->values()) result.push_back(value);
    } else if (const auto reference = annots->AsReference()) {
        const PdfObject& resolved = document.GetObject(PdfReference{reference->first, reference->second});
        if (const PdfArray* array = resolved.AsArray()) {
            for (const auto& value : array->values()) result.push_back(value);
        }
    }
    return result;
}

void writeXrefEntry(std::ostream& output, std::uint64_t offset, std::uint16_t generation) {
    output << std::setw(10) << std::setfill('0') << offset << ' '
           << std::setw(5) << std::setfill('0') << generation << " n \n";
}

const char* subtypeName(PdfAnnotationType type) {
    switch (type) {
    case PdfAnnotationType::Highlight: return "Highlight";
    case PdfAnnotationType::Underline: return "Underline";
    case PdfAnnotationType::StrikeOut: return "StrikeOut";
    case PdfAnnotationType::TextNote: return "Text";
    case PdfAnnotationType::Link: return "Link";
    }
    return "Text";
}

void writeQuadPoints(std::ostream& output, const PdfAnnotation& annotation) {
    if (annotation.type != PdfAnnotationType::Highlight &&
        annotation.type != PdfAnnotationType::Underline &&
        annotation.type != PdfAnnotationType::StrikeOut) return;
    output << "/QuadPoints [";
    const auto writeQuad = [&](const PdfRectangle& q) {
        output << q.left << ' ' << q.top << ' '
               << q.right << ' ' << q.top << ' '
               << q.left << ' ' << q.bottom << ' '
               << q.right << ' ' << q.bottom << ' ';
    };
    if (annotation.quadrilaterals.empty()) writeQuad(annotation.rectangle);
    else for (const auto& quad : annotation.quadrilaterals) writeQuad(quad);
    output << "]\n";
}

void writeAnnotation(std::ostream& output, const PdfAnnotation& annotation) {
    const PdfRectangle& r = annotation.rectangle;
    output << "<< /Type /Annot /Subtype /" << subtypeName(annotation.type) << "\n"
           << "/Rect [" << r.left << ' ' << r.bottom << ' ' << r.right << ' ' << r.top << "]\n";
    writeQuadPoints(output, annotation);
    output << "/C [" << annotation.color.red << ' ' << annotation.color.green << ' ' << annotation.color.blue << "]\n"
           << "/CA " << std::clamp(annotation.opacity, 0.0, 1.0) << "\n";
    if (!annotation.contents.empty()) output << "/Contents (" << escapeLiteral(annotation.contents) << ")\n";
    if (!annotation.title.empty()) output << "/T (" << escapeLiteral(annotation.title) << ")\n";
    if (annotation.type == PdfAnnotationType::TextNote) {
        output << "/Open " << (annotation.open ? "true" : "false") << "\n/Name /Comment\n";
    }
    if (annotation.type == PdfAnnotationType::Link) {
        if (annotation.uri.empty()) throw PdfException(PdfErrorCode::InvalidArgument, "Link annotation URI cannot be empty.");
        output << "/Border [0 0 0]\n/A << /S /URI /URI (" << escapeLiteral(annotation.uri) << ") >>\n";
    } else {
        output << "/F 4\n";
    }
    output << ">>\n";
}

} // namespace

PdfAnnotationEditResult PdfAnnotationEditor::AddAnnotations(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<PdfAnnotation>& annotations,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (document.IsEncrypted() && !document.IsOwnerPasswordAuthenticated() &&
        (static_cast<std::uint32_t>(document.GetPermissionBits()) & 32U) == 0U) {
        throw PdfException(PdfErrorCode::PermissionDenied,
                           "The user password does not permit annotation modification.");
    }

    PdfAnnotationEditResult result{outputPath, annotations.size(), 0U};
    if (annotations.empty()) {
        const std::string original = readFile(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
        return result;
    }
    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);

    std::map<std::size_t, std::vector<const PdfAnnotation*>> byPage;
    for (const auto& annotation : annotations) {
        if (annotation.pageIndex >= document.GetPageCount()) {
            throw PdfException(PdfErrorCode::InvalidArgument, "Annotation page index is out of range.");
        }
        byPage[annotation.pageIndex].push_back(&annotation);
    }
    result.modifiedPageCount = byPage.size();

    std::uint32_t newObjectNumber = Internal::PdfIncrementalWriter::NextObjectNumber(document);
    std::unordered_map<std::size_t, std::vector<PdfReference>> annotationReferences;

    for (const auto& [pageIndex, pageAnnotations] : byPage) {
        auto& references = annotationReferences[pageIndex];
        for (const PdfAnnotation* annotation : pageAnnotations) {
            const std::uint32_t objectNumber = newObjectNumber++;
            references.push_back(PdfReference{objectNumber, 0U});
            std::ostringstream body;
            writeAnnotation(body, *annotation);
            writer.WriteRawObject(PdfReference{objectNumber, 0U}, body.str());
        }
    }

    for (const auto& [pageIndex, references] : annotationReferences) {
        const PdfReference pageReference = document.GetPageReference(pageIndex);
        PdfDictionary pageDictionary = copyPageDictionary(document, pageReference);
        PdfArray annots = collectExistingAnnotations(document, pageDictionary);
        for (const auto& reference : references) {
            annots.push_back(PdfObject::IndirectReference(reference.objectNumber, reference.generation));
        }
        pageDictionary.Put(PdfName("Annots"), PdfObject(std::move(annots)));
        writer.WriteDictionary(pageReference, pageDictionary);
    }
    writer.Finish(newObjectNumber);
    return result;
}

} // namespace CPPPdf

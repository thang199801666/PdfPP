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
    case PdfAnnotationType::FreeText: return "FreeText";
    case PdfAnnotationType::Ink: return "Ink";
    case PdfAnnotationType::Polygon: return "Polygon";
    case PdfAnnotationType::Polyline: return "Polyline";
    case PdfAnnotationType::Square: return "Square";
    case PdfAnnotationType::Circle: return "Circle";
    case PdfAnnotationType::Stamp: return "Stamp";
    }
    return "Text";
}

const char* lineEndStyleName(PdfLineEndStyle style) {
    switch (style) {
    case PdfLineEndStyle::None: return "None";
    case PdfLineEndStyle::Square: return "Square";
    case PdfLineEndStyle::Circle: return "Circle";
    case PdfLineEndStyle::Diamond: return "Diamond";
    case PdfLineEndStyle::OpenArrow: return "OpenArrow";
    case PdfLineEndStyle::ClosedArrow: return "ClosedArrow";
    case PdfLineEndStyle::Butt: return "Butt";
    case PdfLineEndStyle::ROpenArrow: return "ROpenArrow";
    case PdfLineEndStyle::RClosedArrow: return "RClosedArrow";
    case PdfLineEndStyle::Slash: return "Slash";
    }
    return "None";
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

void writeVertices(std::ostream& output, const PdfAnnotation& annotation) {
    const std::vector<PdfPoint>* points = nullptr;
    if (annotation.type == PdfAnnotationType::Ink && !annotation.inkPaths.empty()) {
        output << "/InkList [";
        for (const auto& stroke : annotation.inkPaths) {
            output << '[';
            for (const auto& point : stroke) {
                output << point.x << ' ' << point.y << ' ';
            }
            output << ']';
        }
        output << "]\n";
        return;
    }
    if (annotation.type == PdfAnnotationType::Polygon ||
        annotation.type == PdfAnnotationType::Polyline) {
        points = &annotation.vertices;
    }
    if (!points || points->empty()) return;
    output << "/Vertices [";
    for (const auto& point : *points) {
        output << point.x << ' ' << point.y << ' ';
    }
    output << "]\n";
}

void writeAnnotation(std::ostream& output, const PdfAnnotation& annotation) {
    const PdfRectangle& r = annotation.rectangle;
    output << "<< /Type /Annot /Subtype /" << subtypeName(annotation.type) << "\n"
           << "/Rect [" << r.left << ' ' << r.bottom << ' ' << r.right << ' ' << r.top << "]\n";
    writeQuadPoints(output, annotation);
    writeVertices(output, annotation);
    output << "/C [" << annotation.color.red << ' ' << annotation.color.green << ' ' << annotation.color.blue << "]\n";
    if (annotation.interiorColor.red != 0.0 || annotation.interiorColor.green != 0.0 ||
        annotation.interiorColor.blue != 0.0) {
        output << "/IC [" << annotation.interiorColor.red << ' ' << annotation.interiorColor.green
               << ' ' << annotation.interiorColor.blue << "]\n";
    }
    output << "/CA " << std::clamp(annotation.opacity, 0.0, 1.0) << "\n";
    if (!annotation.contents.empty()) output << "/Contents (" << escapeLiteral(annotation.contents) << ")\n";
    if (!annotation.title.empty()) output << "/T (" << escapeLiteral(annotation.title) << ")\n";
    if (annotation.type == PdfAnnotationType::TextNote) {
        output << "/Open " << (annotation.open ? "true" : "false") << "\n/Name /Comment\n";
    }
    if (annotation.borderWidth > 0.0) {
        output << "/Border [0 0 " << annotation.borderWidth << "]\n";
    }
    const bool hasLineEnds = annotation.lineStart != PdfLineEndStyle::None ||
                             annotation.lineEnd != PdfLineEndStyle::None;
    if (annotation.type == PdfAnnotationType::Polygon && hasLineEnds) {
        output << "/LE [/" << lineEndStyleName(annotation.lineStart) << " /"
               << lineEndStyleName(annotation.lineEnd) << "]\n";
    }
    if (annotation.rotationDegrees != 0.0) {
        output << "/Rotate " << static_cast<int>(annotation.rotationDegrees) << '\n';
    }
    if (annotation.type == PdfAnnotationType::FreeText) {
        output << "/DA (/Helv " << std::max(annotation.borderWidth, 10.0)
               << " Tf 0 g)\n/Q " << annotation.textAlignment << "\n";
    }
    if (annotation.type == PdfAnnotationType::Stamp) {
        const std::string stamp = annotation.stampName.empty() ? "Approved" : annotation.stampName;
        output << "/Name /" << stamp << "\n";
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

PdfArray collectPageAnnotationObjects(const PdfDocument& document, const PdfDictionary& pageDictionary) {
    return collectExistingAnnotations(document, pageDictionary);
}

std::string annotationSubtypeFromObject(const PdfObject& object) {
    const PdfDictionary* dictionary = object.AsDictionary();
    if (!dictionary) return {};
    const PdfObject* subtype = dictionary->Find(PdfName("Subtype"));
    const PdfName* name = subtype ? subtype->AsName() : nullptr;
    return name ? name->value() : std::string{};
}

PdfAnnotationRemovalResult PdfAnnotationEditor::RemoveAnnotations(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::size_t pageIndex,
    const std::vector<std::string>& typeFilter,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (pageIndex >= document.GetPageCount()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Annotation page index is out of range.");
    }
    if (document.IsEncrypted() && !document.IsOwnerPasswordAuthenticated() &&
        (static_cast<std::uint32_t>(document.GetPermissionBits()) & 32U) == 0U) {
        throw PdfException(PdfErrorCode::PermissionDenied,
                           "The user password does not permit annotation modification.");
    }

    const PdfReference pageReference = document.GetPageReference(pageIndex);
    const PdfDictionary pageDictionary = copyPageDictionary(document, pageReference);
    PdfArray annots = collectPageAnnotationObjects(document, pageDictionary);
    if (annots.empty()) {
        const std::string original = readFile(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
        return {outputPath, 0U, 0U};
    }

    std::vector<PdfObject> remaining;
    std::size_t removed = 0U;
    const auto matchesFilter = [&typeFilter](const std::string& subtype) {
        for (const auto& filter : typeFilter) {
            std::string normalized = filter;
            if (!normalized.empty() && normalized.front() == '/') normalized.erase(0U, 1U);
            if (subtype == normalized) return true;
        }
        return false;
    };
    for (const auto& value : annots.values()) {
        if (const auto reference = value.AsReference()) {
            const PdfObject& resolved = document.GetObject(PdfReference{reference->first, reference->second});
            const std::string subtype = annotationSubtypeFromObject(resolved);
            if (typeFilter.empty() || matchesFilter(subtype)) ++removed;
            else remaining.push_back(value);
        } else if (const PdfDictionary* direct = value.AsDictionary()) {
            const PdfObject* subtypeObject = direct->Find(PdfName("Subtype"));
            const PdfName* subtype = subtypeObject ? subtypeObject->AsName() : nullptr;
            if (typeFilter.empty() || (subtype && matchesFilter(subtype->value()))) ++removed;
            else remaining.push_back(value);
        } else {
            remaining.push_back(value);
        }
    }

    PdfAnnotationRemovalResult result{outputPath, removed, 0U};
    if (removed == 0U) {
        const std::string original = readFile(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
        return result;
    }
    result.modifiedPageCount = 1U;

    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    PdfDictionary updatedPage = pageDictionary;
    PdfArray kept;
    for (const auto& value : remaining) kept.push_back(value);
    updatedPage.Put(PdfName("Annots"), PdfObject(std::move(kept)));
    writer.WriteDictionary(pageReference, updatedPage);
    writer.Finish(Internal::PdfIncrementalWriter::NextObjectNumber(document));
    return result;
}

std::size_t PdfAnnotationEditor::UpdateAnnotationContents(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::size_t pageIndex,
    const PdfAnnotationType annotationType,
    const std::string& newContents,
    const std::string& newTitle,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (pageIndex >= document.GetPageCount()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Annotation page index is out of range.");
    }
    if (document.IsEncrypted() && !document.IsOwnerPasswordAuthenticated() &&
        (static_cast<std::uint32_t>(document.GetPermissionBits()) & 32U) == 0U) {
        throw PdfException(PdfErrorCode::PermissionDenied,
                           "The user password does not permit annotation modification.");
    }

    const PdfReference pageReference = document.GetPageReference(pageIndex);
    const PdfDictionary pageDictionary = copyPageDictionary(document, pageReference);
    const PdfArray annots = collectPageAnnotationObjects(document, pageDictionary);
    const std::string targetSubtype = subtypeName(annotationType);
    std::size_t updated = 0U;

    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t nextObjectNumber = Internal::PdfIncrementalWriter::NextObjectNumber(document);
    for (const auto& value : annots.values()) {
        if (!value.AsReference()) continue;
        const auto reference = value.AsReference();
        const PdfObject& resolved = document.GetObject(PdfReference{reference->first, reference->second});
        const PdfDictionary* dictionary = resolved.AsDictionary();
        if (!dictionary) continue;
        const PdfObject* subtypeObject = dictionary->Find(PdfName("Subtype"));
        const PdfName* subtype = subtypeObject ? subtypeObject->AsName() : nullptr;
        if (!subtype || subtype->value() != targetSubtype) continue;
        PdfDictionary updatedDictionary = *dictionary;
        updatedDictionary.Put(PdfName("Contents"), PdfObject(std::string(newContents)));
        if (!newTitle.empty()) updatedDictionary.Put(PdfName("T"), PdfObject(std::string(newTitle)));
        writer.WriteDictionary(PdfReference{reference->first, reference->second}, updatedDictionary);
        ++updated;
    }
    if (updated > 0U) writer.Finish(nextObjectNumber);
    else {
        const std::string original = readFile(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
    }
    return updated;
}

std::string annotationAppearanceCommands(const PdfDictionary& annotation) {
    const PdfObject* subtypeObject = annotation.Find(PdfName("Subtype"));
    const PdfName* subtype = subtypeObject ? subtypeObject->AsName() : nullptr;
    if (!subtype) return {};
    const std::string_view type = subtype->value();
    const PdfObject* rectObject = annotation.Find(PdfName("Rect"));
    const PdfArray* rect = rectObject ? rectObject->AsArray() : nullptr;
    if (!rect || rect->size() < 4U) return {};
    const double x = rect->at(0U).AsReal().value_or(0.0);
    const double y = rect->at(1U).AsReal().value_or(0.0);
    const double right = rect->at(2U).AsReal().value_or(0.0);
    const double top = rect->at(3U).AsReal().value_or(0.0);
    const double width = std::max(1.0, right - x);
    const double height = std::max(1.0, top - y);
    const auto colorOf = [&annotation](const char* key) {
        const PdfObject* object = annotation.Find(PdfName(key));
        const PdfArray* array = object ? object->AsArray() : nullptr;
        if (!array || array->size() < 3U) return std::array<double, 3>{0.0, 0.0, 0.0};
        return std::array<double, 3>{array->at(0U).AsReal().value_or(0.0),
                                     array->at(1U).AsReal().value_or(0.0),
                                     array->at(2U).AsReal().value_or(0.0)};
    };
    const auto color = colorOf("C");
    const auto interior = colorOf("IC");
    const double borderWidth = [&]() {
        const PdfObject* border = annotation.Find(PdfName("Border"));
        const PdfArray* array = border ? border->AsArray() : nullptr;
        if (array && array->size() >= 3U) return array->at(2U).AsReal().value_or(0.0);
        return 1.0;
    }();

    std::ostringstream stream;
    stream << "q\n";
    const auto setStroke = [&stream, &color](const double width) {
        stream << color[0] << ' ' << color[1] << ' ' << color[2] << " RG " << width << " w\n";
    };
    const auto setFill = [&stream, &interior](const char* operands) {
        stream << interior[0] << ' ' << interior[1] << ' ' << interior[2] << ' ' << operands;
    };

    if (type == "Square" || type == "Circle") {
        setStroke(std::max(borderWidth, 0.5));
        if (type == "Square") {
            stream << x << ' ' << y << ' ' << width << ' ' << height << " re\n";
        } else {
            const double cx = x + width * 0.5;
            const double cy = y + height * 0.5;
            const double rx = width * 0.5;
            const double ry = height * 0.5;
            stream << cx << ' ' << cy << ' ' << rx << ' ' << ry << " 0 360 arc\n";
        }
        const PdfObject* interiorObject = annotation.Find(PdfName("IC"));
        if (interiorObject && interiorObject->AsArray()) {
            setFill("f\n");
        }
        stream << "S\n";
    } else if (type == "Polygon" || type == "Polyline") {
        setStroke(std::max(borderWidth, 0.5));
        const PdfObject* verticesObject = annotation.Find(PdfName("Vertices"));
        const PdfArray* vertices = verticesObject ? verticesObject->AsArray() : nullptr;
        if (vertices && vertices->size() >= 2U) {
            stream << vertices->at(0U).AsReal().value_or(0.0) << ' '
                   << vertices->at(1U).AsReal().value_or(0.0) << " m\n";
            for (std::size_t i = 2U; i + 1U < vertices->size(); i += 2U) {
                stream << vertices->at(i).AsReal().value_or(0.0) << ' '
                       << vertices->at(i + 1U).AsReal().value_or(0.0) << " l\n";
            }
            if (type == "Polygon") {
                const PdfObject* interiorObject = annotation.Find(PdfName("IC"));
                if (interiorObject && interiorObject->AsArray()) setFill("f\n");
            }
            stream << "S\n";
        }
    } else if (type == "Ink") {
        setStroke(std::max(borderWidth, 0.5));
        const PdfObject* inkObject = annotation.Find(PdfName("InkList"));
        const PdfArray* inkList = inkObject ? inkObject->AsArray() : nullptr;
        if (inkList) {
            for (const auto& strokeValue : inkList->values()) {
                const PdfArray* stroke = strokeValue.AsArray();
                if (!stroke || stroke->size() < 2U) continue;
                stream << stroke->at(0U).AsReal().value_or(0.0) << ' '
                       << stroke->at(1U).AsReal().value_or(0.0) << " m\n";
                for (std::size_t i = 2U; i + 1U < stroke->size(); i += 2U) {
                    stream << stroke->at(i).AsReal().value_or(0.0) << ' '
                           << stroke->at(i + 1U).AsReal().value_or(0.0) << " l\n";
                }
                stream << "S\n";
            }
        }
    } else if (type == "FreeText") {
        stream << "0.98 g " << x << ' ' << y << ' ' << width << ' ' << height << " re f\n";
        setStroke(0.5);
        stream << x << ' ' << y << ' ' << width << ' ' << height << " re S\n";
        const PdfObject* contentsObject = annotation.Find(PdfName("Contents"));
        const std::string* contents = contentsObject ? contentsObject->AsString() : nullptr;
        if (contents && !contents->empty()) {
            const double fontSize = std::max(1.0, std::min(height * 0.6, width * 0.08));
            stream << "BT /PPAnnotFont " << fontSize << " Tf 0 g "
                   << (x + fontSize * 0.5) << ' ' << (y + height * 0.5 - fontSize * 0.5) << " Td ("
                   << escapeLiteral(*contents) << ") Tj ET\n";
        }
    } else if (type == "Stamp" || type == "Text" || type == "Link") {
        stream << "0.96 g " << x << ' ' << y << ' ' << width << ' ' << height << " re f\n";
        setStroke(0.75);
        stream << x << ' ' << y << ' ' << width << ' ' << height << " re S\n";
        const PdfObject* contentsObject = annotation.Find(PdfName("Contents"));
        const std::string* contents = contentsObject ? contentsObject->AsString() : nullptr;
        if (contents && !contents->empty()) {
            const double fontSize = std::max(1.0, std::min(height * 0.5, width * 0.07));
            stream << "BT /PPAnnotFont " << fontSize << " Tf 0 g "
                   << (x + fontSize * 0.5) << ' ' << (y + height * 0.5 - fontSize * 0.5) << " Td ("
                   << escapeLiteral(*contents) << ") Tj ET\n";
        }
    } else if (type == "Highlight" || type == "Underline" || type == "StrikeOut") {
        const PdfObject* quadObject = annotation.Find(PdfName("QuadPoints"));
        const PdfArray* quads = quadObject ? quadObject->AsArray() : nullptr;
        const double alpha = [&]() {
            const PdfObject* caObject = annotation.Find(PdfName("CA"));
            return caObject ? caObject->AsReal().value_or(1.0) : 1.0;
        }();
        stream << "q " << std::clamp(alpha, 0.0, 1.0) << " g\n";
        if (quads && quads->size() >= 8U) {
            const double qx = quads->at(0U).AsReal().value_or(x);
            const double qy = quads->at(1U).AsReal().value_or(y);
            const double qright = quads->at(2U).AsReal().value_or(right);
            const double qtop = quads->at(3U).AsReal().value_or(top);
            const double thickness = std::max(1.0, height * 0.12);
            if (type == "Highlight") {
                stream << qx << ' ' << qy << ' ' << (qright - qx) << ' ' << (qtop - qy) << " re f\n";
            } else {
                const double lineY = type == "Underline" ? qy + thickness * 0.5 : qy + (qtop - qy) * 0.5;
                stream << qx << ' ' << lineY << ' ' << (qright - qx) << ' ' << thickness << " re f\n";
            }
        }
        stream << "Q\n";
    }
    stream << "Q\n";
    return stream.str();
}

PdfAnnotationAppearanceResult PdfAnnotationEditor::GenerateAppearances(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::size_t pageIndex,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (pageIndex >= document.GetPageCount()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Annotation page index is out of range.");
    }
    if (document.IsEncrypted() && !document.IsOwnerPasswordAuthenticated() &&
        (static_cast<std::uint32_t>(document.GetPermissionBits()) & 32U) == 0U) {
        throw PdfException(PdfErrorCode::PermissionDenied,
                           "The user password does not permit annotation modification.");
    }

    const PdfReference pageReference = document.GetPageReference(pageIndex);
    const PdfDictionary pageDictionary = copyPageDictionary(document, pageReference);
    const PdfArray annots = collectPageAnnotationObjects(document, pageDictionary);

    PdfAnnotationAppearanceResult result{outputPath, 0U, 0U};
    if (annots.empty()) {
        const std::string original = readFile(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
        return result;
    }

    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t nextObjectNumber = Internal::PdfIncrementalWriter::NextObjectNumber(document);
    for (const auto& value : annots.values()) {
        if (!value.AsReference()) continue;
        const auto reference = value.AsReference();
        const PdfObject& resolved = document.GetObject(PdfReference{reference->first, reference->second});
        const PdfDictionary* dictionary = resolved.AsDictionary();
        if (!dictionary) continue;
        const std::string commands = annotationAppearanceCommands(*dictionary);
        if (commands.empty()) continue;
        const PdfReference appearanceReference{nextObjectNumber++, 0U};
        std::ostringstream body;
        body << "<< /Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources "
             << "<</Font << /PPAnnotFont << /Type /Font /Subtype /Type1 "
             << "/BaseFont /Helvetica >> >> >> /Length " << commands.size()
             << " >>\nstream\n" << commands << "endstream";
        writer.WriteRawObject(appearanceReference, body.str());

        // Attach the appearance to the annotation dictionary via /AP << /N ref >>.
        PdfDictionary updated = *dictionary;
        PdfDictionary ap;
        ap.Put(PdfName("N"), PdfObject::IndirectReference(appearanceReference.objectNumber,
                                                          appearanceReference.generation));
        updated.Put(PdfName("AP"), PdfObject(std::move(ap)));
        writer.WriteDictionary(PdfReference{reference->first, reference->second}, updated);
        ++result.appearanceCount;
    }

    if (result.appearanceCount == 0U) {
        const std::string original = readFile(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
        return result;
    }
    result.modifiedPageCount = 1U;
    writer.Finish(nextObjectNumber);
    return result;
}

} // namespace CPPPdf

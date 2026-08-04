#include <CPPPdf/Annotations/PdfXfdf.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace CPPPdf {
namespace {

std::string xmlEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8U);
    for (const char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

std::string xmlUnescape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '&') {
            const auto end = value.find(';', i);
            if (end == std::string_view::npos) {
                result.push_back(value[i]);
                continue;
            }
            const std::string_view entity = value.substr(i + 1, end - i - 1);
            if (entity == "amp") result.push_back('&');
            else if (entity == "lt") result.push_back('<');
            else if (entity == "gt") result.push_back('>');
            else if (entity == "quot") result.push_back('"');
            else if (entity == "apos") result.push_back('\'');
            else result.append(entity);
            i = end;
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open XFDF file: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeStringAttribute(std::ostream& output, std::string_view name, const std::string& value) {
    if (value.empty()) return;
    output << ' ' << name << "=\"" << xmlEscape(value) << '"';
}

void writeColorAttributes(std::ostream& output, const PdfAnnotationColor& color) {
    output << " color=\"#" << std::hex
           << static_cast<int>(std::clamp(color.red, 0.0, 1.0) * 255.0)
           << std::setw(2) << std::setfill('0')
           << static_cast<int>(std::clamp(color.green, 0.0, 1.0) * 255.0)
           << std::setw(2) << std::setfill('0')
           << static_cast<int>(std::clamp(color.blue, 0.0, 1.0) * 255.0)
           << std::dec << '"';
}

void writeRectAttributes(std::ostream& output, const PdfRectangle& rectangle) {
    output << " rect=\"" << rectangle.left << ',' << rectangle.bottom << ','
           << rectangle.right << ',' << rectangle.top << '"';
}

// Minimal XML scanner: extracts `name="value"` attribute pairs from a token.
std::vector<std::pair<std::string, std::string>> parseAttributes(std::string_view source) {
    std::vector<std::pair<std::string, std::string>> attributes;
    std::size_t position = 0;
    while (position < source.size()) {
        const std::size_t nameBegin = source.find_first_not_of(" \t\r\n", position);
        if (nameBegin == std::string_view::npos) break;
        std::size_t cursor = nameBegin;
        while (cursor < source.size() && source[cursor] != '=' &&
               source[cursor] != ' ' && source[cursor] != '>' &&
               source[cursor] != '/' && source[cursor] != '\t' &&
               source[cursor] != '\r' && source[cursor] != '\n') {
            ++cursor;
        }
        if (cursor >= source.size() || source[cursor] != '=') {
            position = cursor + 1;
            continue;
        }
        const std::string name(source.substr(nameBegin, cursor - nameBegin));
        position = cursor + 1;
        while (position < source.size() &&
               (source[position] == ' ' || source[position] == '\t')) ++position;
        if (position >= source.size() || (source[position] != '"' && source[position] != '\'')) continue;
        const char quote = source[position++];
        const std::size_t valueBegin = position;
        while (position < source.size() && source[position] != quote) ++position;
        if (position >= source.size()) break;
        const std::string value(xmlUnescape(source.substr(valueBegin, position - valueBegin)));
        attributes.emplace_back(name, value);
        ++position;
    }
    return attributes;
}

std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes,
                        std::string_view name) {
    for (const auto& [key, value] : attributes) {
        if (key == name) return value;
    }
    return {};
}

std::vector<double> parseNumberList(std::string_view text) {
    std::vector<double> numbers;
    std::string normalized;
    normalized.reserve(text.size());
    for (const char ch : text) {
        if (ch == ',' || ch == ';') normalized.push_back(' ');
        else normalized.push_back(ch);
    }
    std::istringstream stream(normalized);
    double value{};
    while (stream >> value) numbers.push_back(value);
    return numbers;
}

PdfAnnotationColor parseHexColor(std::string_view text) {
    PdfAnnotationColor color;
    if (text.size() >= 6U) {
        const auto hex = [](const char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        color.red = static_cast<double>(hex(text[0]) * 16 + hex(text[1])) / 255.0;
        color.green = static_cast<double>(hex(text[2]) * 16 + hex(text[3])) / 255.0;
        color.blue = static_cast<double>(hex(text[4]) * 16 + hex(text[5])) / 255.0;
    }
    return color;
}

std::string annotationSubtypeToElement(PdfAnnotationType type) {
    switch (type) {
    case PdfAnnotationType::Highlight: return "highlight";
    case PdfAnnotationType::Underline: return "underline";
    case PdfAnnotationType::StrikeOut: return "strikeout";
    case PdfAnnotationType::TextNote: return "text";
    case PdfAnnotationType::Link: return "link";
    case PdfAnnotationType::Line: return "line";
    case PdfAnnotationType::FileAttachment: return "fileattachment";
    case PdfAnnotationType::FreeText: return "freetext";
    case PdfAnnotationType::Polygon: return "polygon";
    case PdfAnnotationType::Polyline: return "polyline";
    case PdfAnnotationType::Square: return "square";
    case PdfAnnotationType::Circle: return "circle";
    case PdfAnnotationType::Stamp: return "stamp";
    case PdfAnnotationType::Ink: return "ink";
    }
    return "text";
}

PdfAnnotationType elementToAnnotationType(std::string_view element) {
    if (element == "highlight") return PdfAnnotationType::Highlight;
    if (element == "underline") return PdfAnnotationType::Underline;
    if (element == "strikeout") return PdfAnnotationType::StrikeOut;
    if (element == "link") return PdfAnnotationType::Link;
    if (element == "line") return PdfAnnotationType::Line;
    if (element == "fileattachment") return PdfAnnotationType::FileAttachment;
    if (element == "freetext") return PdfAnnotationType::FreeText;
    if (element == "polygon") return PdfAnnotationType::Polygon;
    if (element == "polyline") return PdfAnnotationType::Polyline;
    if (element == "square") return PdfAnnotationType::Square;
    if (element == "circle") return PdfAnnotationType::Circle;
    if (element == "stamp") return PdfAnnotationType::Stamp;
    if (element == "ink") return PdfAnnotationType::Ink;
    return PdfAnnotationType::TextNote;
}

} // namespace

PdfXfdf::XfdfExportResult PdfXfdf::ExportAnnotations(
    const std::filesystem::path& pdfPath,
    const std::size_t pageIndex,
    const std::filesystem::path& xfdfPath,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(pdfPath, readerOptions);
    if (pageIndex >= document.GetPageCount()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "XFDF export page index is out of range.");
    }
    const PdfReference pageReference = document.GetPageReference(pageIndex);
    const PdfObject& pageObject = document.GetObject(pageReference);
    const PdfDictionary* pageDictionary = pageObject.AsDictionary();
    if (!pageDictionary) {
        throw PdfException(PdfErrorCode::MalformedObject, "Page object is not a dictionary.");
    }
    const PdfObject* annotsObject = pageDictionary->Find(PdfName("Annots"));
    std::vector<const PdfDictionary*> annotationDictionaries;
    if (annotsObject) {
        if (const PdfArray* array = annotsObject->AsArray()) {
            for (const auto& value : array->values()) {
                if (const auto reference = value.AsReference()) {
                    const PdfObject& resolved = document.GetObject(
                        PdfReference{reference->first, reference->second});
                    if (const PdfDictionary* dictionary = resolved.AsDictionary()) {
                        annotationDictionaries.push_back(dictionary);
                    }
                } else if (const PdfDictionary* direct = value.AsDictionary()) {
                    annotationDictionaries.push_back(direct);
                }
            }
        } else if (const auto reference = annotsObject->AsReference()) {
            const PdfObject& resolved = document.GetObject(
                PdfReference{reference->first, reference->second});
            if (const PdfArray* resolvedArray = resolved.AsArray()) {
                for (const auto& value : resolvedArray->values()) {
                    if (const auto itemReference = value.AsReference()) {
                        const PdfObject& item = document.GetObject(
                            PdfReference{itemReference->first, itemReference->second});
                        if (const PdfDictionary* dictionary = item.AsDictionary()) {
                            annotationDictionaries.push_back(dictionary);
                        }
                    }
                }
            }
        }
    }

    std::ofstream output(xfdfPath, std::ios::binary | std::ios::trunc);
    if (!output) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open XFDF output: " + xfdfPath.string());
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<xfdf xmlns=\"http://ns.adobe.com/xfdf/\" xml:space=\"preserve\">\n"
           << " <annots>\n";
    std::size_t exported = 0U;
    for (const PdfDictionary* dictionary : annotationDictionaries) {
        const PdfObject* subtypeObject = dictionary->Find(PdfName("Subtype"));
        const PdfName* subtype = subtypeObject ? subtypeObject->AsName() : nullptr;
        if (!subtype) continue;
        const std::string_view subtypeName = subtype->value();
        const std::string element = [&]() {
            if (subtypeName == "Highlight") return std::string("highlight");
            if (subtypeName == "Underline") return std::string("underline");
            if (subtypeName == "StrikeOut") return std::string("strikeout");
            if (subtypeName == "Link") return std::string("link");
            if (subtypeName == "Line") return std::string("line");
            if (subtypeName == "FileAttachment") return std::string("fileattachment");
            if (subtypeName == "FreeText") return std::string("freetext");
            if (subtypeName == "Polygon") return std::string("polygon");
            if (subtypeName == "Polyline") return std::string("polyline");
            if (subtypeName == "Square") return std::string("square");
            if (subtypeName == "Circle") return std::string("circle");
            if (subtypeName == "Stamp") return std::string("stamp");
            if (subtypeName == "Ink") return std::string("ink");
            if (subtypeName == "Text") return std::string("text");
            return std::string{};
        }();
        if (element.empty()) continue;

        output << "  <" << element;
        const auto rect = dictionary->GetAsArray(PdfName("Rect"));
        if (rect && rect->size() >= 4U) {
            const auto x = [&](const std::size_t index) { return rect->at(index).AsReal().value_or(0.0); };
            output << " rect=\"" << x(0) << ',' << x(1) << ',' << x(2) << ',' << x(3) << '"';
        }
        const PdfObject* colorObject = dictionary->Find(PdfName("C"));
        if (const PdfArray* color = colorObject ? colorObject->AsArray() : nullptr) {
            if (color->size() >= 3U) {
                output << " color=\"#";
                for (std::size_t index = 0U; index < 3U; ++index) {
                    const double channel = color->at(index).AsReal().value_or(0.0);
                    output << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<int>(std::clamp(channel, 0.0, 1.0) * 255.0) << std::dec;
                }
                output << '"';
            }
        }
        const PdfObject* titleObject = dictionary->Find(PdfName("T"));
        if (const std::string* title = titleObject ? titleObject->AsString() : nullptr) {
            writeStringAttribute(output, "title", *title);
        }
        if (element == "link") {
            const PdfObject* action = dictionary->Find(PdfName("A"));
            if (const PdfDictionary* actionDictionary = action ? action->AsDictionary() : nullptr) {
                const PdfObject* uriObject = actionDictionary->Find(PdfName("URI"));
                if (const std::string* uri = uriObject ? uriObject->AsString() : nullptr) {
                    writeStringAttribute(output, "href", *uri);
                }
            }
        }
        if (element == "stamp") {
            const PdfObject* nameObject = dictionary->Find(PdfName("Name"));
            if (const PdfName* name = nameObject ? nameObject->AsName() : nullptr) {
                writeStringAttribute(output, "name", name->value());
            }
        }
        if (element == "fileattachment") {
            const PdfObject* fileSpecObject = dictionary->Find(PdfName("FS"));
            const PdfDictionary* fileSpec = fileSpecObject ? fileSpecObject->AsDictionary() : nullptr;
            if (!fileSpec && fileSpecObject && fileSpecObject->AsReference()) {
                const auto reference = fileSpecObject->AsReference();
                fileSpec = document.GetObject(PdfReference{reference->first, reference->second}).AsDictionary();
            }
            const PdfObject* fileNameObject = fileSpec ? fileSpec->Find(PdfName("F")) : nullptr;
            if (const std::string* fileName = fileNameObject ? fileNameObject->AsString() : nullptr) {
                writeStringAttribute(output, "name", *fileName);
            }
        }
        output << ">\n";
        const PdfObject* contentsObject = dictionary->Find(PdfName("Contents"));
        if (const std::string* contents = contentsObject ? contentsObject->AsString() : nullptr) {
            output << "   <contents>" << xmlEscape(*contents) << "</contents>\n";
        }
        output << "  </" << element << ">\n";
        ++exported;
    }
    output << " </annots>\n</xfdf>\n";
    return {xfdfPath, exported};
}

PdfXfdf::XfdfImportResult PdfXfdf::ImportAnnotations(
    const std::filesystem::path& pdfPath,
    const std::size_t pageIndex,
    const std::filesystem::path& xfdfPath,
    const std::filesystem::path& outputPath,
    const PdfReaderOptions& readerOptions) {
    const std::string xml = readFile(xfdfPath);
    std::vector<PdfAnnotation> annotations;

    const auto openTag = [&](std::size_t position, std::string_view name) -> std::size_t {
        const std::string open = "<" + std::string(name);
        const auto found = xml.find(open, position);
        if (found == std::string::npos) return std::string::npos;
        if (found + open.size() < xml.size() && xml[found + open.size()] == ' ') return found;
        return std::string::npos;
    };

    std::size_t position = 0U;
    while (position < xml.size()) {
        const auto lt = xml.find('<', position);
        if (lt == std::string::npos) break;
        const auto gt = xml.find('>', lt);
        if (gt == std::string::npos) break;
        std::string_view token(xml.data() + lt + 1, gt - lt - 1);
        if (!token.empty() && token.front() == '/') {
            position = gt + 1;
            continue;
        }
        std::string_view elementName = token;
        if (const auto space = token.find_first_of(" \t\r\n"); space != std::string_view::npos) {
            elementName = token.substr(0, space);
        }
        const bool isSelfClosing = !token.empty() && token.back() == '/';
        if (elementName == "text" || elementName == "highlight" || elementName == "underline" ||
            elementName == "strikeout" || elementName == "link" || elementName == "freetext" ||
            elementName == "line" || elementName == "fileattachment" || elementName == "polygon" || elementName == "polyline" || elementName == "square" ||
            elementName == "circle" || elementName == "stamp" || elementName == "ink") {
            const auto attributes = parseAttributes(token);
            PdfAnnotation annotation;
            annotation.pageIndex = pageIndex;
            annotation.type = elementToAnnotationType(elementName);
            const std::string rect = attributeOf(attributes, "rect");
            const auto numbers = parseNumberList(rect);
            if (numbers.size() >= 4U) {
                annotation.rectangle = PdfRectangle{numbers[0], numbers[1], numbers[2], numbers[3]};
            }
            const std::string color = attributeOf(attributes, "color");
            if (!color.empty()) annotation.color = parseHexColor(color);
            annotation.title = attributeOf(attributes, "title");
            annotation.uri = attributeOf(attributes, "href");
            annotation.stampName = attributeOf(attributes, "name");
            annotation.attachmentName = attributeOf(attributes, "name");
            if (annotation.type == PdfAnnotationType::Link && annotation.uri.empty()) {
                position = gt + 1;
                continue;
            }
            // Contents from the nested <contents> element if present.
            const std::string contentsOpen = "<contents>";
            const auto contentsStart = xml.find(contentsOpen, gt);
            if (contentsStart != std::string::npos) {
                const std::size_t contentsEnd = xml.find("</contents>", contentsStart);
                if (contentsEnd != std::string::npos) {
                    annotation.contents = xmlUnescape(std::string_view(xml.data() + contentsStart + contentsOpen.size(),
                                                                      contentsEnd - contentsStart - contentsOpen.size()));
                }
            }
            if (annotation.rectangle.empty()) {
                position = gt + 1;
                continue;
            }
            annotations.push_back(std::move(annotation));
        }
        position = gt + 1;
        if (isSelfClosing) continue;
    }

    const PdfAnnotationEditResult result = PdfAnnotationEditor::AddAnnotations(
        pdfPath, outputPath, annotations, readerOptions);
    return {outputPath, result.annotationCount, pageIndex};
}

} // namespace CPPPdf

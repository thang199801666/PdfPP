#include <CPPPdf/Document/PdfPageEditor.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Writer/PdfIncrementalWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>

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
    for (const auto& [key, value] : dictionary.values()) {
        output << '\n' << escapeName(key.value()) << ' ';
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
    case PdfObjectType::IndirectReference: {
        const auto reference = object.AsReference();
        output << reference->first << ' ' << reference->second << " R";
        break;
    }
    case PdfObjectType::Stream:
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Direct stream objects cannot be embedded in an updated page dictionary.");
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


PdfObject deepCloneObject(const PdfObject& object);

PdfArray deepCloneArray(const PdfArray& source) {
    PdfArray result;
    for (const auto& item : source.values()) result.push_back(deepCloneObject(item));
    return result;
}

PdfDictionary deepCloneDictionary(const PdfDictionary& source) {
    PdfDictionary result;
    for (const auto& [key, value] : source.values()) result.Put(key, deepCloneObject(value));
    return result;
}

PdfObject deepCloneObject(const PdfObject& object) {
    switch (object.type()) {
    case PdfObjectType::Null: return PdfObject{};
    case PdfObjectType::Boolean: return PdfObject(*object.AsBoolean());
    case PdfObjectType::Integer: return PdfObject(*object.AsInteger());
    case PdfObjectType::Real: return PdfObject(*object.AsReal());
    case PdfObjectType::Name: return PdfObject(*object.AsName());
    case PdfObjectType::String: return PdfObject(*object.AsString());
    case PdfObjectType::Array: return PdfObject(deepCloneArray(*object.AsArray()));
    case PdfObjectType::Dictionary: return PdfObject(deepCloneDictionary(*object.AsDictionary()));
    case PdfObjectType::Stream: {
        const auto* stream = object.AsStream();
        std::vector<std::byte> bytes(stream->bytes().begin(), stream->bytes().end());
        return PdfObject(PdfStream(deepCloneDictionary(stream->dictionary()), std::move(bytes)));
    }
    case PdfObjectType::IndirectReference: {
        const auto reference = object.AsReference();
        return PdfObject::IndirectReference(reference->first, reference->second);
    }
    }
    return PdfObject{};
}

PdfDictionary copyPageDictionary(const PdfDocument& document, const PdfReference& reference) {
    const PdfObject& object = document.GetObject(reference);
    const PdfDictionary* dictionary = object.AsDictionary();
    if (!dictionary) throw PdfException(PdfErrorCode::MalformedObject, "Page object is not a dictionary.");
    return deepCloneDictionary(*dictionary);
}

PdfArray collectContents(const PdfDictionary& pageDictionary) {
    PdfArray contents;
    const PdfObject* value = pageDictionary.Find(PdfName::Contents);
    if (!value) return contents;
    if (value->AsStream()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Direct page content streams are not supported by incremental page editing yet.");
    }
    if (const PdfArray* array = value->AsArray()) {
        for (const auto& item : array->values()) {
            if (item.AsStream()) {
                throw PdfException(PdfErrorCode::UnsupportedFeature,
                                   "Direct streams inside /Contents arrays are not supported yet.");
            }
            contents.push_back(item);
        }
    } else {
        contents.push_back(*value);
    }
    return contents;
}

PdfArray rectangleArray(const PdfRectangle& rectangle) {
    PdfArray array;
    array.push_back(PdfObject(rectangle.left));
    array.push_back(PdfObject(rectangle.bottom));
    array.push_back(PdfObject(rectangle.right));
    array.push_back(PdfObject(rectangle.top));
    return array;
}

void writeXrefEntry(std::ostream& output, std::uint64_t offset, std::uint16_t generation) {
    output << std::setw(10) << std::setfill('0') << offset << ' '
           << std::setw(5) << std::setfill('0') << generation << " n \n";
}


PdfDictionary resolveDictionaryValue(
    const PdfDocument& document,
    const PdfObject* value) {
    if (!value) return {};
    if (const auto* dictionary = value->AsDictionary()) return deepCloneDictionary(*dictionary);
    if (const auto reference = value->AsReference()) {
        const PdfObject& resolved = document.GetObject({reference->first, reference->second});
        if (const auto* dictionary = resolved.AsDictionary()) return deepCloneDictionary(*dictionary);
    }
    return {};
}

PdfDictionary inheritedResources(
    const PdfDocument& document,
    PdfReference pageReference) {
    std::set<std::uint64_t> visited;
    for (std::size_t depth = 0; depth < 256U; ++depth) {
        const std::uint64_t key = (static_cast<std::uint64_t>(pageReference.objectNumber) << 16U) |
                                  pageReference.generation;
        if (!visited.insert(key).second) {
            throw PdfException(PdfErrorCode::InvalidPageTree, "Cycle detected while resolving page resources.");
        }
        const PdfObject& object = document.GetObject(pageReference);
        const PdfDictionary* dictionary = object.AsDictionary();
        if (!dictionary) return {};
        if (const PdfObject* resources = dictionary->Find(PdfName::Resources)) {
            return resolveDictionaryValue(document, resources);
        }
        const PdfObject* parent = dictionary->Find(PdfName("Parent"));
        if (!parent) return {};
        const auto reference = parent->AsReference();
        if (!reference) return {};
        pageReference = {reference->first, reference->second};
    }
    throw PdfException(PdfErrorCode::InvalidPageTree,
                       "Page resource inheritance exceeded the configured depth.");
}

PdfDictionary ensureSubDictionary(
    const PdfDocument& document,
    const PdfDictionary& parent,
    const PdfName& key) {
    return resolveDictionaryValue(document, parent.Find(key));
}


void installStampResources(
    const PdfDocument& document,
    const PdfReference& pageReference,
    PdfDictionary& pageDictionary) {
    PdfDictionary resources = inheritedResources(document, pageReference);
    PdfDictionary fonts = ensureSubDictionary(document, resources, PdfName::Font);
    PdfDictionary font;
    font.Put(PdfName::Type, PdfObject(PdfName("Font")));
    font.Put(PdfName("Subtype"), PdfObject(PdfName("Type1")));
    font.Put(PdfName("BaseFont"), PdfObject(PdfName("Helvetica")));
    fonts.Put(PdfName("PPStampFont"), PdfObject(std::move(font)));
    resources.Put(PdfName::Font, PdfObject(std::move(fonts)));

    PdfDictionary states = ensureSubDictionary(document, resources, PdfName("ExtGState"));
    PdfDictionary state;
    state.Put(PdfName::Type, PdfObject(PdfName("ExtGState")));
    state.Put(PdfName("CA"), PdfObject(1.0));
    state.Put(PdfName("ca"), PdfObject(1.0));
    states.Put(PdfName("PPStampGS"), PdfObject(std::move(state)));
    resources.Put(PdfName("ExtGState"), PdfObject(std::move(states)));
    pageDictionary.Put(PdfName::Resources, PdfObject(std::move(resources)));
}

std::string makeTextStampContent(
    const PdfTextStampOptions& options,
    const PdfRectangle& pageBox,
    bool watermarkMode,
    const PdfWatermarkOptions* watermark = nullptr) {
    std::string text = options.text;
    double fontSize = options.fontSize;
    PdfColor color = options.textColor;
    double opacity = std::clamp(options.opacity, 0.0, 1.0);
    double rotation = options.rotationDegrees;
    double x = options.position.x;
    double y = options.position.y;

    if (watermarkMode && watermark) {
        text = watermark->text;
        fontSize = watermark->fontSize;
        color = watermark->color;
        opacity = std::clamp(watermark->opacity, 0.0, 1.0);
        rotation = watermark->rotationDegrees;
        const double estimatedWidth = std::max(1.0, fontSize * 0.55 * static_cast<double>(text.size()));
        switch (watermark->horizontalAlignment) {
        case PdfStampHorizontalAlignment::Left: x = pageBox.left; break;
        case PdfStampHorizontalAlignment::Center: x = pageBox.left + (pageBox.width() - estimatedWidth) * 0.5; break;
        case PdfStampHorizontalAlignment::Right: x = pageBox.right - estimatedWidth; break;
        }
        switch (watermark->verticalAlignment) {
        case PdfStampVerticalAlignment::Bottom: y = pageBox.bottom; break;
        case PdfStampVerticalAlignment::Middle: y = pageBox.bottom + (pageBox.height() - fontSize) * 0.5; break;
        case PdfStampVerticalAlignment::Top: y = pageBox.top - fontSize; break;
        }
        x += watermark->offset.x;
        y += watermark->offset.y;
    }

    const double radians = rotation * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double estimatedWidth = std::max(1.0, fontSize * 0.55 * static_cast<double>(text.size()));
    const double padding = options.padding;

    std::ostringstream output;
    output << "q\n/PPStampGS gs\n";
    if (!watermarkMode && options.drawBackground) {
        output << options.backgroundColor.r << ' ' << options.backgroundColor.g << ' '
               << options.backgroundColor.b << " rg\n"
               << (x - padding) << ' ' << (y - padding) << ' '
               << (estimatedWidth + 2.0 * padding) << ' ' << (fontSize + 2.0 * padding)
               << " re f\n";
    }
    if (!watermarkMode && options.drawBorder) {
        output << options.borderColor.r << ' ' << options.borderColor.g << ' '
               << options.borderColor.b << " RG\n" << options.borderWidth << " w\n"
               << (x - padding) << ' ' << (y - padding) << ' '
               << (estimatedWidth + 2.0 * padding) << ' ' << (fontSize + 2.0 * padding)
               << " re S\n";
    }
    output << color.r << ' ' << color.g << ' ' << color.b << " rg\n"
           << "BT\n/PPStampFont " << fontSize << " Tf\n"
           << cosine << ' ' << sine << ' ' << -sine << ' ' << cosine << ' ' << x << ' ' << y << " Tm\n"
           << '(' << escapeLiteral(text) << ") Tj\nET\nQ\n";
    (void)opacity; // ExtGState opacity is patched in installStampResources per page below.
    return output.str();
}

void setStampOpacity(PdfDictionary& pageDictionary, double opacity) {
    PdfDictionary* resources = pageDictionary.Find(PdfName::Resources)
        ? const_cast<PdfDictionary*>(pageDictionary.Find(PdfName::Resources)->AsDictionary())
        : nullptr;
    if (!resources) return;
    PdfDictionary* states = resources->Find(PdfName("ExtGState"))
        ? const_cast<PdfDictionary*>(resources->Find(PdfName("ExtGState"))->AsDictionary())
        : nullptr;
    if (!states) return;
    PdfDictionary* state = states->Find(PdfName("PPStampGS"))
        ? const_cast<PdfDictionary*>(states->Find(PdfName("PPStampGS"))->AsDictionary())
        : nullptr;
    if (!state) return;
    const double value = std::clamp(opacity, 0.0, 1.0);
    state->Put(PdfName("CA"), PdfObject(value));
    state->Put(PdfName("ca"), PdfObject(value));
}


std::string pdfImageColorSpaceName(const PdfImageColorSpace colorSpace) {
    switch (colorSpace) {
    case PdfImageColorSpace::DeviceGray: return "/DeviceGray";
    case PdfImageColorSpace::DeviceRGB: return "/DeviceRGB";
    case PdfImageColorSpace::DeviceCMYK: return "/DeviceCMYK";
    default:
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Existing-PDF image stamps currently support DeviceGray, DeviceRGB and DeviceCMYK only.");
    }
}

void writeImageObjectBody(std::ostream& output, const PdfImage& image) {
    const auto bytes = image.GetBytes();
    output << "<< /Type /XObject /Subtype /Image"
           << " /Width " << image.GetWidth()
           << " /Height " << image.GetHeight()
           << " /ColorSpace " << pdfImageColorSpaceName(image.GetColorSpace())
           << " /BitsPerComponent " << image.GetBitsPerComponent();
    if (image.GetEncoding() == PdfImageEncoding::Dct) {
        output << " /Filter /DCTDecode";
    } else if (image.GetEncoding() != PdfImageEncoding::Raw) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Existing-PDF image stamps support raw samples and JPEG pass-through only.");
    }
    output << " /Length " << bytes.size() << " >>\nstream\n";
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output << "\nendstream";
}

std::string makeImageStampContent(
    const std::string& imageResourceName,
    const std::string& stateResourceName,
    const PdfImageStampOptions& options) {
    const auto& rectangle = options.rectangle;
    std::ostringstream output;
    output << "q\n/" << stateResourceName << " gs\n"
           << rectangle.width() << " 0 0 " << rectangle.height() << ' '
           << rectangle.left << ' ' << rectangle.bottom << " cm\n/"
           << imageResourceName << " Do\nQ\n";
    if (options.drawBorder) {
        output << "q\n" << options.borderColor.r << ' ' << options.borderColor.g << ' '
               << options.borderColor.b << " RG\n" << options.borderWidth << " w\n"
               << rectangle.left << ' ' << rectangle.bottom << ' '
               << rectangle.width() << ' ' << rectangle.height() << " re S\nQ\n";
    }
    return output.str();
}

} // namespace

std::string PdfContentCommands::DrawLine(
    PdfPoint start,
    PdfPoint end,
    double lineWidth,
    double red,
    double green,
    double blue) {
    std::ostringstream output;
    output << "q\n" << red << ' ' << green << ' ' << blue << " RG\n"
           << lineWidth << " w\n"
           << start.x << ' ' << start.y << " m\n"
           << end.x << ' ' << end.y << " l\nS\nQ\n";
    return output.str();
}

std::string PdfContentCommands::FillRectangle(
    const PdfRectangle& rectangle,
    double red,
    double green,
    double blue) {
    std::ostringstream output;
    output << "q\n" << red << ' ' << green << ' ' << blue << " rg\n"
           << rectangle.left << ' ' << rectangle.bottom << ' '
           << rectangle.width() << ' ' << rectangle.height() << " re\nf\nQ\n";
    return output.str();
}

std::string PdfContentCommands::StrokeRectangle(
    const PdfRectangle& rectangle,
    double lineWidth,
    double red,
    double green,
    double blue) {
    std::ostringstream output;
    output << "q\n" << red << ' ' << green << ' ' << blue << " RG\n"
           << lineWidth << " w\n"
           << rectangle.left << ' ' << rectangle.bottom << ' '
           << rectangle.width() << ' ' << rectangle.height() << " re\nS\nQ\n";
    return output.str();
}

PdfPageEditResult PdfPageEditor::AddContent(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    std::size_t pageIndex,
    std::string content,
    PdfContentLayer layer) {
    PdfPageEdit edit;
    edit.pageIndex = pageIndex;
    if (layer == PdfContentLayer::Background) edit.backgroundContent = std::move(content);
    else edit.foregroundContent = std::move(content);
    return ApplyEdits(inputPath, outputPath, {std::move(edit)});
}

PdfPageEditResult PdfPageEditor::AddTextStamp(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    std::size_t pageIndex,
    const PdfTextStampOptions& options) {
    PdfPageEdit edit;
    edit.pageIndex = pageIndex;
    edit.textStamps.push_back(options);
    return ApplyEdits(inputPath, outputPath, {std::move(edit)});
}

PdfPageEditResult PdfPageEditor::AddTextStampToAllPages(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfTextStampOptions& options) {
    const PdfDocument document = PdfDocument::Open(inputPath);
    std::vector<PdfPageEdit> edits;
    edits.reserve(document.GetPageCount());
    for (std::size_t pageIndex = 0; pageIndex < document.GetPageCount(); ++pageIndex) {
        PdfPageEdit edit;
        edit.pageIndex = pageIndex;
        edit.textStamps.push_back(options);
        edits.push_back(std::move(edit));
    }
    return ApplyEdits(inputPath, outputPath, edits);
}

PdfPageEditResult PdfPageEditor::AddWatermark(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    std::size_t pageIndex,
    const PdfWatermarkOptions& options) {
    PdfPageEdit edit;
    edit.pageIndex = pageIndex;
    edit.watermarks.push_back(options);
    return ApplyEdits(inputPath, outputPath, {std::move(edit)});
}

PdfPageEditResult PdfPageEditor::AddImageStamp(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::size_t pageIndex,
    const PdfImage& image,
    const PdfImageStampOptions& options) {
    PdfPageEdit edit;
    edit.pageIndex = pageIndex;
    edit.imageStamps.emplace_back(image, options);
    return ApplyEdits(inputPath, outputPath, {std::move(edit)});
}

PdfPageEditResult PdfPageEditor::AddImageStampToAllPages(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfImage& image,
    const PdfImageStampOptions& options) {
    const PdfDocument document = PdfDocument::Open(inputPath);
    std::vector<PdfPageEdit> edits;
    edits.reserve(document.GetPageCount());
    for (std::size_t pageIndex = 0; pageIndex < document.GetPageCount(); ++pageIndex) {
        PdfPageEdit edit;
        edit.pageIndex = pageIndex;
        edit.imageStamps.emplace_back(image, options);
        edits.push_back(std::move(edit));
    }
    return ApplyEdits(inputPath, outputPath, edits);
}

PdfPageEditResult PdfPageEditor::AddWatermarkToAllPages(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfWatermarkOptions& options) {
    const PdfDocument document = PdfDocument::Open(inputPath);
    std::vector<PdfPageEdit> edits;
    edits.reserve(document.GetPageCount());
    for (std::size_t pageIndex = 0; pageIndex < document.GetPageCount(); ++pageIndex) {
        PdfPageEdit edit;
        edit.pageIndex = pageIndex;
        edit.watermarks.push_back(options);
        edits.push_back(std::move(edit));
    }
    return ApplyEdits(inputPath, outputPath, edits);
}

PdfPageEditResult PdfPageEditor::ApplyEdits(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<PdfPageEdit>& edits,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (document.IsEncrypted() && !document.IsOwnerPasswordAuthenticated() &&
        (static_cast<std::uint32_t>(document.GetPermissionBits()) & 8U) == 0U) {
        throw PdfException(PdfErrorCode::PermissionDenied,
                           "The user password does not permit page-content modification.");
    }

    std::map<std::size_t, PdfPageEdit> merged;
    for (const auto& edit : edits) {
        if (edit.pageIndex >= document.GetPageCount()) {
            throw PdfException(PdfErrorCode::InvalidArgument, "Page edit index is outside the document.");
        }
        auto [iterator, inserted] = merged.emplace(edit.pageIndex, edit);
        if (!inserted) {
            auto& target = iterator->second;
            target.backgroundContent += edit.backgroundContent;
            target.foregroundContent += edit.foregroundContent;
            if (edit.rotation) target.rotation = edit.rotation;
            if (edit.mediaBox) target.mediaBox = edit.mediaBox;
            if (edit.cropBox) target.cropBox = edit.cropBox;
            target.textStamps.insert(target.textStamps.end(), edit.textStamps.begin(), edit.textStamps.end());
            target.watermarks.insert(target.watermarks.end(), edit.watermarks.begin(), edit.watermarks.end());
            target.imageStamps.insert(target.imageStamps.end(), edit.imageStamps.begin(), edit.imageStamps.end());
        }
    }

    PdfPageEditResult result{outputPath, merged.size(), 0U};
    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t objectNumber = Internal::PdfIncrementalWriter::NextObjectNumber(document);

    for (const auto& [pageIndex, edit] : merged) {
        const PdfReference pageReference = document.GetPageReference(pageIndex);
        PdfDictionary pageDictionary = copyPageDictionary(document, pageReference);
        PdfArray contents = collectContents(pageDictionary);
        std::string backgroundContent = edit.backgroundContent;
        std::string foregroundContent = edit.foregroundContent;

        if (!edit.imageStamps.empty()) {
            PdfDictionary resources = inheritedResources(document, pageReference);
            PdfDictionary xObjects = ensureSubDictionary(document, resources, PdfName("XObject"));
            PdfDictionary states = ensureSubDictionary(document, resources, PdfName("ExtGState"));
            std::size_t stampIndex = 0U;
            for (const auto& [image, options] : edit.imageStamps) {
                const std::uint32_t imageNumber = objectNumber++;
                std::ostringstream imageBody;
                writeImageObjectBody(imageBody, image);
                writer.WriteRawObject(PdfReference{imageNumber, 0U}, imageBody.str());

                const std::string imageName = "PPImage" + std::to_string(stampIndex + 1U);
                const std::string stateName = "PPImageGS" + std::to_string(stampIndex + 1U);
                xObjects.Put(PdfName(imageName), PdfObject::IndirectReference(imageNumber, 0U));

                PdfDictionary state;
                state.Put(PdfName::Type, PdfObject(PdfName("ExtGState")));
                const double opacity = std::clamp(options.opacity, 0.0, 1.0);
                state.Put(PdfName("CA"), PdfObject(opacity));
                state.Put(PdfName("ca"), PdfObject(opacity));
                states.Put(PdfName(stateName), PdfObject(std::move(state)));

                const std::string command = makeImageStampContent(imageName, stateName, options);
                if (options.layer == PdfStampLayer::Background) backgroundContent += command;
                else foregroundContent += command;
                ++stampIndex;
            }
            resources.Put(PdfName("XObject"), PdfObject(std::move(xObjects)));
            resources.Put(PdfName("ExtGState"), PdfObject(std::move(states)));
            pageDictionary.Put(PdfName::Resources, PdfObject(std::move(resources)));
        }

        if (!edit.textStamps.empty() || !edit.watermarks.empty()) {
            installStampResources(document, pageReference, pageDictionary);
            const PdfRectangle pageBox = edit.cropBox.value_or(
                document.GetPageInfo(pageIndex).cropBox.width() > 0.0
                    ? document.GetPageInfo(pageIndex).cropBox
                    : document.GetPageInfo(pageIndex).mediaBox);
            for (const auto& stamp : edit.textStamps) {
                setStampOpacity(pageDictionary, stamp.opacity);
                const std::string command = makeTextStampContent(stamp, pageBox, false);
                if (stamp.layer == PdfStampLayer::Background) {
                    backgroundContent += command;
                } else {
                    foregroundContent += command;
                }
            }
            for (const auto& watermark : edit.watermarks) {
                setStampOpacity(pageDictionary, watermark.opacity);
                PdfTextStampOptions bridge;
                bridge.text = watermark.text;
                bridge.fontName = watermark.fontName;
                bridge.fontSize = watermark.fontSize;
                bridge.textColor = watermark.color;
                bridge.opacity = watermark.opacity;
                bridge.rotationDegrees = watermark.rotationDegrees;
                const std::string command = makeTextStampContent(bridge, pageBox, true, &watermark);
                if (watermark.layer == PdfStampLayer::Background) {
                    backgroundContent += command;
                } else {
                    foregroundContent += command;
                }
            }
        }

        auto appendStream = [&](const std::string& streamContent, bool background) {
            if (streamContent.empty()) return;
            const std::uint32_t streamNumber = objectNumber++;
            std::vector<std::byte> streamBytes(streamContent.size());
            std::transform(streamContent.begin(), streamContent.end(), streamBytes.begin(),
                           [](const char value) {
                               return static_cast<std::byte>(static_cast<unsigned char>(value));
                           });
            writer.WriteObject(PdfReference{streamNumber, 0U},
                               PdfObject(PdfStream(PdfDictionary{}, std::move(streamBytes))));

            PdfArray updated;
            if (background) updated.push_back(PdfObject::IndirectReference(streamNumber, 0U));
            for (const auto& item : contents.values()) updated.push_back(item);
            if (!background) updated.push_back(PdfObject::IndirectReference(streamNumber, 0U));
            contents = std::move(updated);
            ++result.appendedContentStreamCount;
        };

        appendStream(backgroundContent, true);
        appendStream(foregroundContent, false);

        if (!contents.empty()) pageDictionary.Put(PdfName::Contents, PdfObject(std::move(contents)));
        if (edit.rotation) {
            int rotation = *edit.rotation % 360;
            if (rotation < 0) rotation += 360;
            if (rotation % 90 != 0) {
                throw PdfException(PdfErrorCode::InvalidArgument, "Page rotation must be a multiple of 90 degrees.");
            }
            pageDictionary.Put(PdfName::Rotate, PdfObject(static_cast<std::int64_t>(rotation)));
        }
        if (edit.mediaBox) pageDictionary.Put(PdfName::MediaBox, PdfObject(rectangleArray(*edit.mediaBox)));
        if (edit.cropBox) pageDictionary.Put(PdfName::CropBox, PdfObject(rectangleArray(*edit.cropBox)));

        writer.WriteDictionary(pageReference, pageDictionary);
    }
    writer.Finish(objectNumber);
    return result;
}

std::size_t PdfPageEditor::SetPageBox(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::size_t pageIndex,
    const PdfRectangle& box,
    const bool cropBox,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (pageIndex >= document.GetPageCount()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Page index is out of range.");
    }
    const PdfReference pageReference = document.GetPageReference(pageIndex);
    const PdfObject& pageObject = document.GetObject(pageReference);
    const PdfDictionary* dictionary = pageObject.AsDictionary();
    if (!dictionary) throw PdfException(PdfErrorCode::MalformedObject, "Page is not a dictionary.");
    PdfDictionary updated = *dictionary;
    PdfArray rect;
    rect.push_back(PdfObject(box.left));
    rect.push_back(PdfObject(box.bottom));
    rect.push_back(PdfObject(box.right));
    rect.push_back(PdfObject(box.top));
    updated.Put(PdfName(cropBox ? "CropBox" : "MediaBox"), PdfObject(std::move(rect)));
    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    writer.WriteDictionary(pageReference, updated);
    writer.Finish(Internal::PdfIncrementalWriter::NextObjectNumber(document));
    return 1U;
}

} // namespace CPPPdf

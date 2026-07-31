#include <CPPPdf/Forms/PdfAcroForm.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace CPPPdf {
namespace {

struct FieldNode final {
    PdfReference reference{};
    PdfDictionary dictionary;
    std::string fullName;
    std::string partialName;
    PdfName fieldType;
    std::uint32_t flags{};
    PdfObject inheritedValue;
    std::vector<PdfReference> widgets;
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open input PDF: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

PdfObject deepClone(const PdfObject& source);

PdfArray deepCloneArray(const PdfArray& source) {
    PdfArray result;
    for (const auto& value : source.values()) result.push_back(deepClone(value));
    return result;
}

PdfDictionary deepCloneDictionary(const PdfDictionary& source) {
    PdfDictionary result;
    for (const auto& [key, value] : source.values()) result.Put(key, deepClone(value));
    return result;
}

PdfObject deepClone(const PdfObject& source) {
    switch (source.type()) {
    case PdfObjectType::Null: return PdfObject{};
    case PdfObjectType::Boolean: return PdfObject(*source.AsBoolean());
    case PdfObjectType::Integer: return PdfObject(*source.AsInteger());
    case PdfObjectType::Real: return PdfObject(*source.AsReal());
    case PdfObjectType::Name: return PdfObject(*source.AsName());
    case PdfObjectType::String: return PdfObject(*source.AsString());
    case PdfObjectType::Array: return PdfObject(deepCloneArray(*source.AsArray()));
    case PdfObjectType::Dictionary: return PdfObject(deepCloneDictionary(*source.AsDictionary()));
    case PdfObjectType::Stream: {
        const PdfStream& stream = *source.AsStream();
        return PdfObject(PdfStream(
            deepCloneDictionary(stream.dictionary()),
            std::vector<std::byte>(stream.bytes().begin(), stream.bytes().end())));
    }
    case PdfObjectType::IndirectReference: {
        const auto reference = *source.AsReference();
        return PdfObject::IndirectReference(reference.first, reference.second);
    }
    }
    return PdfObject{};
}

const PdfDictionary* resolveDictionary(const PdfDocument& document, const PdfObject& object) {
    if (const PdfDictionary* dictionary = object.AsDictionary()) return dictionary;
    if (const auto reference = object.AsReference()) {
        return document.GetObject({reference->first, reference->second}).AsDictionary();
    }
    return nullptr;
}

PdfReference requireReference(const PdfObject& object, std::string_view context) {
    const auto reference = object.AsReference();
    if (!reference) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
            std::string(context) + " must be an indirect reference for incremental form editing.");
    }
    return {reference->first, reference->second};
}

std::optional<PdfReference> acroFormReference(const PdfDocument& document) {
    const PdfDictionary* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    if (!catalog) throw PdfException(PdfErrorCode::MalformedObject, "Catalog is not a dictionary.");
    const PdfObject* value = catalog->Find(PdfName("AcroForm"));
    if (!value) return std::nullopt;
    const auto reference = value->AsReference();
    if (!reference) return std::nullopt;
    return PdfReference{reference->first, reference->second};
}

const PdfDictionary* acroFormDictionary(const PdfDocument& document) {
    const PdfDictionary* catalog = document.GetObject(document.GetCatalogReference()).AsDictionary();
    if (!catalog) throw PdfException(PdfErrorCode::MalformedObject, "Catalog is not a dictionary.");
    const PdfObject* value = catalog->Find(PdfName("AcroForm"));
    return value ? resolveDictionary(document, *value) : nullptr;
}

std::string objectTextValue(const PdfObject* value) {
    if (!value) return {};
    if (const std::string* text = value->AsString()) return *text;
    if (const PdfName* name = value->AsName()) return name->value();
    return {};
}

std::uint32_t objectFlags(const PdfObject* value, std::uint32_t fallback) {
    if (!value) return fallback;
    const auto integer = value->AsInteger();
    if (!integer || *integer < 0) return fallback;
    return static_cast<std::uint32_t>(*integer);
}

PdfFormFieldType fieldType(const PdfName& type) {
    if (type.value() == "Tx") return PdfFormFieldType::Text;
    if (type.value() == "Btn") return PdfFormFieldType::Button;
    if (type.value() == "Ch") return PdfFormFieldType::Choice;
    if (type.value() == "Sig") return PdfFormFieldType::Signature;
    return PdfFormFieldType::Unknown;
}

bool isWidget(const PdfDictionary& dictionary) {
    const auto subtype = dictionary.GetAsName(PdfName("Subtype"));
    return subtype && subtype->value() == "Widget";
}

std::vector<std::string> parseOptions(const PdfObject* value) {
    std::vector<std::string> result;
    const PdfArray* array = value ? value->AsArray() : nullptr;
    if (!array) return result;
    for (const auto& item : array->values()) {
        if (const std::string* text = item.AsString()) {
            result.push_back(*text);
        } else if (const PdfArray* pair = item.AsArray(); pair && !pair->empty()) {
            result.push_back(objectTextValue(&pair->at(pair->size() > 1U ? 1U : 0U)));
        }
    }
    return result;
}

std::unordered_map<std::uint64_t, std::size_t> buildWidgetPageMap(const PdfDocument& document) {
    std::unordered_map<std::uint64_t, std::size_t> result;
    for (std::size_t pageIndex = 0; pageIndex < document.GetPageCount(); ++pageIndex) {
        const PdfDictionary* page = document.GetObject(document.GetPageReference(pageIndex)).AsDictionary();
        if (!page) continue;
        const PdfObject* annots = page->Find(PdfName("Annots"));
        const PdfArray* array = nullptr;
        if (annots) {
            array = annots->AsArray();
            if (!array) {
                if (const auto ref = annots->AsReference()) {
                    array = document.GetObject({ref->first, ref->second}).AsArray();
                }
            }
        }
        if (!array) continue;
        for (const auto& annotation : array->values()) {
            if (const auto reference = annotation.AsReference()) {
                const std::uint64_t key = (static_cast<std::uint64_t>(reference->first) << 16U) | reference->second;
                result.emplace(key, pageIndex);
            }
        }
    }
    return result;
}

void collectFieldNodes(
    const PdfDocument& document,
    const PdfArray& fields,
    const std::string& parentName,
    const PdfName& inheritedType,
    const std::uint32_t inheritedFlags,
    const PdfObject& inheritedValue,
    std::vector<FieldNode>& output,
    std::unordered_set<std::uint64_t>& active) {

    for (const auto& item : fields.values()) {
        const PdfReference reference = requireReference(item, "AcroForm field");
        const std::uint64_t key = (static_cast<std::uint64_t>(reference.objectNumber) << 16U) | reference.generation;
        if (!active.insert(key).second) {
            throw PdfException(PdfErrorCode::MalformedObject, "Cycle detected in AcroForm field tree.");
        }

        const PdfDictionary* source = document.GetObject(reference).AsDictionary();
        if (!source) throw PdfException(PdfErrorCode::MalformedObject, "AcroForm field is not a dictionary.");
        const std::string partial = objectTextValue(source->Find(PdfName("T")));
        const std::string full = partial.empty() ? parentName :
            (parentName.empty() ? partial : parentName + "." + partial);
        const PdfName type = source->GetAsName(PdfName("FT")).value_or(inheritedType);
        const std::uint32_t flags = objectFlags(source->Find(PdfName("Ff")), inheritedFlags);
        PdfObject value = source->Contains(PdfName("V")) ? deepClone(source->Get(PdfName("V"))) : deepClone(inheritedValue);

        std::vector<PdfReference> widgets;
        if (isWidget(*source)) widgets.push_back(reference);
        const PdfArray* kids = source->GetAsArray(PdfName("Kids"));
        if (kids) {
            for (const auto& kid : kids->values()) {
                const auto kidReference = kid.AsReference();
                if (!kidReference) continue;
                const PdfReference widgetRef{kidReference->first, kidReference->second};
                const PdfDictionary* kidDictionary = document.GetObject(widgetRef).AsDictionary();
                if (kidDictionary && isWidget(*kidDictionary)) widgets.push_back(widgetRef);
            }
        }

        const bool terminal = !kids || widgets.size() == kids->size() || !source->Find(PdfName("Kids"));
        if (!full.empty() && (terminal || !type.empty())) {
            output.push_back(FieldNode{
                reference,
                deepCloneDictionary(*source),
                full,
                partial,
                type,
                flags,
                value,
                widgets});
        }

        if (kids) {
            PdfArray childFields;
            for (const auto& kid : kids->values()) {
                const auto kidReference = kid.AsReference();
                if (!kidReference) continue;
                const PdfDictionary* kidDictionary = document.GetObject({kidReference->first, kidReference->second}).AsDictionary();
                if (kidDictionary && !isWidget(*kidDictionary)) childFields.push_back(kid);
            }
            if (!childFields.empty()) {
                collectFieldNodes(document, childFields, full, type, flags, value, output, active);
            }
        }
        active.erase(key);
    }
}

std::vector<FieldNode> readFieldNodes(const PdfDocument& document) {
    const PdfDictionary* form = acroFormDictionary(document);
    if (!form) return {};
    const PdfArray* fields = form->GetAsArray(PdfName("Fields"));
    if (!fields) return {};
    std::vector<FieldNode> result;
    std::unordered_set<std::uint64_t> active;
    collectFieldNodes(document, *fields, {}, PdfName{}, 0U, PdfObject{}, result, active);
    return result;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool checkedValue(std::string_view value) {
    const std::string normalized = lowerAscii(std::string(value));
    return normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on" || normalized == "checked";
}

std::string widgetOnState(const PdfDocument& document, const PdfReference& reference) {
    const PdfDictionary* widget = document.GetObject(reference).AsDictionary();
    if (!widget) return "Yes";
    const PdfObject* apObject = widget->Find(PdfName("AP"));
    const PdfDictionary* ap = apObject ? resolveDictionary(document, *apObject) : nullptr;
    if (!ap) return "Yes";
    const PdfObject* normalObject = ap->Find(PdfName("N"));
    const PdfDictionary* normal = normalObject ? resolveDictionary(document, *normalObject) : nullptr;
    if (!normal) return "Yes";
    for (const auto& [name, unused] : normal->values()) {
        (void)unused;
        if (name.value() != "Off") return name.value();
    }
    return "Yes";
}

PdfDictionary parseTrailer(const PdfDocument& document) {
    const PdfObject parsed = Internal::PdfObjectParser::Parse(document.trailerDictionary(), 256U);
    const PdfDictionary* dictionary = parsed.AsDictionary();
    if (!dictionary) throw PdfException(PdfErrorCode::MalformedXref, "Trailer is not a dictionary.");
    return deepCloneDictionary(*dictionary);
}

void writeXrefEntries(
    std::ostream& output,
    const std::vector<std::pair<PdfReference, std::uint64_t>>& entries) {
    std::size_t index = 0U;
    while (index < entries.size()) {
        const std::size_t begin = index;
        std::uint32_t expected = entries[index].first.objectNumber;
        while (index < entries.size() && entries[index].first.objectNumber == expected) {
            ++index;
            ++expected;
        }
        output << entries[begin].first.objectNumber << ' ' << (index - begin) << '\n';
        for (std::size_t i = begin; i < index; ++i) {
            output << std::setw(10) << std::setfill('0') << entries[i].second << ' '
                   << std::setw(5) << std::setfill('0') << entries[i].first.generation << " n \n";
        }
    }
}

void writeIncrementalRevisions(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfDocument& document,
    const std::map<std::uint32_t, std::pair<PdfReference, PdfDictionary>>& revisions) {

    const std::string source = readFile(inputPath);
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot create output PDF: " + outputPath.string());
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!source.empty() && source.back() != '\n') output << '\n';

    std::vector<std::pair<PdfReference, std::uint64_t>> entries;
    entries.reserve(revisions.size());
    for (const auto& [number, revision] : revisions) {
        (void)number;
        const auto& [reference, dictionary] = revision;
        const auto offset = static_cast<std::uint64_t>(output.tellp());
        output << reference.objectNumber << ' ' << reference.generation << " obj\n";
        Internal::PdfObjectSerializer::WriteDictionary(output, dictionary);
        output << "\nendobj\n";
        entries.emplace_back(reference, offset);
    }

    const auto xrefOffset = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n";
    writeXrefEntries(output, entries);

    PdfDictionary trailer = parseTrailer(document);
    trailer.Put(PdfName("Prev"), PdfObject(static_cast<std::int64_t>(document.GetStartXrefOffset())));
    output << "trailer\n";
    Internal::PdfObjectSerializer::WriteDictionary(output, trailer);
    output << "\nstartxref\n" << xrefOffset << "\n%%EOF\n";
}

using IncrementalObjects = std::map<std::uint32_t, std::pair<PdfReference, PdfObject>>;

std::uint32_t nextObjectNumber(const PdfDocument& document) {
    std::uint32_t maximum = 0U;
    for (const auto number : document.objectNumbers()) maximum = std::max(maximum, number);
    return maximum + 1U;
}

std::optional<PdfRectangle> rectangleFromObject(const PdfObject* object) {
    const PdfArray* array = object ? object->AsArray() : nullptr;
    if (!array || array->size() != 4U) return std::nullopt;
    auto number = [](const PdfObject& value) -> std::optional<double> {
        if (const auto real = value.AsReal()) return *real;
        if (const auto integer = value.AsInteger()) return static_cast<double>(*integer);
        return std::nullopt;
    };
    const auto left = number(array->at(0U));
    const auto bottom = number(array->at(1U));
    const auto right = number(array->at(2U));
    const auto top = number(array->at(3U));
    if (!left || !bottom || !right || !top) return std::nullopt;
    return PdfRectangle{*left, *bottom, *right, *top};
}

PdfObject rectangleArray(const PdfRectangle& rectangle) {
    PdfArray array;
    array.push_back(PdfObject(rectangle.left));
    array.push_back(PdfObject(rectangle.bottom));
    array.push_back(PdfObject(rectangle.right));
    array.push_back(PdfObject(rectangle.top));
    return PdfObject(std::move(array));
}

std::vector<std::byte> bytesFromString(const std::string& value) {
    std::vector<std::byte> bytes(value.size());
    std::transform(value.begin(), value.end(), bytes.begin(), [](const char ch) {
        return static_cast<std::byte>(static_cast<unsigned char>(ch));
    });
    return bytes;
}

bool fieldSelected(const std::vector<std::string>& names, const std::string& fieldName) {
    return names.empty() || std::find(names.begin(), names.end(), fieldName) != names.end();
}

std::string appearanceCommands(
    const FieldNode& node,
    const PdfRectangle& localBox,
    const PdfFormAppearanceOptions& options,
    const bool absoluteCoordinates) {
    const double x = absoluteCoordinates ? localBox.left : 0.0;
    const double y = absoluteCoordinates ? localBox.bottom : 0.0;
    const double width = std::max(1.0, localBox.width());
    const double height = std::max(1.0, localBox.height());
    std::ostringstream stream;
    stream << "q\n";
    if (options.drawBackground) {
        stream << "0.96 g " << x << ' ' << y << ' ' << width << ' ' << height << " re f\n";
    }
    if (options.drawBorder) {
        stream << "0 G 0.75 w " << x << ' ' << y << ' ' << width << ' ' << height << " re S\n";
    }
    if (fieldType(node.fieldType) == PdfFormFieldType::Button) {
        const std::string value = objectTextValue(&node.inheritedValue);
        if (!value.empty() && value != "Off") {
            const double inset = std::max(2.0, std::min(width, height) * 0.20);
            stream << "1 w " << (x + inset) << ' ' << (y + inset) << " m "
                   << (x + width - inset) << ' ' << (y + height - inset) << " l S\n"
                   << (x + inset) << ' ' << (y + height - inset) << " m "
                   << (x + width - inset) << ' ' << (y + inset) << " l S\n";
        }
    } else {
        const std::string value = objectTextValue(&node.inheritedValue);
        const double fontSize = std::max(1.0, std::min(options.fontSize, height - 2.0 * options.padding));
        const double baseline = y + std::max(options.padding, (height - fontSize) * 0.5);
        stream << "BT /PPFormFont " << fontSize << " Tf 0 g "
               << (x + options.padding) << ' ' << baseline << " Td ("
               << Internal::PdfObjectSerializer::EscapeLiteral(value) << ") Tj ET\n";
    }
    stream << "Q\n";
    return stream.str();
}

PdfDictionary formResources() {
    PdfDictionary font;
    font.Put(PdfName("Type"), PdfObject(PdfName("Font")));
    font.Put(PdfName("Subtype"), PdfObject(PdfName("Type1")));
    font.Put(PdfName("BaseFont"), PdfObject(PdfName("Helvetica")));
    PdfDictionary fonts;
    fonts.Put(PdfName("PPFormFont"), PdfObject(std::move(font)));
    PdfDictionary resources;
    resources.Put(PdfName("Font"), PdfObject(std::move(fonts)));
    return resources;
}

PdfDictionary pageResourcesWithFormFont(const PdfDocument& document, const PdfDictionary& page) {
    PdfDictionary resources;
    if (const PdfObject* object = page.Find(PdfName("Resources"))) {
        if (const PdfDictionary* direct = object->AsDictionary()) resources = deepCloneDictionary(*direct);
        else if (const auto reference = object->AsReference()) {
            if (const PdfDictionary* resolved = document.GetObject({reference->first, reference->second}).AsDictionary()) {
                resources = deepCloneDictionary(*resolved);
            }
        }
    }
    PdfDictionary fonts;
    if (const PdfObject* object = resources.Find(PdfName("Font"))) {
        if (const PdfDictionary* direct = object->AsDictionary()) fonts = deepCloneDictionary(*direct);
        else if (const auto reference = object->AsReference()) {
            if (const PdfDictionary* resolved = document.GetObject({reference->first, reference->second}).AsDictionary()) {
                fonts = deepCloneDictionary(*resolved);
            }
        }
    }
    PdfDictionary font;
    font.Put(PdfName("Type"), PdfObject(PdfName("Font")));
    font.Put(PdfName("Subtype"), PdfObject(PdfName("Type1")));
    font.Put(PdfName("BaseFont"), PdfObject(PdfName("Helvetica")));
    fonts.Put(PdfName("PPFormFont"), PdfObject(std::move(font)));
    resources.Put(PdfName("Font"), PdfObject(std::move(fonts)));
    return resources;
}

void writeIncrementalObjects(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfDocument& document,
    const IncrementalObjects& objects) {
    const std::string source = readFile(inputPath);
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot create output PDF: " + outputPath.string());
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!source.empty() && source.back() != '\n') output << '\n';

    std::vector<std::pair<PdfReference, std::uint64_t>> entries;
    for (const auto& [unused, entry] : objects) {
        (void)unused;
        const auto& [reference, object] = entry;
        const auto offset = static_cast<std::uint64_t>(output.tellp());
        output << reference.objectNumber << ' ' << reference.generation << " obj\n";
        Internal::PdfObjectSerializer::WriteObject(output, object);
        output << "\nendobj\n";
        entries.emplace_back(reference, offset);
    }
    const auto xrefOffset = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n";
    writeXrefEntries(output, entries);
    PdfDictionary trailer = parseTrailer(document);
    trailer.Put(PdfName("Prev"), PdfObject(static_cast<std::int64_t>(document.GetStartXrefOffset())));
    std::uint32_t size = nextObjectNumber(document);
    if (!objects.empty()) size = std::max(size, objects.rbegin()->first + 1U);
    trailer.Put(PdfName("Size"), PdfObject(static_cast<std::int64_t>(size)));
    output << "trailer\n";
    Internal::PdfObjectSerializer::WriteDictionary(output, trailer);
    output << "\nstartxref\n" << xrefOffset << "\n%%EOF\n";
}

} // namespace

std::vector<PdfFormFieldInfo> PdfAcroForm::GetFields(const PdfDocument& document) {
    const auto pageMap = buildWidgetPageMap(document);
    const auto nodes = readFieldNodes(document);
    std::vector<PdfFormFieldInfo> fields;
    fields.reserve(nodes.size());
    for (const auto& node : nodes) {
        PdfFormFieldInfo info;
        info.name = node.fullName;
        info.partialName = node.partialName;
        info.type = fieldType(node.fieldType);
        info.value = objectTextValue(&node.inheritedValue);
        info.options = parseOptions(node.dictionary.Find(PdfName("Opt")));
        info.reference = node.reference;
        info.widgetReferences = node.widgets;
        info.flags = node.flags;
        info.readOnly = (node.flags & 1U) != 0U;
        info.required = (node.flags & 2U) != 0U;
        info.noExport = (node.flags & 4U) != 0U;
        info.checked = info.type == PdfFormFieldType::Button && !info.value.empty() && info.value != "Off";
        for (const auto& widget : node.widgets) {
            const std::uint64_t key = (static_cast<std::uint64_t>(widget.objectNumber) << 16U) | widget.generation;
            if (const auto found = pageMap.find(key); found != pageMap.end()) {
                info.pageIndex = found->second;
                break;
            }
        }
        fields.push_back(std::move(info));
    }
    return fields;
}

std::vector<PdfFormFieldInfo> PdfAcroForm::GetFields(const std::filesystem::path& inputPath) {
    const PdfDocument document = PdfDocument::Open(inputPath);
    return GetFields(document);
}

PdfFormUpdateResult PdfAcroForm::SetFieldValues(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<PdfFormFieldUpdate>& updates,
    const PdfFormUpdateOptions& options) {

    PdfDocument document = PdfDocument::Open(inputPath);
    if (document.IsEncrypted()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature, "AcroForm editing does not support encrypted PDFs yet.");
    }
    const auto nodes = readFieldNodes(document);
    std::unordered_map<std::string, const FieldNode*> byName;
    for (const auto& node : nodes) byName.emplace(node.fullName, &node);

    std::map<std::uint32_t, std::pair<PdfReference, PdfDictionary>> revisions;
    PdfFormUpdateResult result{outputPath, 0U, 0U};

    for (const auto& update : updates) {
        const auto found = byName.find(update.name);
        if (found == byName.end()) {
            if (options.ignoreMissingFields) continue;
            throw PdfException(PdfErrorCode::InvalidArgument, "AcroForm field not found: " + update.name);
        }
        const FieldNode& node = *found->second;
        PdfDictionary revised = deepCloneDictionary(node.dictionary);
        const PdfFormFieldType type = fieldType(node.fieldType);
        if (type == PdfFormFieldType::Signature) {
            throw PdfException(PdfErrorCode::UnsupportedFeature, "Signature field values cannot be set as text.");
        }
        if (type == PdfFormFieldType::Button) {
            const bool checked = checkedValue(update.value);
            const std::string onState = node.widgets.empty() ? "Yes" : widgetOnState(document, node.widgets.front());
            revised.Put(PdfName("V"), PdfObject(PdfName(checked ? onState : "Off")));
            for (const auto& widgetReference : node.widgets) {
                const PdfDictionary* originalWidget = document.GetObject(widgetReference).AsDictionary();
                if (!originalWidget) continue;
                PdfDictionary widget = deepCloneDictionary(*originalWidget);
                widget.Put(PdfName("AS"), PdfObject(PdfName(checked ? onState : "Off")));
                revisions[widgetReference.objectNumber] = {widgetReference, std::move(widget)};
                ++result.updatedWidgetCount;
            }
        } else {
            revised.Put(PdfName("V"), PdfObject(update.value));
        }
        revisions[node.reference.objectNumber] = {node.reference, std::move(revised)};
        ++result.updatedFieldCount;
    }

    if (options.setNeedAppearances && result.updatedFieldCount > 0U) {
        if (const auto formReference = acroFormReference(document)) {
            const PdfDictionary* original = document.GetObject(*formReference).AsDictionary();
            if (original) {
                PdfDictionary revised = deepCloneDictionary(*original);
                revised.Put(PdfName("NeedAppearances"), PdfObject(true));
                revisions[formReference->objectNumber] = {*formReference, std::move(revised)};
            }
        } else {
            const PdfReference catalogReference = document.GetCatalogReference();
            const PdfDictionary* originalCatalog = document.GetObject(catalogReference).AsDictionary();
            if (originalCatalog) {
                PdfDictionary catalog = deepCloneDictionary(*originalCatalog);
                PdfObject* formObject = catalog.Find(PdfName("AcroForm"));
                PdfDictionary* directForm = formObject ? const_cast<PdfDictionary*>(formObject->AsDictionary()) : nullptr;
                if (directForm) directForm->Put(PdfName("NeedAppearances"), PdfObject(true));
                revisions[catalogReference.objectNumber] = {catalogReference, std::move(catalog)};
            }
        }
    }

    if (revisions.empty()) {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        const std::string source = readFile(inputPath);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        return result;
    }
    writeIncrementalRevisions(inputPath, outputPath, document, revisions);
    return result;
}

PdfFormAppearanceResult PdfAcroForm::GenerateAppearances(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<std::string>& fieldNames,
    const PdfFormAppearanceOptions& options) {

    PdfDocument document = PdfDocument::Open(inputPath);
    if (document.IsEncrypted()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature, "AcroForm appearance generation does not support encrypted PDFs yet.");
    }
    const auto nodes = readFieldNodes(document);
    IncrementalObjects revisions;
    std::uint32_t nextObject = nextObjectNumber(document);
    PdfFormAppearanceResult result{outputPath, 0U, 0U, 0U};

    for (const auto& node : nodes) {
        if (!fieldSelected(fieldNames, node.fullName)) continue;
        if (fieldType(node.fieldType) == PdfFormFieldType::Signature) continue;
        for (const auto& widgetReference : node.widgets) {
            const PdfDictionary* originalWidget = document.GetObject(widgetReference).AsDictionary();
            if (!originalWidget) continue;
            const auto rectangle = rectangleFromObject(originalWidget->Find(PdfName("Rect")));
            if (!rectangle || rectangle->width() <= 0.0 || rectangle->height() <= 0.0) continue;

            const PdfRectangle local{0.0, 0.0, rectangle->width(), rectangle->height()};
            PdfDictionary streamDictionary;
            streamDictionary.Put(PdfName("Type"), PdfObject(PdfName("XObject")));
            streamDictionary.Put(PdfName("Subtype"), PdfObject(PdfName("Form")));
            streamDictionary.Put(PdfName("FormType"), PdfObject(static_cast<std::int64_t>(1)));
            streamDictionary.Put(PdfName("BBox"), rectangleArray(local));
            streamDictionary.Put(PdfName("Resources"), PdfObject(formResources()));
            const std::string commands = appearanceCommands(node, local, options, false);
            const PdfReference appearanceReference{nextObject++, 0U};
            revisions[appearanceReference.objectNumber] = {
                appearanceReference,
                PdfObject(PdfStream(std::move(streamDictionary), bytesFromString(commands)))};

            PdfDictionary widget = deepCloneDictionary(*originalWidget);
            PdfDictionary appearance;
            if (const PdfDictionary* oldAppearance = widget.GetAsDictionary(PdfName("AP"))) {
                appearance = deepCloneDictionary(*oldAppearance);
            }
            appearance.Put(PdfName("N"), PdfObject::IndirectReference(appearanceReference.objectNumber));
            widget.Put(PdfName("AP"), PdfObject(std::move(appearance)));
            revisions[widgetReference.objectNumber] = {widgetReference, PdfObject(std::move(widget))};
            ++result.generatedAppearanceCount;
        }
    }

    if (result.generatedAppearanceCount > 0U) {
        if (const auto formReference = acroFormReference(document)) {
            if (const PdfDictionary* original = document.GetObject(*formReference).AsDictionary()) {
                PdfDictionary form = deepCloneDictionary(*original);
                form.Put(PdfName("NeedAppearances"), PdfObject(false));
                revisions[formReference->objectNumber] = {*formReference, PdfObject(std::move(form))};
            }
        }
        writeIncrementalObjects(inputPath, outputPath, document, revisions);
    } else {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        const std::string source = readFile(inputPath);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
    }
    return result;
}

PdfFormAppearanceResult PdfAcroForm::FlattenFields(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<std::string>& fieldNames,
    const PdfFormAppearanceOptions& options) {

    PdfDocument document = PdfDocument::Open(inputPath);
    if (document.IsEncrypted()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature, "AcroForm flattening does not support encrypted PDFs yet.");
    }
    const auto nodes = readFieldNodes(document);
    const auto pageMap = buildWidgetPageMap(document);
    IncrementalObjects revisions;
    std::uint32_t nextObject = nextObjectNumber(document);
    PdfFormAppearanceResult result{outputPath, 0U, 0U, 0U};
    std::unordered_set<std::uint64_t> removedWidgets;
    std::unordered_set<std::uint64_t> removedFields;
    std::map<std::size_t, std::string> pageCommands;

    for (const auto& node : nodes) {
        if (!fieldSelected(fieldNames, node.fullName)) continue;
        if (fieldType(node.fieldType) == PdfFormFieldType::Signature) continue;
        bool flattened = false;
        for (const auto& widgetReference : node.widgets) {
            const PdfDictionary* widget = document.GetObject(widgetReference).AsDictionary();
            if (!widget) continue;
            const auto rectangle = rectangleFromObject(widget->Find(PdfName("Rect")));
            if (!rectangle) continue;
            const std::uint64_t widgetKey = (static_cast<std::uint64_t>(widgetReference.objectNumber) << 16U) | widgetReference.generation;
            const auto page = pageMap.find(widgetKey);
            if (page == pageMap.end()) continue;
            pageCommands[page->second] += appearanceCommands(node, *rectangle, options, true);
            removedWidgets.insert(widgetKey);
            ++result.removedWidgetCount;
            flattened = true;
        }
        if (flattened) {
            removedFields.insert((static_cast<std::uint64_t>(node.reference.objectNumber) << 16U) | node.reference.generation);
            ++result.flattenedFieldCount;
        }
    }

    for (const auto& [pageIndex, commands] : pageCommands) {
        const PdfReference pageReference = document.GetPageReference(pageIndex);
        const PdfDictionary* originalPage = document.GetObject(pageReference).AsDictionary();
        if (!originalPage) continue;
        PdfDictionary page = deepCloneDictionary(*originalPage);
        page.Put(PdfName("Resources"), PdfObject(pageResourcesWithFormFont(document, *originalPage)));

        const PdfReference contentReference{nextObject++, 0U};
        PdfDictionary streamDictionary;
        revisions[contentReference.objectNumber] = {
            contentReference,
            PdfObject(PdfStream(std::move(streamDictionary), bytesFromString(commands)))};

        PdfArray contents;
        if (const PdfObject* oldContents = originalPage->Find(PdfName("Contents"))) {
            if (const PdfArray* array = oldContents->AsArray()) {
                for (const auto& value : array->values()) contents.push_back(deepClone(value));
            } else {
                contents.push_back(deepClone(*oldContents));
            }
        }
        contents.push_back(PdfObject::IndirectReference(contentReference.objectNumber));
        page.Put(PdfName("Contents"), PdfObject(std::move(contents)));

        PdfArray annotations;
        if (const PdfObject* annotsObject = originalPage->Find(PdfName("Annots"))) {
            const PdfArray* annots = annotsObject->AsArray();
            if (!annots) {
                if (const auto reference = annotsObject->AsReference()) {
                    annots = document.GetObject({reference->first, reference->second}).AsArray();
                }
            }
            if (annots) {
                for (const auto& annotation : annots->values()) {
                    const auto reference = annotation.AsReference();
                    const std::uint64_t key = reference ?
                        (static_cast<std::uint64_t>(reference->first) << 16U) | reference->second : 0U;
                    if (!reference || !removedWidgets.contains(key)) annotations.push_back(deepClone(annotation));
                }
            }
        }
        if (annotations.empty()) page.Remove(PdfName("Annots"));
        else page.Put(PdfName("Annots"), PdfObject(std::move(annotations)));
        revisions[pageReference.objectNumber] = {pageReference, PdfObject(std::move(page))};
    }

    if (const auto formReference = acroFormReference(document)) {
        if (const PdfDictionary* originalForm = document.GetObject(*formReference).AsDictionary()) {
            PdfDictionary form = deepCloneDictionary(*originalForm);
            PdfArray remaining;
            if (const PdfArray* fields = originalForm->GetAsArray(PdfName("Fields"))) {
                for (const auto& field : fields->values()) {
                    const auto reference = field.AsReference();
                    const std::uint64_t key = reference ?
                        (static_cast<std::uint64_t>(reference->first) << 16U) | reference->second : 0U;
                    if (!reference || !removedFields.contains(key)) remaining.push_back(deepClone(field));
                }
            }
            form.Put(PdfName("Fields"), PdfObject(std::move(remaining)));
            form.Put(PdfName("NeedAppearances"), PdfObject(false));
            revisions[formReference->objectNumber] = {*formReference, PdfObject(std::move(form))};
        }
    }

    if (!revisions.empty()) writeIncrementalObjects(inputPath, outputPath, document, revisions);
    else {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        const std::string source = readFile(inputPath);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
    }
    return result;
}

} // namespace CPPPdf

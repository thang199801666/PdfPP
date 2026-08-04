#include <CPPPdf/Forms/PdfAcroForm.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"
#include "Internal/Writer/PdfIncrementalWriter.hpp"

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
    if (const auto integer = value->AsInteger()) return std::to_string(*integer);
    if (const auto real = value->AsReal()) {
        std::ostringstream output;
        output << std::setprecision(12) << *real;
        return output.str();
    }
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

    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t size = Internal::PdfIncrementalWriter::NextObjectNumber(document);
    for (const auto& [number, revision] : revisions) {
        (void)number;
        const auto& [reference, dictionary] = revision;
        writer.WriteDictionary(reference, dictionary);
        size = std::max(size, reference.objectNumber + 1U);
    }
    writer.Finish(size);
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
    const bool absoluteCoordinates,
    const std::optional<bool> buttonSelected = std::nullopt) {
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
        const bool selected = buttonSelected.value_or(!value.empty() && value != "Off");
        if (selected) {
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
    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    for (const auto& [unused, entry] : objects) {
        (void)unused;
        const auto& [reference, object] = entry;
        writer.WriteObject(reference, object);
    }
    std::uint32_t size = Internal::PdfIncrementalWriter::NextObjectNumber(document);
    if (!objects.empty()) size = std::max(size, objects.rbegin()->first + 1U);
    writer.Finish(size);
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
        info.radio = info.type == PdfFormFieldType::Button && (info.flags & 32768U) != 0U;
        info.checked = info.type == PdfFormFieldType::Button && !info.value.empty() && info.value != "Off";
        if (info.type == PdfFormFieldType::Choice) {
            if (const PdfArray* selected = node.dictionary.Find(PdfName("I"))
                    ? node.dictionary.Find(PdfName("I"))->AsArray() : nullptr) {
                for (const auto& item : selected->values()) {
                    if (const auto index = item.AsInteger(); index && *index >= 0) {
                        info.selectedIndices.push_back(static_cast<std::size_t>(*index));
                    }
                }
            }
        }
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

std::vector<PdfFormFieldInfo> PdfAcroForm::GetFields(
    const std::filesystem::path& inputPath, const PdfReaderOptions& readerOptions) {
    const PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    return GetFields(document);
}

PdfFormUpdateResult PdfAcroForm::SetFieldValues(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<PdfFormFieldUpdate>& updates,
    const PdfFormUpdateOptions& options,
    const PdfReaderOptions& readerOptions) {

    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (document.IsEncrypted() && !document.IsOwnerPasswordAuthenticated() &&
        (static_cast<std::uint32_t>(document.GetPermissionBits()) & 256U) == 0U) {
        throw PdfException(PdfErrorCode::PermissionDenied,
                           "The user password does not permit form filling.");
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
            const bool radio = (node.flags & 32768U) != 0U;
            const bool checked = checkedValue(update.value);
            const std::string onState = node.widgets.empty() ? "Yes" : widgetOnState(document, node.widgets.front());
            revised.Put(PdfName("V"), PdfObject(PdfName(
                radio ? (update.value == "Off" ? "Off" : update.value) : (checked ? onState : "Off"))));
            for (const auto& widgetReference : node.widgets) {
                const PdfDictionary* originalWidget = document.GetObject(widgetReference).AsDictionary();
                if (!originalWidget) continue;
                PdfDictionary widget = deepCloneDictionary(*originalWidget);
                const std::string widgetState = radio
                    ? (update.value != "Off" && widgetOnState(document, widgetReference) == update.value
                        ? update.value : "Off")
                    : (checked ? onState : "Off");
                widget.Put(PdfName("AS"), PdfObject(PdfName(widgetState)));
                revisions[widgetReference.objectNumber] = {widgetReference, std::move(widget)};
                ++result.updatedWidgetCount;
            }
        } else if (type == PdfFormFieldType::Choice && !update.selections.empty()) {
            if ((node.flags & (1U << 21U)) == 0U) {
                throw PdfException(PdfErrorCode::InvalidArgument,
                                   "Multiple selections require a multi-select choice field.");
            }
            PdfArray values;
            PdfArray indices;
            const auto optionsList = parseOptions(node.dictionary.Find(PdfName("Opt")));
            for (const auto& selection : update.selections) {
                const auto option = std::find(optionsList.begin(), optionsList.end(), selection);
                if (option == optionsList.end()) {
                    throw PdfException(PdfErrorCode::InvalidArgument, "Choice option not found: " + selection);
                }
                values.push_back(PdfObject(selection));
                indices.push_back(PdfObject(static_cast<std::int64_t>(option - optionsList.begin())));
            }
            revised.Put(PdfName("V"), PdfObject(std::move(values)));
            revised.Put(PdfName("I"), PdfObject(std::move(indices)));
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
    const PdfFormAppearanceOptions& options,
    const PdfReaderOptions& readerOptions) {

    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (document.IsEncrypted() && !document.IsOwnerPasswordAuthenticated() &&
        (static_cast<std::uint32_t>(document.GetPermissionBits()) & 256U) == 0U) {
        throw PdfException(PdfErrorCode::PermissionDenied,
                           "The user password does not permit form appearance updates.");
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
            const std::optional<bool> selected = fieldType(node.fieldType) == PdfFormFieldType::Button
                ? std::optional<bool>(objectTextValue(&node.inheritedValue) != "Off" &&
                    objectTextValue(&node.inheritedValue) == widgetOnState(document, widgetReference))
                : std::nullopt;
            const std::string commands = appearanceCommands(node, local, options, false, selected);
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
    const PdfFormAppearanceOptions& options,
    const PdfReaderOptions& readerOptions) {

    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (document.IsEncrypted() && !document.IsOwnerPasswordAuthenticated() &&
        (static_cast<std::uint32_t>(document.GetPermissionBits()) & 256U) == 0U) {
        throw PdfException(PdfErrorCode::PermissionDenied,
                           "The user password does not permit form flattening.");
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

namespace {

std::string trimWhitespace(std::string_view value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (begin >= end) return {};
    return std::string(begin, end);
}

// Minimal arithmetic evaluator for AcroForm calculation scripts. Supports the
// common `+ - * /` operators, parentheses, numeric literals, and references to
// other fields by name (resolved to their numeric value). Returns std::nullopt
// when the expression cannot be parsed.
class CalcEvaluator final {
public:
    explicit CalcEvaluator(const std::map<std::string, double>& values) : values_(values) {}

    std::optional<double> Evaluate(std::string_view expression) {
        pos_ = 0;
        expression_ = expression;
        const auto result = ParseAddSub();
        if (result && pos_ == expression_.size()) return result;
        return std::nullopt;
    }

private:
    std::string_view expression_;
    std::size_t pos_{};
    const std::map<std::string, double>& values_;

    void SkipSpace() { while (pos_ < expression_.size() && expression_[pos_] == ' ') ++pos_; }

    std::optional<double> ParseAddSub() {
        auto left = ParseMulDiv();
        if (!left) return std::nullopt;
        while (true) {
            SkipSpace();
            if (pos_ >= expression_.size()) return left;
            const char op = expression_[pos_];
            if (op != '+' && op != '-') return left;
            ++pos_;
            auto right = ParseMulDiv();
            if (!right) return std::nullopt;
            if (op == '+') *left += *right;
            else *left -= *right;
        }
    }

    std::optional<double> ParseMulDiv() {
        auto left = ParseUnary();
        if (!left) return std::nullopt;
        while (true) {
            SkipSpace();
            if (pos_ >= expression_.size()) return left;
            const char op = expression_[pos_];
            if (op != '*' && op != '/') return left;
            ++pos_;
            auto right = ParseUnary();
            if (!right) return std::nullopt;
            if (op == '*') *left *= *right;
            else {
                if (*right == 0.0) return std::nullopt;
                *left /= *right;
            }
        }
    }

    std::optional<double> ParseUnary() {
        SkipSpace();
        if (pos_ < expression_.size() && (expression_[pos_] == '-' || expression_[pos_] == '+')) {
            const bool negate = expression_[pos_] == '-';
            ++pos_;
            auto value = ParseUnary();
            if (!value) return std::nullopt;
            return negate ? -*value : *value;
        }
        return ParsePrimary();
    }

    std::optional<double> ParsePrimary() {
        SkipSpace();
        if (pos_ >= expression_.size()) return std::nullopt;
        const char ch = expression_[pos_];
        if (ch == '(') {
            ++pos_;
            auto inner = ParseAddSub();
            SkipSpace();
            if (!inner || pos_ >= expression_.size() || expression_[pos_] != ')') return std::nullopt;
            ++pos_;
            return inner;
        }
        if (ch >= '0' && ch <= '9') {
            const auto begin = pos_;
            while (pos_ < expression_.size() && ((expression_[pos_] >= '0' && expression_[pos_] <= '9') ||
                   expression_[pos_] == '.')) ++pos_;
            const std::string token(expression_.substr(begin, pos_ - begin));
            try {
                return std::stod(token);
            } catch (...) {
                return std::nullopt;
            }
        }
        // Field reference: [A-Za-z_.]+ resolved to a numeric value.
        const auto begin = pos_;
        while (pos_ < expression_.size() &&
               (std::isalnum(static_cast<unsigned char>(expression_[pos_])) || expression_[pos_] == '_' ||
                expression_[pos_] == '.')) ++pos_;
        if (pos_ == begin) return std::nullopt;
        const std::string name(expression_.substr(begin, pos_ - begin));
        const auto found = values_.find(name);
        if (found == values_.end()) return std::nullopt;
        return found->second;
    }
};

double numericFieldValue(const PdfObject& value) {
    if (const auto real = value.AsReal()) return *real;
    if (const auto integer = value.AsInteger()) return static_cast<double>(*integer);
    if (const std::string* text = value.AsString()) {
        try { return std::stod(*text); } catch (...) { return 0.0; }
    }
    return 0.0;
}

// Extracts the calculation script (`/AA /C`, a JavaScript string) from a field.
std::string fieldCalcScript(const PdfDocument& document, const FieldNode& node) {
    const PdfObject& raw = document.GetObject(node.reference);
    const PdfDictionary* dictionary = raw.AsDictionary();
    if (!dictionary) dictionary = &node.dictionary;
    const PdfObject* aaObject = dictionary->Find(PdfName("AA"));
    const PdfDictionary* aa = aaObject ? resolveDictionary(document, *aaObject) : nullptr;
    if (!aa) return {};
    const PdfObject* calcObject = aa->Find(PdfName("C"));
    if (const PdfDictionary* calc = calcObject ? resolveDictionary(document, *calcObject) : nullptr) {
        if (const std::string* js = calc->Find(PdfName("JS")) ?
            calc->Find(PdfName("JS"))->AsString() : nullptr) {
            return *js;
        }
    }
    if (const std::string* js = calcObject ? calcObject->AsString() : nullptr) return *js;
    return {};
}

} // namespace

PdfFormCalcResult PdfAcroForm::CalculateFields(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    const auto nodes = readFieldNodes(document);

    // Collect current numeric values by full field name.
    std::map<std::string, double> currentValues;
    for (const auto& node : nodes) {
        currentValues[node.fullName] = numericFieldValue(node.inheritedValue);
    }

    PdfFormCalcResult result{outputPath, 0U, {}};
    // Fields with a `/AA /C` script are computed fields; their names are the
    // ones listed in the script (simplified: the script's leftmost identifier
    // that matches no other field is the target). Evaluate each script.
    std::vector<FieldNode> computed;
    for (const auto& node : nodes) {
        const std::string script = fieldCalcScript(document, node);
        if (!script.empty()) {
            computed.push_back(node);
        }
    }

    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t nextObject = Internal::PdfIncrementalWriter::NextObjectNumber(document);
    for (const auto& node : computed) {
        const std::string script = fieldCalcScript(document, node);
        // The target field name is the script text before the first `=`.
        const auto eq = script.find('=');
        const std::string targetName = eq == std::string::npos ? node.fullName : trimWhitespace(script.substr(0, eq));
        const std::string expression = eq == std::string::npos ? script : script.substr(eq + 1U);
        CalcEvaluator evaluator(currentValues);
        const auto value = evaluator.Evaluate(expression);
        if (!value) continue;
        PdfDictionary updated = deepCloneDictionary(node.dictionary);
        updated.Put(PdfName("V"), PdfObject(*value));
        writer.WriteDictionary(node.reference, updated);
        ++result.calculatedFieldCount;
        result.updatedFields.push_back(targetName);
        currentValues[targetName] = *value;
    }
    if (!computed.empty()) writer.Finish(nextObject);
    else {
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        const std::string source = readFile(inputPath);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
    }
    return result;
}

} // namespace CPPPdf

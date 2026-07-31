#include <CPPPdf/Document/PdfPageImporter.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Document/PdfPage.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace CPPPdf {
namespace {

struct SourceReferenceKey final {
    std::size_t sourceIndex{};
    std::uint32_t objectNumber{};
    std::uint16_t generation{};

    friend bool operator==(const SourceReferenceKey&, const SourceReferenceKey&) = default;
};

struct SourceReferenceHash final {
    std::size_t operator()(const SourceReferenceKey& key) const noexcept {
        std::size_t hash = std::hash<std::size_t>{}(key.sourceIndex);
        hash ^= std::hash<std::uint32_t>{}(key.objectNumber) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        hash ^= std::hash<std::uint16_t>{}(key.generation) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

PdfObject rectangleObject(const PdfRectangle& rectangle) {
    PdfArray array;
    array.push_back(PdfObject(rectangle.left));
    array.push_back(PdfObject(rectangle.bottom));
    array.push_back(PdfObject(rectangle.right));
    array.push_back(PdfObject(rectangle.top));
    return PdfObject(std::move(array));
}

class ObjectGraphImporter final {
public:
    explicit ObjectGraphImporter(std::vector<std::unique_ptr<PdfDocument>>& documents)
        : documents_(documents) {}

    [[nodiscard]] PdfReference AllocatePage(const std::size_t sourceIndex, const std::size_t pageIndex) {
        const PdfReference sourceReference = documents_.at(sourceIndex)->GetPageReference(pageIndex);
        const SourceReferenceKey key{sourceIndex, sourceReference.objectNumber, sourceReference.generation};
        if (const auto found = references_.find(key); found != references_.end()) return found->second;
        const PdfReference destination{nextObjectNumber_++, 0U};
        references_.emplace(key, destination);
        pendingPages_.emplace_back(sourceIndex, pageIndex, sourceReference, destination);
        return destination;
    }

    void ImportPendingPages(const PdfReference& pagesRoot) {
        for (const auto& pending : pendingPages_) {
            const auto [sourceIndex, pageIndex, sourceReference, destination] = pending;
            const PdfObject& sourceObject = documents_.at(sourceIndex)->GetObject(sourceReference);
            const PdfDictionary* sourcePage = sourceObject.AsDictionary();
            if (!sourcePage) throw PdfException(PdfErrorCode::InvalidPageTree, "Imported page is not a dictionary.");

            PdfDictionary page;
            for (const auto& [key, value] : sourcePage->values()) {
                if (key == PdfName("Parent")) continue;
                page.Put(key, CloneObject(sourceIndex, value));
            }

            const PdfPage pageView = documents_.at(sourceIndex)->GetPage(pageIndex);
            if (!page.Contains(PdfName("MediaBox"))) page.Put(PdfName("MediaBox"), rectangleObject(pageView.GetMediaBox()));
            if (!page.Contains(PdfName("CropBox"))) page.Put(PdfName("CropBox"), rectangleObject(pageView.GetCropBox()));
            if (!page.Contains(PdfName("Rotate")) && pageView.GetRotation() != 0) {
                page.Put(PdfName("Rotate"), PdfObject(static_cast<std::int64_t>(pageView.GetRotation())));
            }
            if (!page.Contains(PdfName("Resources")) && !pageView.GetResourcesDictionary().empty()) {
                const PdfObject resources = Internal::PdfObjectParser::Parse(pageView.GetResourcesDictionary(), 256U);
                page.Put(PdfName("Resources"), CloneObject(sourceIndex, resources));
            }
            page.Put(PdfName("Parent"), PdfObject::IndirectReference(pagesRoot.objectNumber, pagesRoot.generation));
            objects_.emplace(destination.objectNumber, PdfObject(std::move(page)));
        }
    }

    [[nodiscard]] std::optional<PdfObject> CloneCatalogEntry(
        const std::size_t sourceIndex,
        const PdfName& key) {
        const PdfReference catalogReference = documents_.at(sourceIndex)->GetCatalogReference();
        const PdfDictionary* catalog = documents_.at(sourceIndex)->GetObject(catalogReference).AsDictionary();
        if (!catalog) throw PdfException(PdfErrorCode::MalformedObject, "Source catalog is not a dictionary.");
        if (!catalog->Contains(key)) return std::nullopt;
        return CloneObject(sourceIndex, catalog->Get(key));
    }

    [[nodiscard]] std::optional<PdfReference> ImportTrailerReference(
        const std::size_t sourceIndex,
        const PdfName& key) {
        const auto sourceReference = documents_.at(sourceIndex)->GetTrailerReference(key);
        if (!sourceReference) return std::nullopt;
        return ImportReference(sourceIndex, *sourceReference);
    }

    [[nodiscard]] PdfReference ImportObjectReference(
        const std::size_t sourceIndex,
        const PdfReference& sourceReference) {
        return ImportReference(sourceIndex, sourceReference);
    }

    [[nodiscard]] PdfObject CloneStandaloneObject(
        const std::size_t sourceIndex,
        const PdfObject& source) {
        return CloneObject(sourceIndex, source);
    }

    [[nodiscard]] PdfReference AddObject(PdfObject object) {
        const PdfReference destination{nextObjectNumber_++, 0U};
        objects_.emplace(destination.objectNumber, std::move(object));
        return destination;
    }

    [[nodiscard]] PdfObject* FindMutableObject(const PdfReference& reference) noexcept {
        const auto found = objects_.find(reference.objectNumber);
        return found == objects_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const std::map<std::uint32_t, PdfObject>& Objects() const noexcept { return objects_; }
    [[nodiscard]] std::size_t ImportedObjectCount() const noexcept { return objects_.size(); }

private:
    [[nodiscard]] PdfObject CloneObject(const std::size_t sourceIndex, const PdfObject& source) {
        switch (source.type()) {
        case PdfObjectType::Null: return PdfObject();
        case PdfObjectType::Boolean: return PdfObject(*source.AsBoolean());
        case PdfObjectType::Integer: return PdfObject(*source.AsInteger());
        case PdfObjectType::Real: return PdfObject(*source.AsReal());
        case PdfObjectType::Name: return PdfObject(*source.AsName());
        case PdfObjectType::String: return PdfObject(*source.AsString());
        case PdfObjectType::Array: {
            PdfArray array;
            for (const auto& value : source.AsArray()->values()) array.push_back(CloneObject(sourceIndex, value));
            return PdfObject(std::move(array));
        }
        case PdfObjectType::Dictionary: {
            PdfDictionary dictionary;
            for (const auto& [key, value] : source.AsDictionary()->values()) {
                dictionary.Put(key, CloneObject(sourceIndex, value));
            }
            return PdfObject(std::move(dictionary));
        }
        case PdfObjectType::Stream: {
            const PdfStream& stream = *source.AsStream();
            PdfDictionary dictionary;
            for (const auto& [key, value] : stream.dictionary().values()) {
                if (key == PdfName("Length")) continue;
                dictionary.Put(key, CloneObject(sourceIndex, value));
            }
            const auto bytes = stream.bytes();
            return PdfObject(PdfStream(std::move(dictionary), std::vector<std::byte>(bytes.begin(), bytes.end())));
        }
        case PdfObjectType::IndirectReference: {
            const auto [number, generation] = *source.AsReference();
            const PdfReference imported = ImportReference(sourceIndex, {number, generation});
            return PdfObject::IndirectReference(imported.objectNumber, imported.generation);
        }
        }
        throw PdfException(PdfErrorCode::MalformedObject, "Unsupported PDF object type during import.");
    }

    [[nodiscard]] PdfReference ImportReference(const std::size_t sourceIndex, const PdfReference& sourceReference) {
        const SourceReferenceKey key{sourceIndex, sourceReference.objectNumber, sourceReference.generation};
        if (const auto found = references_.find(key); found != references_.end()) return found->second;

        const PdfReference destination{nextObjectNumber_++, 0U};
        references_.emplace(key, destination); // Allocate before recursion to break cycles.
        const PdfObject& sourceObject = documents_.at(sourceIndex)->GetObject(sourceReference);
        objects_.emplace(destination.objectNumber, CloneObject(sourceIndex, sourceObject));
        return destination;
    }

    std::vector<std::unique_ptr<PdfDocument>>& documents_;
    std::uint32_t nextObjectNumber_{3U};
    std::unordered_map<SourceReferenceKey, PdfReference, SourceReferenceHash> references_;
    std::vector<std::tuple<std::size_t, std::size_t, PdfReference, PdfReference>> pendingPages_;
    std::map<std::uint32_t, PdfObject> objects_;
};

bool importsAllPagesInOriginalOrder(
    const PdfPageImportSource& source,
    const PdfDocument& document) {
    if (source.pageIndices.empty()) return true;
    if (source.pageIndices.size() != document.GetPageCount()) return false;
    for (std::size_t index = 0; index < source.pageIndices.size(); ++index) {
        if (source.pageIndices[index] != index) return false;
    }
    return true;
}

struct PreservedDocumentStructures final {
    std::vector<std::pair<PdfName, PdfObject>> catalogEntries;
    std::optional<PdfReference> infoReference;
    bool preservedAcroForm{};
    std::size_t importedFormFieldCount{};
};

const PdfDictionary* resolveDictionary(
    const PdfDocument& document,
    const PdfObject& object) {
    if (const PdfDictionary* dictionary = object.AsDictionary()) return dictionary;
    if (const auto reference = object.AsReference()) {
        return document.GetObject({reference->first, reference->second}).AsDictionary();
    }
    return nullptr;
}

std::string fieldName(const PdfDocument& document, const PdfObject& fieldObject) {
    const PdfDictionary* field = resolveDictionary(document, fieldObject);
    if (!field) return {};
    if (const PdfObject* name = field->Find(PdfName("T"))) {
        if (const std::string* text = name->AsString()) return *text;
    }
    return {};
}

void renameImportedField(
    ObjectGraphImporter& importer,
    const PdfReference& reference,
    const std::string& name) {
    PdfObject* object = importer.FindMutableObject(reference);
    if (!object) return;
    PdfDictionary* dictionary = const_cast<PdfDictionary*>(object->AsDictionary());
    if (dictionary) dictionary->Put(PdfName("T"), PdfObject(name));
}

std::optional<PdfObject> buildMergedAcroForm(
    const std::vector<PdfPageImportSource>& sources,
    const std::vector<std::unique_ptr<PdfDocument>>& documents,
    ObjectGraphImporter& importer,
    const PdfPageImportOptions& options,
    std::size_t& importedFieldCount) {
    if (!options.preserveAcroForm) return std::nullopt;

    PdfArray mergedFields;
    PdfDictionary mergedForm;
    std::set<std::string> usedNames;
    bool foundAnyForm = false;
    bool copiedDefaults = false;

    for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
        if (!importsAllPagesInOriginalOrder(sources[sourceIndex], *documents[sourceIndex])) continue;

        const PdfDictionary* catalog = documents[sourceIndex]->GetObject(
            documents[sourceIndex]->GetCatalogReference()).AsDictionary();
        if (!catalog) continue;
        const PdfObject* formObject = catalog->Find(PdfName("AcroForm"));
        if (!formObject) continue;
        const PdfDictionary* sourceForm = resolveDictionary(*documents[sourceIndex], *formObject);
        if (!sourceForm) continue;
        const PdfArray* fields = sourceForm->GetAsArray(PdfName("Fields"));
        if (!fields) continue;

        foundAnyForm = true;
        if (!copiedDefaults) {
            for (const char* keyName : {"DR", "DA", "Q", "NeedAppearances", "SigFlags"}) {
                const PdfName key(keyName);
                if (const PdfObject* value = sourceForm->Find(key)) {
                    mergedForm.Put(key, importer.CloneStandaloneObject(sourceIndex, *value));
                }
            }
            copiedDefaults = true;
        }

        for (const PdfObject& fieldObject : fields->values()) {
            PdfReference importedReference{};
            if (const auto reference = fieldObject.AsReference()) {
                importedReference = importer.ImportObjectReference(
                    sourceIndex, {reference->first, reference->second});
            } else {
                importedReference = importer.AddObject(
                    importer.CloneStandaloneObject(sourceIndex, fieldObject));
            }

            std::string name = fieldName(*documents[sourceIndex], fieldObject);
            if (!name.empty() && usedNames.contains(name)) {
                switch (options.formFieldConflictPolicy) {
                case PdfFormFieldConflictPolicy::Error:
                    throw PdfException(PdfErrorCode::InvalidArgument,
                        "Duplicate AcroForm field name while merging: " + name);
                case PdfFormFieldConflictPolicy::RenameWithSourceIndex: {
                    const std::string base = "Source" + std::to_string(sourceIndex + 1U) + "." + name;
                    std::string candidate = base;
                    std::size_t suffix = 2U;
                    while (usedNames.contains(candidate)) {
                        candidate = base + "." + std::to_string(suffix++);
                    }
                    renameImportedField(importer, importedReference, candidate);
                    name = std::move(candidate);
                    break;
                }
                case PdfFormFieldConflictPolicy::KeepDuplicate:
                    break;
                }
            }
            if (!name.empty()) usedNames.insert(name);
            mergedFields.push_back(PdfObject::IndirectReference(
                importedReference.objectNumber, importedReference.generation));
            ++importedFieldCount;
        }
    }

    if (!foundAnyForm || mergedFields.empty()) return std::nullopt;
    mergedForm.Put(PdfName("Fields"), PdfObject(std::move(mergedFields)));
    return PdfObject(std::move(mergedForm));
}

PreservedDocumentStructures preserveDocumentStructures(
    const std::vector<PdfPageImportSource>& sources,
    const std::vector<std::unique_ptr<PdfDocument>>& documents,
    ObjectGraphImporter& importer,
    const PdfPageImportOptions& options) {
    PreservedDocumentStructures structures;
    if (sources.empty()) return structures;

    const bool importsCompleteFirstDocument = importsAllPagesInOriginalOrder(sources.front(), *documents.front());
    auto copyCatalogEntry = [&](const char* name) {
        if (auto value = importer.CloneCatalogEntry(0U, PdfName(name))) {
            structures.catalogEntries.emplace_back(PdfName(name), std::move(*value));
        }
    };

    if (options.preserveMetadataStream) copyCatalogEntry("Metadata");
    if (options.preservePageModeAndLayout) {
        copyCatalogEntry("PageMode");
        copyCatalogEntry("PageLayout");
        copyCatalogEntry("Lang");
        copyCatalogEntry("ViewerPreferences");
    }
    if (importsCompleteFirstDocument) {
        if (options.preserveOutlines) copyCatalogEntry("Outlines");
        if (options.preserveNamedDestinations) {
            copyCatalogEntry("Names");
            copyCatalogEntry("Dests");
        }
    }
    if (options.preserveDocumentInfo) {
        structures.infoReference = importer.ImportTrailerReference(0U, PdfName("Info"));
    }
    if (auto acroForm = buildMergedAcroForm(
            sources, documents, importer, options, structures.importedFormFieldCount)) {
        const PdfReference formReference = importer.AddObject(std::move(*acroForm));
        structures.catalogEntries.emplace_back(
            PdfName("AcroForm"),
            PdfObject::IndirectReference(formReference.objectNumber, formReference.generation));
        structures.preservedAcroForm = true;
    }
    return structures;
}

void writeFreshDocument(
    const std::filesystem::path& outputPath,
    const std::vector<PdfReference>& pages,
    ObjectGraphImporter& importer,
    const PreservedDocumentStructures& preserved) {
    const PdfReference pagesReference{2U, 0U};
    importer.ImportPendingPages(pagesReference);

    PdfDictionary catalog;
    catalog.Put(PdfName::Type, PdfObject(PdfName("Catalog")));
    catalog.Put(PdfName::Pages, PdfObject::IndirectReference(2U, 0U));
    for (const auto& [key, value] : preserved.catalogEntries) catalog.Put(key, value);

    PdfArray kids;
    for (const PdfReference& page : pages) kids.push_back(PdfObject::IndirectReference(page.objectNumber, page.generation));
    PdfDictionary pagesRoot;
    pagesRoot.Put(PdfName::Type, PdfObject(PdfName::Pages));
    pagesRoot.Put(PdfName("Kids"), PdfObject(std::move(kids)));
    pagesRoot.Put(PdfName("Count"), PdfObject(static_cast<std::int64_t>(pages.size())));

    std::map<std::uint32_t, PdfObject> objects = importer.Objects();
    objects.insert_or_assign(1U, PdfObject(std::move(catalog)));
    objects.insert_or_assign(2U, PdfObject(std::move(pagesRoot)));

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot create merged PDF: " + outputPath.string());
    output << "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";

    const std::uint32_t maximumObject = objects.rbegin()->first;
    std::vector<std::uint64_t> offsets(static_cast<std::size_t>(maximumObject) + 1U, 0U);
    for (const auto& [number, object] : objects) {
        offsets[number] = static_cast<std::uint64_t>(output.tellp());
        output << number << " 0 obj\n";
        Internal::PdfObjectSerializer::WriteObject(output, object);
        output << "\nendobj\n";
    }

    const auto xrefOffset = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n0 " << (maximumObject + 1U) << "\n";
    output << "0000000000 65535 f \n";
    for (std::uint32_t number = 1U; number <= maximumObject; ++number) {
        if (offsets[number] == 0U) {
            output << "0000000000 00000 f \n";
        } else {
            output << std::setw(10) << std::setfill('0') << offsets[number] << " 00000 n \n";
        }
    }
    output << "trailer\n<< /Size " << (maximumObject + 1U) << " /Root 1 0 R";
    if (preserved.infoReference) {
        output << " /Info " << preserved.infoReference->objectNumber << ' '
               << preserved.infoReference->generation << " R";
    }
    output << " >>\nstartxref\n" << xrefOffset << "\n%%EOF\n";
}

} // namespace

PdfPageImportResult PdfPageImporter::MergeDocuments(
    const std::vector<std::filesystem::path>& inputPaths,
    const std::filesystem::path& outputPath,
    const PdfPageImportOptions& options) {
    if (inputPaths.empty()) throw PdfException(PdfErrorCode::InvalidArgument, "At least one input PDF is required.");
    std::vector<PdfPageImportSource> sources;
    sources.reserve(inputPaths.size());
    for (const auto& path : inputPaths) sources.push_back({path, {}});
    return CopyPages(sources, outputPath, options);
}

PdfPageImportResult PdfPageImporter::CopyPages(
    const std::vector<PdfPageImportSource>& sources,
    const std::filesystem::path& outputPath,
    const PdfPageImportOptions& options) {
    if (sources.empty()) throw PdfException(PdfErrorCode::InvalidArgument, "At least one page source is required.");

    std::vector<std::unique_ptr<PdfDocument>> documents;
    documents.reserve(sources.size());
    for (const auto& source : sources) {
        auto document = std::make_unique<PdfDocument>(PdfDocument::Open(source.inputPath));
        if (document->IsEncrypted()) {
            throw PdfException(PdfErrorCode::UnsupportedFeature, "Encrypted PDFs cannot currently be merged.");
        }
        documents.push_back(std::move(document));
    }

    ObjectGraphImporter importer(documents);
    std::vector<PdfReference> outputPages;
    for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
        std::vector<std::size_t> indices = sources[sourceIndex].pageIndices;
        if (indices.empty()) {
            indices.resize(documents[sourceIndex]->GetPageCount());
            for (std::size_t index = 0; index < indices.size(); ++index) indices[index] = index;
        }
        for (const std::size_t pageIndex : indices) {
            if (pageIndex >= documents[sourceIndex]->GetPageCount()) {
                throw PdfException(PdfErrorCode::InvalidArgument, "Imported page index is out of range.");
            }
            outputPages.push_back(importer.AllocatePage(sourceIndex, pageIndex));
        }
    }
    if (outputPages.empty()) throw PdfException(PdfErrorCode::InvalidArgument, "No pages were selected for import.");

    const PreservedDocumentStructures preserved =
        preserveDocumentStructures(sources, documents, importer, options);
    writeFreshDocument(outputPath, outputPages, importer, preserved);
    return {
        outputPath,
        sources.size(),
        outputPages.size(),
        importer.ImportedObjectCount(),
        preserved.catalogEntries.size(),
        preserved.infoReference.has_value(),
        preserved.preservedAcroForm,
        preserved.importedFormFieldCount};
}

} // namespace CPPPdf

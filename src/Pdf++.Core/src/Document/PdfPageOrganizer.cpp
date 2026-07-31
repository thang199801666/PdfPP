#include <CPPPdf/Document/PdfPageOrganizer.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Document/PdfPage.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string>

namespace CPPPdf {
namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open input PDF: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

PdfDictionary parseTrailer(const PdfDocument& document) {
    const PdfObject object = Internal::PdfObjectParser::Parse(document.trailerDictionary(), 256U);
    const auto* dictionary = object.AsDictionary();
    if (!dictionary) throw PdfException(PdfErrorCode::MalformedXref, "Trailer is not a dictionary.");
    return *dictionary;
}

PdfReference referenceValue(const PdfDictionary& dictionary, const PdfName& key) {
    const PdfObject* value = dictionary.Find(key);
    if (!value) throw PdfException(PdfErrorCode::MalformedObject, "Missing required reference: " + key.value());
    const auto ref = value->AsReference();
    if (!ref) throw PdfException(PdfErrorCode::MalformedObject, "Expected indirect reference: " + key.value());
    return {ref->first, ref->second};
}

PdfReference pagesRootReference(const PdfDocument& document, const PdfDictionary& trailer) {
    const PdfReference root = referenceValue(trailer, PdfName::Root);
    const PdfObject& catalogObject = document.GetObject(root);
    const auto* catalog = catalogObject.AsDictionary();
    if (!catalog) throw PdfException(PdfErrorCode::MalformedObject, "Catalog is not a dictionary.");
    return referenceValue(*catalog, PdfName::Pages);
}

PdfDictionary cloneDictionary(const PdfDictionary& source) {
    PdfDictionary result;
    for (const auto& [key, value] : source.values()) result.Put(key, value);
    return result;
}

std::uint32_t trailerSize(const PdfDictionary& trailer, const PdfDocument& document) {
    if (const PdfObject* size = trailer.Find(PdfName("Size"))) {
        if (const auto integer = size->AsInteger(); integer && *integer > 0) {
            return static_cast<std::uint32_t>(*integer);
        }
    }
    std::uint32_t maximum = 0U;
    for (const auto number : document.objectNumbers()) maximum = std::max(maximum, number);
    return maximum + 1U;
}


PdfObject rectangleObject(const PdfRectangle& rectangle) {
    PdfArray values;
    values.push_back(PdfObject(rectangle.left));
    values.push_back(PdfObject(rectangle.bottom));
    values.push_back(PdfObject(rectangle.right));
    values.push_back(PdfObject(rectangle.top));
    return PdfObject(std::move(values));
}

std::size_t pageIndexForReference(const PdfDocument& document, const PdfReference& reference) {
    for (std::size_t index = 0; index < document.GetPageCount(); ++index) {
        const PdfReference candidate = document.GetPageReference(index);
        if (candidate.objectNumber == reference.objectNumber && candidate.generation == reference.generation) {
            return index;
        }
    }
    throw PdfException(PdfErrorCode::InvalidPageTree, "Selected page reference is not present in the page tree.");
}

void materializeInheritedPageProperties(
    const PdfDocument& document,
    const PdfReference& pageReference,
    PdfDictionary& revisedPage) {

    const std::size_t pageIndex = pageIndexForReference(document, pageReference);
    const PdfPage page = document.GetPage(pageIndex);

    if (!revisedPage.Contains(PdfName("MediaBox"))) {
        revisedPage.Put(PdfName("MediaBox"), rectangleObject(page.GetMediaBox()));
    }
    if (!revisedPage.Contains(PdfName("CropBox"))) {
        revisedPage.Put(PdfName("CropBox"), rectangleObject(page.GetCropBox()));
    }
    if (!revisedPage.Contains(PdfName("Rotate")) && page.GetRotation() != 0) {
        revisedPage.Put(PdfName("Rotate"), PdfObject(static_cast<std::int64_t>(page.GetRotation())));
    }
    if (!revisedPage.Contains(PdfName("Resources")) && !page.GetResourcesDictionary().empty()) {
        const PdfObject resources = Internal::PdfObjectParser::Parse(page.GetResourcesDictionary(), 256U);
        if (resources.AsDictionary()) {
            revisedPage.Put(PdfName("Resources"), resources);
        }
    }
}

void writeXrefEntries(
    std::ostream& output,
    const std::vector<std::pair<PdfReference, std::uint64_t>>& entries) {

    if (entries.empty()) return;
    std::size_t index = 0U;
    while (index < entries.size()) {
        const std::size_t begin = index;
        std::uint32_t expected = entries[index].first.objectNumber;
        while (index < entries.size() && entries[index].first.objectNumber == expected) {
            ++index;
            ++expected;
        }
        output << entries[begin].first.objectNumber << ' ' << (index - begin) << "\n";
        for (std::size_t i = begin; i < index; ++i) {
            output << std::setw(10) << std::setfill('0') << entries[i].second << ' '
                   << std::setw(5) << std::setfill('0') << entries[i].first.generation << " n \n";
        }
    }
}

void writeIncrementalPageTree(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfDocument& document,
    PdfDictionary trailer,
    const PdfReference& pagesRoot,
    const std::vector<PdfReference>& selectedPages) {

    const PdfObject& rootObject = document.GetObject(pagesRoot);
    const auto* originalRoot = rootObject.AsDictionary();
    if (!originalRoot) throw PdfException(PdfErrorCode::InvalidPageTree, "Pages root is not a dictionary.");

    PdfDictionary revisedRoot = cloneDictionary(*originalRoot);
    PdfArray kids;
    for (const auto& page : selectedPages) {
        kids.push_back(PdfObject::IndirectReference(page.objectNumber, page.generation));
    }
    revisedRoot.Put(PdfName("Kids"), PdfObject(std::move(kids)));
    revisedRoot.Put(PdfName("Count"), PdfObject(static_cast<std::int64_t>(selectedPages.size())));
    revisedRoot.Remove(PdfName("Parent"));

    const std::string source = readFile(inputPath);
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot create output PDF: " + outputPath.string());
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!source.empty() && source.back() != '\n') output << '\n';

    std::vector<std::pair<PdfReference, std::uint64_t>> entries;
    entries.reserve(selectedPages.size() + 1U);

    auto writeDictionaryRevision = [&](const PdfReference& reference, const PdfDictionary& dictionary) {
        const auto offset = static_cast<std::uint64_t>(output.tellp());
        output << reference.objectNumber << ' ' << reference.generation << " obj\n";
        Internal::PdfObjectSerializer::WriteDictionary(output, dictionary);
        output << "\nendobj\n";
        entries.emplace_back(reference, offset);
    };

    writeDictionaryRevision(pagesRoot, revisedRoot);

    for (const auto& pageReference : selectedPages) {
        const PdfObject& pageObject = document.GetObject(pageReference);
        const auto* originalPage = pageObject.AsDictionary();
        if (!originalPage) {
            throw PdfException(PdfErrorCode::InvalidPageTree, "Selected page is not a dictionary.");
        }
        PdfDictionary revisedPage = cloneDictionary(*originalPage);
        materializeInheritedPageProperties(document, pageReference, revisedPage);
        revisedPage.Put(
            PdfName("Parent"),
            PdfObject::IndirectReference(pagesRoot.objectNumber, pagesRoot.generation));
        writeDictionaryRevision(pageReference, revisedPage);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.first.objectNumber < right.first.objectNumber;
    });

    const auto xrefOffset = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n";
    writeXrefEntries(output, entries);

    trailer.Put(PdfName("Size"), PdfObject(static_cast<std::int64_t>(trailerSize(trailer, document))));
    trailer.Put(PdfName("Prev"), PdfObject(static_cast<std::int64_t>(document.GetStartXrefOffset())));
    output << "trailer\n";
    Internal::PdfObjectSerializer::WriteDictionary(output, trailer);
    output << "\nstartxref\n" << xrefOffset << "\n%%EOF\n";
}

std::vector<PdfReference> selectPages(const PdfDocument& document, const std::vector<std::size_t>& indices) {
    const std::size_t count = document.GetPageCount();
    std::vector<PdfReference> result;
    result.reserve(indices.size());
    for (const std::size_t index : indices) {
        if (index >= count) throw PdfException(PdfErrorCode::InvalidArgument, "Page index is out of range.");
        result.push_back(document.GetPageReference(index));
    }
    return result;
}

PdfPageOrganizationResult applySelection(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<std::size_t>& pageIndices,
    bool requireUnique) {

    PdfDocument document = PdfDocument::Open(inputPath);
    if (document.IsEncrypted()) throw PdfException(PdfErrorCode::UnsupportedFeature, "Encrypted PDFs are not supported.");
    if (pageIndices.empty()) throw PdfException(PdfErrorCode::InvalidArgument, "At least one page must remain.");
    if (requireUnique) {
        std::set<std::size_t> unique(pageIndices.begin(), pageIndices.end());
        if (unique.size() != pageIndices.size()) {
            throw PdfException(PdfErrorCode::InvalidArgument, "Page order contains duplicate indices.");
        }
    }

    PdfDictionary trailer = parseTrailer(document);
    const PdfReference pagesRoot = pagesRootReference(document, trailer);
    const auto pages = selectPages(document, pageIndices);
    writeIncrementalPageTree(inputPath, outputPath, document, std::move(trailer), pagesRoot, pages);
    return {outputPath, document.GetPageCount(), pages.size()};
}

} // namespace

PdfPageOrganizationResult PdfPageOrganizer::ReorderPages(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<std::size_t>& pageOrder) {
    const PdfDocument document = PdfDocument::Open(inputPath);
    if (pageOrder.size() != document.GetPageCount()) {
        throw PdfException(PdfErrorCode::InvalidArgument, "ReorderPages requires every page exactly once.");
    }
    return applySelection(inputPath, outputPath, pageOrder, true);
}

PdfPageOrganizationResult PdfPageOrganizer::RemovePages(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<std::size_t>& pageIndices) {
    PdfDocument document = PdfDocument::Open(inputPath);
    const std::size_t count = document.GetPageCount();
    std::set<std::size_t> removed;
    for (const auto index : pageIndices) {
        if (index >= count) throw PdfException(PdfErrorCode::InvalidArgument, "Page index is out of range.");
        removed.insert(index);
    }
    std::vector<std::size_t> kept;
    for (std::size_t index = 0; index < count; ++index) {
        if (!removed.contains(index)) kept.push_back(index);
    }
    if (kept.empty()) throw PdfException(PdfErrorCode::InvalidArgument, "Cannot remove every page.");
    return applySelection(inputPath, outputPath, kept, true);
}

PdfPageOrganizationResult PdfPageOrganizer::ExtractPages(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const std::vector<std::size_t>& pageIndices) {
    return applySelection(inputPath, outputPath, pageIndices, true);
}

std::vector<PdfPageOrganizationResult> PdfPageOrganizer::SplitEvery(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputDirectory,
    const std::size_t pagesPerFile,
    const std::string& filePrefix) {
    if (pagesPerFile == 0U) throw PdfException(PdfErrorCode::InvalidArgument, "pagesPerFile must be greater than zero.");
    PdfDocument document = PdfDocument::Open(inputPath);
    std::filesystem::create_directories(outputDirectory);
    std::vector<PdfPageOrganizationResult> results;
    for (std::size_t begin = 0, part = 1; begin < document.GetPageCount(); begin += pagesPerFile, ++part) {
        const std::size_t end = std::min(begin + pagesPerFile, document.GetPageCount());
        std::vector<std::size_t> pages;
        for (std::size_t index = begin; index < end; ++index) pages.push_back(index);
        const auto outputPath = outputDirectory / (filePrefix + "_" + std::to_string(part) + ".pdf");
        results.push_back(ExtractPages(inputPath, outputPath, pages));
    }
    return results;
}

} // namespace CPPPdf

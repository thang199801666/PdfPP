#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace CPPPdf {

struct PdfPageImportSource final {
    std::filesystem::path inputPath;
    std::vector<std::size_t> pageIndices;
};

enum class PdfFormFieldConflictPolicy {
    RenameWithSourceIndex,
    KeepDuplicate,
    Error
};

struct PdfPageImportOptions final {
    // Document-level structures are copied from the first source only. Catalog
    // structures that can target pages are preserved only when all pages from
    // the first source are imported in their original order.
    bool preserveDocumentInfo{true};
    bool preserveMetadataStream{true};
    bool preserveOutlines{true};
    bool preserveNamedDestinations{true};
    bool preservePageModeAndLayout{true};
    bool preserveAcroForm{true};
    PdfFormFieldConflictPolicy formFieldConflictPolicy{PdfFormFieldConflictPolicy::RenameWithSourceIndex};
};

struct PdfPageImportResult final {
    std::filesystem::path outputPath;
    std::size_t sourceDocumentCount{};
    std::size_t importedPageCount{};
    std::size_t importedObjectCount{};
    std::size_t preservedCatalogEntryCount{};
    bool preservedDocumentInfo{};
    bool preservedAcroForm{};
    std::size_t importedFormFieldCount{};
};

class PdfPageImporter final {
public:
    [[nodiscard]] static PdfPageImportResult MergeDocuments(
        const std::vector<std::filesystem::path>& inputPaths,
        const std::filesystem::path& outputPath,
        const PdfPageImportOptions& options = {});

    [[nodiscard]] static PdfPageImportResult CopyPages(
        const std::vector<PdfPageImportSource>& sources,
        const std::filesystem::path& outputPath,
        const PdfPageImportOptions& options = {});
};

} // namespace CPPPdf

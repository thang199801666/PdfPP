#pragma once

#include <filesystem>
#include <vector>
#include <cstddef>

namespace CPPPdf {

struct PdfPageOrganizationResult final {
    std::filesystem::path outputPath;
    std::size_t originalPageCount{};
    std::size_t outputPageCount{};
};

class PdfPageOrganizer final {
public:
    [[nodiscard]] static PdfPageOrganizationResult ReorderPages(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<std::size_t>& pageOrder);

    // Moves a page to a new index, shifting the others accordingly.
    [[nodiscard]] static PdfPageOrganizationResult MovePage(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t fromIndex,
        std::size_t toIndex);

    [[nodiscard]] static PdfPageOrganizationResult RemovePages(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<std::size_t>& pageIndices);

    [[nodiscard]] static PdfPageOrganizationResult ExtractPages(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<std::size_t>& pageIndices);
    [[nodiscard]] static PdfPageOrganizationResult ExtractRange(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t firstPage, std::size_t pageCount);

    [[nodiscard]] static std::vector<PdfPageOrganizationResult> SplitEvery(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputDirectory,
        std::size_t pagesPerFile,
        const std::string& filePrefix = "part");

    // Duplicates the given pages, appending copies at the end of the document.
    [[nodiscard]] static PdfPageOrganizationResult DuplicatePages(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<std::size_t>& pageIndices);
};

} // namespace CPPPdf

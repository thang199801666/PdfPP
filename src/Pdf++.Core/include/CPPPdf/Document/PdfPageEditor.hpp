#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/Writer/PdfWriter.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CPPPdf {

enum class PdfContentLayer {
    Background,
    Foreground
};

struct PdfPageEdit final {
    std::size_t pageIndex{};
    std::string backgroundContent;
    std::string foregroundContent;
    std::optional<int> rotation;
    std::optional<PdfRectangle> mediaBox;
    std::optional<PdfRectangle> cropBox;
    std::vector<PdfTextStampOptions> textStamps;
    std::vector<PdfWatermarkOptions> watermarks;
    std::vector<std::pair<PdfImage, PdfImageStampOptions>> imageStamps;
};

struct PdfPageEditResult final {
    std::filesystem::path outputPath;
    std::size_t modifiedPageCount{};
    std::size_t appendedContentStreamCount{};
};

class PdfContentCommands final {
public:
    [[nodiscard]] static std::string DrawLine(
        PdfPoint start,
        PdfPoint end,
        double lineWidth = 1.0,
        double red = 0.0,
        double green = 0.0,
        double blue = 0.0);

    [[nodiscard]] static std::string FillRectangle(
        const PdfRectangle& rectangle,
        double red,
        double green,
        double blue);

    [[nodiscard]] static std::string StrokeRectangle(
        const PdfRectangle& rectangle,
        double lineWidth = 1.0,
        double red = 0.0,
        double green = 0.0,
        double blue = 0.0);
};

class PdfPageEditor final {
public:
    [[nodiscard]] static PdfPageEditResult ApplyEdits(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const std::vector<PdfPageEdit>& edits);

    [[nodiscard]] static PdfPageEditResult AddContent(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t pageIndex,
        std::string content,
        PdfContentLayer layer = PdfContentLayer::Foreground);

    [[nodiscard]] static PdfPageEditResult AddTextStamp(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t pageIndex,
        const PdfTextStampOptions& options);

    [[nodiscard]] static PdfPageEditResult AddTextStampToAllPages(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const PdfTextStampOptions& options);

    [[nodiscard]] static PdfPageEditResult AddWatermark(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t pageIndex,
        const PdfWatermarkOptions& options);

    [[nodiscard]] static PdfPageEditResult AddImageStamp(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        std::size_t pageIndex,
        const PdfImage& image,
        const PdfImageStampOptions& options);

    [[nodiscard]] static PdfPageEditResult AddImageStampToAllPages(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const PdfImage& image,
        const PdfImageStampOptions& options);

    [[nodiscard]] static PdfPageEditResult AddWatermarkToAllPages(
        const std::filesystem::path& inputPath,
        const std::filesystem::path& outputPath,
        const PdfWatermarkOptions& options);
};

} // namespace CPPPdf

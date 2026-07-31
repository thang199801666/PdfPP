#include <CPPPdf/CPPPdf.hpp>

#include <filesystem>
#include <iostream>

int main()
{
    const std::filesystem::path inputPath =
        R"(D:\Documents\openxlsx.pdf)";

    const std::filesystem::path outputPath =
        R"(D:\Documents\openxlsx_highlighted.pdf)";

    CPPPdf::PdfKeywordHighlightOptions options;

    options.keyword = "openXL";
    options.caseInsensitive = true;

    // Vàng nhạt.
    options.color = {
        1.0,
        1.0,
        0.70
    };

    options.opacity = 0.75;
    options.verticalPadding = 1.0;

    try
    {
        const auto result =
            CPPPdf::PdfKeywordHighlighter::HighlightFile(
                inputPath,
                outputPath,
                options);

        std::cout
            << "Found "
            << result.MatchCount()
            << " matches.\n";

        for (const auto& match : result.matches)
        {
            std::cout
                << "Page "
                << match.pageIndex + 1
                << ": ["
                << match.rectangle.left << ", "
                << match.rectangle.bottom << ", "
                << match.rectangle.right << ", "
                << match.rectangle.top
                << "]\n";
        }

        std::cout
            << "Output: "
            << result.outputPath.string()
            << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Error: "
            << error.what()
            << '\n';

        return 1;
    }

    return 0;
}
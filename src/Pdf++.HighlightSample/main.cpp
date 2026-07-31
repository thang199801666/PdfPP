#include <CPPPdf/CPPPdf.hpp>

#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    try {
        if (argc < 2) {
            std::cerr << "Usage: Pdf++.HighlightSample <input.pdf> [output.pdf]\n";
            return 2;
        }

        const std::filesystem::path inputPath = argv[1];
        const std::filesystem::path outputPath = argc >= 3
            ? std::filesystem::path(argv[2])
            : inputPath.parent_path() / (inputPath.stem().wstring() + L"_highlighted.pdf");

        CPPPdf::PdfKeywordHighlightOptions options;
        options.keyword = "openXL";
        options.caseInsensitive = true;
        options.color = {1.0, 1.0, 0.70};
        options.opacity = 0.35;
        options.verticalPadding = 1.0;

        const auto result = CPPPdf::PdfKeywordHighlighter::HighlightFile(
            inputPath,
            outputPath,
            options);

        std::cout << "Input : " << inputPath.string() << '\n';
        std::cout << "Output: " << outputPath.string() << '\n';
        std::cout << "Matches: " << result.MatchCount() << '\n';
        for (const auto& match : result.matches) {
            std::cout << "  Page " << (match.pageIndex + 1)
                      << " [" << match.rectangle.left << ", " << match.rectangle.bottom
                      << ", " << match.rectangle.right << ", " << match.rectangle.top << "]\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

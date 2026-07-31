#include <CPPPdf/CPPPdf.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

double measureMilliseconds(const std::function<void()>& operation, const std::size_t iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto begin = Clock::now();
        operation();
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2U];
}

void printRow(const std::string& workload, const double milliseconds,
              const std::size_t pages, const std::uintmax_t bytes) {
    const double pagesPerSecond = milliseconds > 0.0
        ? static_cast<double>(pages) * 1000.0 / milliseconds : 0.0;
    const double megabytesPerSecond = milliseconds > 0.0
        ? static_cast<double>(bytes) / (1024.0 * 1024.0) * 1000.0 / milliseconds : 0.0;
    std::cout << workload << ',' << std::fixed << std::setprecision(3)
              << milliseconds << ',' << pagesPerSecond << ',' << megabytesPerSecond << '\n';
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: Pdf++.Benchmarks <input.pdf> [iterations] [threads]\n";
        return 1;
    }
    const std::filesystem::path path = argv[1];
    const std::size_t iterations = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 5U;
    const std::size_t threads = argc >= 4 ? std::max(1, std::atoi(argv[3])) : 0U;
    const auto fileBytes = std::filesystem::file_size(path);

    CPPPdf::PdfDocument baseline = CPPPdf::PdfDocument::Open(path);
    const std::size_t pages = baseline.GetPageCount();

    std::cout << "workload,median_ms,pages_per_second,megabytes_per_second\n";
    printRow("open_and_page_count", measureMilliseconds([&] {
        auto document = CPPPdf::PdfDocument::Open(path);
        volatile auto count = document.GetPageCount();
        (void)count;
    }, iterations), pages, fileBytes);

    printRow("extract_all_sequential", measureMilliseconds([&] {
        auto document = CPPPdf::PdfDocument::Open(path);
        volatile auto text = document.GetAllPageText();
        (void)text;
    }, iterations), pages, fileBytes);

    printRow("extract_all_parallel", measureMilliseconds([&] {
        auto document = CPPPdf::PdfDocument::Open(path);
        volatile auto text = document.ExtractAllPageTextParallel(threads);
        (void)text;
    }, iterations), pages, fileBytes);


    const auto searchChunks = baseline.ExtractTextChunks(0U);
    printRow("search_literal_first_page", measureMilliseconds([&] {
        volatile auto matches = CPPPdf::PdfTextSearch::Find(searchChunks, "the");
        (void)matches;
    }, iterations), 1U, fileBytes);

    printRow("search_literal_case_sensitive_first_page", measureMilliseconds([&] {
        CPPPdf::PdfTextSearchOptions options;
        options.caseInsensitive = false;
        volatile auto matches = CPPPdf::PdfTextSearch::Find(searchChunks, "the", options);
        (void)matches;
    }, iterations), 1U, fileBytes);

    const CPPPdf::PdfTextSearchIndex searchIndex(searchChunks);
    printRow("search_literal_reusable_index_first_page", measureMilliseconds([&] {
        volatile auto matches = searchIndex.Find("the");
        (void)matches;
    }, iterations), 1U, fileBytes);

    const std::regex precompiledRegex(R"([A-Za-z]{4,})",
                                      std::regex_constants::ECMAScript |
                                      std::regex_constants::icase |
                                      std::regex_constants::optimize);
    printRow("search_regex_precompiled_first_page", measureMilliseconds([&] {
        CPPPdf::PdfRegexSearchOptions options;
        options.maxMatches = 1000U;
        volatile auto matches = CPPPdf::PdfTextSearch::FindRegex(
            searchChunks, precompiledRegex, options);
        (void)matches;
    }, iterations), 1U, fileBytes);

    printRow("search_regex_reusable_index_first_page", measureMilliseconds([&] {
        CPPPdf::PdfRegexSearchOptions options;
        options.maxMatches = 1000U;
        volatile auto matches = searchIndex.FindRegex(precompiledRegex, options);
        (void)matches;
    }, iterations), 1U, fileBytes);

    printRow("search_regex_first_page", measureMilliseconds([&] {
        CPPPdf::PdfRegexSearchOptions options;
        options.maxMatches = 1000U;
        volatile auto matches = CPPPdf::PdfTextSearch::FindRegex(
            searchChunks, R"([A-Za-z]{4,})", options);
        (void)matches;
    }, iterations), 1U, fileBytes);


    CPPPdf::PdfDocument indexedDocument = CPPPdf::PdfDocument::Open(path);
    CPPPdf::PdfDocumentTextIndexOptions documentIndexOptions;
    documentIndexOptions.memoryBudgetBytes = 256U * 1024U * 1024U;
    documentIndexOptions.maxConcurrency = threads;
    CPPPdf::PdfTextDocumentIndex documentIndex(indexedDocument, documentIndexOptions);
    printRow("document_text_index_cold_search_all", measureMilliseconds([&] {
        documentIndex.Clear();
        volatile auto matches = documentIndex.FindAll("the");
        (void)matches;
    }, iterations), pages, fileBytes);
    documentIndex.Preload(0U, pages);
    printRow("document_text_index_warm_search_all", measureMilliseconds([&] {
        volatile auto matches = documentIndex.FindAll("the");
        (void)matches;
    }, iterations), pages, fileBytes);
    printRow("document_text_index_warm_regex_all", measureMilliseconds([&] {
        CPPPdf::PdfRegexSearchOptions options;
        options.maxMatches = 10000U;
        volatile auto matches = documentIndex.FindRegexAll(precompiledRegex, options);
        (void)matches;
    }, iterations), pages, fileBytes);

    printRow("enumerate_images", measureMilliseconds([&] {
        auto document = CPPPdf::PdfDocument::Open(path);
        std::size_t imageCount = 0U;
        for (std::size_t page = 0; page < document.GetPageCount(); ++page)
            imageCount += document.ExtractImages(page).size();
        volatile auto sink = imageCount;
        (void)sink;
    }, iterations), pages, fileBytes);

    return 0;
}

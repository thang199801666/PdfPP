#include <CPPPdf/PdfDocument.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0U) return 0;

    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    CPPPdf::PdfReaderOptions options;
    options.repairDamagedXref = true;
    options.limits.maxObjectCount = 50'000U;
    options.limits.maxRecursionDepth = 64U;
    options.limits.maxDecodedStreamSize = 4U * 1024U * 1024U;
    options.limits.maxObjectStreamObjects = 10'000U;
    options.limits.maxPageCount = 10'000U;
    options.limits.maxCachedObjects = 256U;

    try {
        auto document = CPPPdf::PdfDocument::Open(
            std::span<const std::byte>(bytes, size), options);
        if (document.GetPageCount() > 0U) {
            (void)document.GetPageInfo(0U);
        }
    } catch (const CPPPdf::PdfException&) {
        // Rejection of malformed or unsupported input is expected.
    } catch (...) {
        // The fuzz target must not turn recoverable parser errors into crashes.
    }
    return 0;
}

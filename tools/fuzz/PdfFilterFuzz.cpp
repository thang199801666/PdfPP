#include <CPPPdf/Filters/PdfFilterPipeline.hpp>
#include <CPPPdf/PdfError.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0U) return 0;

    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data), size);

    // Exercise every supported filter on the same input. A malformed stream
    // must throw a controlled PdfException, never crash.
    const char* const filters[] = {
        "FlateDecode",
        "ASCIIHexDecode",
        "ASCII85Decode",
        "RunLengthDecode",
        "LZWDecode"
    };
    for (const char* name : filters) {
        try {
            (void)CPPPdf::PdfFilterPipeline(16U * 1024U * 1024U).Decode(
                bytes, {{name, {}}});
        } catch (const CPPPdf::PdfException&) {
            // Expected for truncated or corrupt data.
        } catch (const std::exception&) {
            // Recoverable filter errors must not propagate as crashes.
        }
    }
    return 0;
}

#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/PdfError.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0U) return 0;

    const auto content = std::string_view(
        reinterpret_cast<const char*>(data), size);

    try {
        CPPPdf::PdfContentProcessor processor;
        processor.SetHandler([](const CPPPdf::PdfContentEvent&) {
            // The processor itself must stay allocation-bounded and robust;
            // consuming every event exercises all operator parsing paths.
        });
        processor.Process(content);
    } catch (const CPPPdf::PdfException&) {
        // Rejection of malformed or unsupported content is expected.
    } catch (const std::exception&) {
        // The fuzz target must not turn recoverable content errors into
        // crashes, aborts, or non-termination.
    }
    return 0;
}

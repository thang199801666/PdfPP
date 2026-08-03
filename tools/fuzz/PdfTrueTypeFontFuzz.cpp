#include <CPPPdf/Fonts/PdfTrueTypeFont.hpp>
#include <CPPPdf/PdfError.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0U) return 0;

    std::vector<std::uint8_t> bytes(data, data + size);
    try {
        const auto font = CPPPdf::PdfTrueTypeFont::Parse(std::move(bytes));
        // Walk a bounded set of glyph outlines through the quadratic path
        // builder and the cached advance path.
        const std::size_t glyphCount = font.GetMetrics().glyphCount;
        const std::size_t stride = glyphCount == 0U ? 1U : glyphCount;
        for (std::uint16_t glyph = 0U; glyph < glyphCount; glyph += static_cast<std::uint16_t>(stride)) {
            try {
                (void)font.GetGlyphOutline(glyph);
                (void)font.GetAdvanceWidth(glyph);
            } catch (const std::exception&) {
                // A single malformed glyph must not abort the walk.
            }
        }
    } catch (const CPPPdf::PdfException&) {
        // Malformed TrueType rejection is expected.
    } catch (const std::exception&) {
        // Recoverable font errors must not crash the harness.
    }
    return 0;
}

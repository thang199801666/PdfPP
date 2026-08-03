#include <CPPPdf/Fonts/PdfCff.hpp>
#include <CPPPdf/PdfError.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 4U) return 0;

    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data), size);

    try {
        const auto font = CPPPdf::PdfCffParser::ParseFont(bytes);
        // Walk every glyph outline through the Type 2 charstring interpreter.
        for (std::uint32_t glyph = 0U; glyph < font.glyphCount; ++glyph) {
            try {
                (void)CPPPdf::PdfCffParser::GetGlyphOutline(font, glyph);
            } catch (const std::exception&) {
                // A single malformed charstring must not abort the walk.
            }
        }
    } catch (const CPPPdf::PdfException&) {
        // Malformed CFF program rejection is expected.
    } catch (const std::exception&) {
        // Recoverable font errors must not crash the harness.
    }
    return 0;
}

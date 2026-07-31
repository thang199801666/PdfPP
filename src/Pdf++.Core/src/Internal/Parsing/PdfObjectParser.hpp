#pragma once
#include <string_view>
#include <CPPPdf/Objects/PdfObject.hpp>

namespace CPPPdf::Internal {
class PdfObjectParser final {
public:
    [[nodiscard]] static PdfObject Parse(std::string_view source, std::size_t maxDepth = 256);
};
} // namespace CPPPdf::Internal

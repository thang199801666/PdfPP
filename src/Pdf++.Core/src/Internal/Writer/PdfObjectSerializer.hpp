#pragma once

#include <CPPPdf/Objects/PdfObject.hpp>

#include <functional>
#include <iosfwd>
#include <string>

namespace CPPPdf::Internal {

class PdfObjectSerializer final {
public:
    using ReferenceMapper = std::function<PdfReference(const PdfReference&)>;

    static void WriteObject(
        std::ostream& output,
        const PdfObject& object,
        const ReferenceMapper& mapper = {});

    static void WriteDictionary(
        std::ostream& output,
        const PdfDictionary& dictionary,
        const ReferenceMapper& mapper = {});

    [[nodiscard]] static std::string EscapeName(std::string_view value);
    [[nodiscard]] static std::string EscapeLiteral(std::string_view value);
};

} // namespace CPPPdf::Internal

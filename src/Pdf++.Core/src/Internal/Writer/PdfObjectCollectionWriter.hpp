#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include "Internal/Security/PdfStandardSecurity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace CPPPdf::Internal {

struct PdfObjectCollectionWriterOptions {
    bool writeXrefStream{true};
    bool writeObjectStreams{false};
};

// Emits a fully assembled PDF from a 1-indexed vector of serialized object
// bodies. Shared by PdfWriter::Save and the resave/sanitize path so both
// produce byte-compatible output (header, xref stream or classic table,
// optional object stream, trailer, startxref).
class PdfObjectCollectionWriter final {
public:
    // objects: 1-indexed; index 0 is unused. The encryption dictionary body,
    // when encryptionObject != 0, must already be stored in objects.
    static void Write(std::ostream& output,
                      const PdfObjectCollectionWriterOptions& options,
                      const std::vector<std::string>& objects,
                      std::size_t catalogObject,
                      std::size_t infoObject,
                      std::size_t encryptionObject,
                      const PdfStandardSecurity* security,
                      const std::array<std::uint8_t, 16>& fileId);
};

} // namespace CPPPdf::Internal

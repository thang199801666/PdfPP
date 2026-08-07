#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace CPPPdf {

class PdfDocument;

struct PdfIncrementalUpdateOptions final {
    bool writeXrefStream{true};
    bool writeObjectStreams{false};
};

// General-purpose incremental revision builder. The source document remains
// immutable; replacements and additions are appended to outputPath and the
// previous revision is retained through the trailer /Prev chain.
class PdfIncrementalUpdate final {
public:
    PdfIncrementalUpdate(const PdfDocument& document,
                         const std::filesystem::path& outputPath,
                         const PdfIncrementalUpdateOptions& options = {});
    ~PdfIncrementalUpdate();

    PdfIncrementalUpdate(const PdfIncrementalUpdate&) = delete;
    PdfIncrementalUpdate& operator=(const PdfIncrementalUpdate&) = delete;
    PdfIncrementalUpdate(PdfIncrementalUpdate&&) noexcept;
    PdfIncrementalUpdate& operator=(PdfIncrementalUpdate&&) noexcept;

    // Replaces an existing indirect object while preserving its object number
    // and generation. Encrypted documents reuse the authenticated file key.
    void ReplaceObject(PdfReference reference, const PdfObject& object);
    void ReplaceDictionary(PdfReference reference, const PdfDictionary& dictionary);

    // Appends a new generation-0 indirect object and returns its reference.
    [[nodiscard]] PdfReference AddObject(const PdfObject& object);
    [[nodiscard]] PdfReference AddDictionary(const PdfDictionary& dictionary);

    // Finalizes the xref/trailer revision. Safe to call more than once.
    void Commit();
    [[nodiscard]] bool IsCommitted() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace CPPPdf

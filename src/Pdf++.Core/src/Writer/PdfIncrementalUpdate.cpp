#include <CPPPdf/Writer/PdfIncrementalUpdate.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Writer/PdfIncrementalWriter.hpp"

#include <algorithm>
#include <utility>

namespace CPPPdf {

struct PdfIncrementalUpdate::Impl final {
    Impl(const PdfDocument& source,
         const std::filesystem::path& outputPath,
         const PdfIncrementalUpdateOptions& options)
        : document(source),
          writer(source.GetPath(), outputPath, source,
                 Internal::PdfIncrementalWriterOptions{
                     options.writeXrefStream, options.writeObjectStreams}),
          nextObject(Internal::PdfIncrementalWriter::NextObjectNumber(source)) {
        if (source.GetPath().empty()) {
            throw PdfException(PdfErrorCode::InvalidArgument,
                               "Incremental update requires a document opened from a file path.");
        }
    }

    const PdfDocument& document;
    Internal::PdfIncrementalWriter writer;
    std::uint32_t nextObject{};
    bool committed{};
};

PdfIncrementalUpdate::PdfIncrementalUpdate(
    const PdfDocument& document,
    const std::filesystem::path& outputPath,
    const PdfIncrementalUpdateOptions& options)
    : impl_(std::make_unique<Impl>(document, outputPath, options)) {
}

PdfIncrementalUpdate::~PdfIncrementalUpdate() = default;
PdfIncrementalUpdate::PdfIncrementalUpdate(PdfIncrementalUpdate&&) noexcept = default;
PdfIncrementalUpdate& PdfIncrementalUpdate::operator=(PdfIncrementalUpdate&&) noexcept = default;

void PdfIncrementalUpdate::ReplaceObject(const PdfReference reference,
                                         const PdfObject& object) {
    if (!impl_ || impl_->committed) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Cannot modify a committed incremental revision.");
    }
    const auto xref = impl_->document.GetXrefEntry(reference.objectNumber);
    if (!xref || !xref->inUse) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Replacement reference does not exist in the source document.");
    }
    if (xref->generation != reference.generation) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Replacement reference generation does not match the source document.");
    }
    const auto encryptionReference = impl_->document.GetTrailerReference(PdfName("Encrypt"));
    if (encryptionReference && *encryptionReference == reference) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "The encryption dictionary cannot be replaced by a generic update.");
    }
    impl_->writer.WriteObject(reference, object);
}

void PdfIncrementalUpdate::ReplaceDictionary(const PdfReference reference,
                                             const PdfDictionary& dictionary) {
    ReplaceObject(reference, PdfObject(dictionary));
}

PdfReference PdfIncrementalUpdate::AddObject(const PdfObject& object) {
    if (!impl_ || impl_->committed) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Cannot modify a committed incremental revision.");
    }
    const PdfReference reference{impl_->nextObject++, 0U};
    impl_->writer.WriteObject(reference, object);
    return reference;
}

PdfReference PdfIncrementalUpdate::AddDictionary(const PdfDictionary& dictionary) {
    return AddObject(PdfObject(dictionary));
}

void PdfIncrementalUpdate::Commit() {
    if (!impl_ || impl_->committed) return;
    impl_->writer.Finish(impl_->nextObject);
    impl_->committed = true;
}

bool PdfIncrementalUpdate::IsCommitted() const noexcept {
    return !impl_ || impl_->committed;
}

} // namespace CPPPdf

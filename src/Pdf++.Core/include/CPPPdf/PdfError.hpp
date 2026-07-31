#pragma once

#include <stdexcept>
#include <string>

namespace CPPPdf {

enum class PdfErrorCode {
    InvalidArgument,
    FileOpenFailed,
    InvalidHeader,
    StartXrefNotFound,
    UnsupportedXrefStream,
    MalformedXref,
    TrailerNotFound,
    ObjectNotFound,
    MalformedObject,
    InvalidPageTree,
    UnsupportedFeature
};

class PdfException final : public std::runtime_error {
public:
    PdfException(PdfErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    [[nodiscard]] PdfErrorCode code() const noexcept { return code_; }

private:
    PdfErrorCode code_;
};

} // namespace CPPPdf

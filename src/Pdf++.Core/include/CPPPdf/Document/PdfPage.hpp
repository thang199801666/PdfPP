#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <CPPPdf/Core/PdfTypes.hpp>

namespace CPPPdf {

class PdfDocument;

class PdfPage final {
public:
    PdfPage() = default;
    [[nodiscard]] std::size_t GetIndex() const noexcept { return info_.pageIndex; }
    [[nodiscard]] std::uint32_t GetObjectNumber() const noexcept { return info_.objectNumber; }
    [[nodiscard]] PdfRectangle GetMediaBox() const noexcept { return info_.mediaBox; }
    [[nodiscard]] PdfRectangle GetCropBox() const noexcept { return info_.cropBox; }
    [[nodiscard]] int GetRotation() const noexcept { return info_.rotation; }
    [[nodiscard]] const std::string& GetResourcesDictionary() const noexcept { return resourcesDictionary_; }
    [[nodiscard]] const std::vector<std::string>& GetContentStreams() const noexcept { return contentStreams_; }

private:
    friend class PdfDocument;
    PdfPage(PdfPageInfo info, std::string resources, std::vector<std::string> streams)
        : info_(info), resourcesDictionary_(std::move(resources)), contentStreams_(std::move(streams)) {}
    PdfPageInfo info_{};
    std::string resourcesDictionary_;
    std::vector<std::string> contentStreams_;
};

} // namespace CPPPdf

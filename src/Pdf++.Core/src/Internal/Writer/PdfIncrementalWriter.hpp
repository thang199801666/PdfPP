#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

namespace CPPPdf {
class PdfDocument;
}

namespace CPPPdf::Internal {

struct PdfIncrementalWriterOptions {
    bool writeXrefStream{false};
    bool writeObjectStreams{false};
};

class PdfIncrementalWriter final {
public:
    PdfIncrementalWriter(const std::filesystem::path& inputPath,
                         const std::filesystem::path& outputPath,
                         const PdfDocument& document);
    PdfIncrementalWriter(const std::filesystem::path& inputPath,
                         const std::filesystem::path& outputPath,
                         const PdfDocument& document,
                         const PdfIncrementalWriterOptions& options);

    void WriteObject(const PdfReference& reference, const PdfObject& object);
    void WriteDictionary(const PdfReference& reference, const PdfDictionary& dictionary);
    void WriteRawObject(const PdfReference& reference, std::string_view body);
    void Finish(std::uint32_t trailerSize);

    [[nodiscard]] static std::uint32_t NextObjectNumber(const PdfDocument& document);

private:
    [[nodiscard]] bool canLiveInObjectStream(const PdfReference& reference,
                                             std::string_view body) const;
    void writeObjectStream(std::uint32_t& size);
    void writeClassicXref(std::uint32_t trailerSize, std::uint64_t previousOffset);
    void writeXrefStream(std::uint32_t trailerSize, std::uint64_t previousOffset);

    const PdfDocument& document_;
    std::ofstream output_;
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint16_t>> entries_;
    std::map<std::uint32_t, std::string> objectStreamBodies_;
    std::map<std::uint32_t, std::uint32_t> objectStreamOffsets_;
    std::uint32_t objectStreamNumber_{0U};
    std::uint32_t catalogObject_{0U};
    bool writeXrefStream_{false};
    bool writeObjectStreams_{false};
    bool finished_{false};
};

} // namespace CPPPdf::Internal

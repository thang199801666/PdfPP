#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <optional>
#include <istream>
#include <span>
#include <cstddef>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>

namespace CPPPdf {

class PdfPage;
namespace Internal { class PdfObjectResolver; }

struct PdfXrefEntry {
    enum class Type : std::uint8_t { Free = 0, Uncompressed = 1, Compressed = 2 };
    Type type{Type::Free};
    std::uint64_t offset{};
    std::uint32_t objectStream{};
    std::uint32_t objectIndex{};
    std::uint16_t generation{};
    bool inUse{};
};

class PdfDocument final {
public:
    PdfDocument() = default;
    ~PdfDocument();
    PdfDocument(PdfDocument&&) noexcept;
    PdfDocument& operator=(PdfDocument&&) noexcept;
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    static PdfDocument Open(const std::filesystem::path& path);
    static PdfDocument Open(const std::filesystem::path& path, const PdfReaderOptions& options);
    static PdfDocument OpenMapped(const std::filesystem::path& path, const PdfReaderOptions& options = {});
    static PdfDocument Open(std::span<const std::byte> bytes, const PdfReaderOptions& options = {});
    static PdfDocument Open(std::istream& stream, const PdfReaderOptions& options = {});
    static PdfDocument Open(std::unique_ptr<PdfInputSource> source, const PdfReaderOptions& options = {});

    [[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return path_; }
    [[nodiscard]] const std::string& GetVersion() const noexcept { return version_; }
    [[nodiscard]] std::uint64_t GetFileSize() const noexcept { return static_cast<std::uint64_t>(bytes_.size()); }
    [[nodiscard]] std::size_t GetXrefEntryCount() const noexcept { return xref_.size(); }
    [[nodiscard]] std::size_t GetPageCount() const { return pageCount(); }

    // Compatibility accessors retained for existing clients.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return GetPath(); }
    [[nodiscard]] const std::string& version() const noexcept { return GetVersion(); }
    [[nodiscard]] std::uint64_t fileSize() const noexcept { return GetFileSize(); }
    [[nodiscard]] std::size_t xrefEntryCount() const noexcept { return GetXrefEntryCount(); }
    [[nodiscard]] std::size_t pageCount() const;
    [[nodiscard]] const std::string& GetTrailerDictionary() const noexcept { return trailerDictionary_; }
    [[nodiscard]] bool IsEncrypted() const noexcept { return trailerDictionary_.find("/Encrypt") != std::string::npos; }
    [[nodiscard]] const std::string& trailerDictionary() const noexcept { return GetTrailerDictionary(); }
    [[nodiscard]] bool isEncrypted() const noexcept { return IsEncrypted(); }
    [[nodiscard]] PdfDocumentInfo documentInfo() const;
    [[nodiscard]] PdfDocumentInfo GetDocumentInfo() const { return documentInfo(); }
    [[nodiscard]] PdfPageInfo pageInfo(std::size_t pageIndex) const;
    [[nodiscard]] PdfPageInfo GetPageInfo(std::size_t pageIndex) const { return pageInfo(pageIndex); }

    [[nodiscard]] std::string readIndirectObject(std::uint32_t objectNumber) const;
    [[nodiscard]] std::vector<std::uint32_t> objectNumbers() const;
    [[nodiscard]] const PdfObject& GetObject(const PdfReference& reference) const;
    [[nodiscard]] PdfPage GetPage(std::size_t pageIndex) const;
    [[nodiscard]] PdfReference GetPageReference(std::size_t pageIndex) const;
    [[nodiscard]] PdfReference GetCatalogReference() const;
    [[nodiscard]] std::optional<PdfReference> GetTrailerReference(const PdfName& key) const;
    [[nodiscard]] std::uint64_t GetStartXrefOffset() const { return findStartXref(); }
    [[nodiscard]] const PdfReaderOptions& readerOptions() const noexcept { return readerOptions_; }
    [[nodiscard]] std::size_t GetCachedObjectCount() const noexcept;
    [[nodiscard]] std::size_t GetObjectCacheCapacity() const noexcept;
    void ClearObjectCache() const noexcept;

    // pageIndex is zero-based.
    [[nodiscard]] std::string extractPageText(std::size_t pageIndex) const;
    [[nodiscard]] std::string GetPageText(std::size_t pageIndex) const { return extractPageText(pageIndex); }
    [[nodiscard]] std::vector<std::string> extractAllPageText() const;
    [[nodiscard]] std::vector<std::string> GetAllPageText() const { return extractAllPageText(); }
    // Extracts pages concurrently. File-backed documents use independent reader
    // instances per worker to keep resolver state thread-safe. Non-file inputs
    // fall back to deterministic sequential extraction.
    [[nodiscard]] std::vector<std::string> ExtractAllPageTextParallel(
        std::size_t maxConcurrency = 0U) const;
    [[nodiscard]] PdfTextPage ExtractTextPage(
        std::size_t pageIndex,
        const PdfTextExtractionOptions& options = {}) const;
    [[nodiscard]] std::vector<PdfTextChunk> ExtractTextChunks(
        std::size_t pageIndex,
        const PdfTextExtractionRequest& request = {}) const;
    [[nodiscard]] std::string ExtractText(
        std::size_t pageIndex,
        const PdfTextExtractionRequest& request) const;
    [[nodiscard]] std::vector<PdfExtractedImage> ExtractImages(
        std::size_t pageIndex,
        const PdfImageExtractionOptions& options = {}) const;

private:
    void parse();
    void parseHeader();
    [[nodiscard]] std::uint64_t findStartXref() const;
    void parseXrefSection(std::uint64_t offset);
    void parseClassicXref(std::uint64_t offset);
    void parseXrefStream(std::uint64_t offset);
    [[nodiscard]] PdfReference findRootReference() const;
    [[nodiscard]] PdfReference findPagesReference(const std::string& catalogObject) const;
    [[nodiscard]] std::size_t countPagesFromNode(const PdfReference& reference,
                                                 std::unordered_map<std::uint32_t, bool>& visiting) const;
    void collectPageReferences(const PdfReference& reference,
                               std::unordered_map<std::uint32_t, bool>& visiting,
                               std::vector<PdfReference>& pages) const;

    [[nodiscard]] std::vector<PdfReference> pageReferences() const;
    [[nodiscard]] std::vector<PdfReference> contentReferences(const std::string& pageObject) const;
    [[nodiscard]] std::string decodeContentStream(const std::string& streamObject) const;
    [[nodiscard]] std::string readCompressedObject(std::uint32_t objectNumber,
                                                   const PdfXrefEntry& entry) const;
    [[nodiscard]] std::string recoverIndirectObject(std::uint32_t objectNumber) const;
    [[nodiscard]] std::string recoverFromObjectStreams(std::uint32_t objectNumber) const;
    [[nodiscard]] static std::string extractStreamData(const std::string& streamObject);
    [[nodiscard]] static std::vector<std::size_t> parseIntegerArrayAfterKey(
        const std::string& dictionary, const std::string& key);
    [[nodiscard]] static std::string extractTextOperators(const std::string& content,
                                                           const PdfTextExtractionOptions& options);

    [[nodiscard]] static PdfReference parseReferenceAfterKey(const std::string& dictionary,
                                                             const std::string& key);
    [[nodiscard]] static std::vector<PdfReference> parseReferenceArrayAfterKey(const std::string& dictionary,
                                                                               const std::string& key);
    [[nodiscard]] static std::string parseNameAfterKey(const std::string& dictionary,
                                                       const std::string& key);
    [[nodiscard]] static std::string parseStringAfterKey(const std::string& dictionary,
                                                         const std::string& key);
    [[nodiscard]] static std::vector<double> parseNumberArrayAfterKey(const std::string& dictionary,
                                                                      const std::string& key);
    [[nodiscard]] std::string findInheritedPageValue(std::string pageObject,
                                                     const std::string& key) const;
    [[nodiscard]] std::string extractPageTextFromReference(
        const PdfReference& pageReference,
        const PdfTextExtractionOptions& options = {}) const;

    [[nodiscard]] static std::size_t parseIntegerAfterKey(const std::string& dictionary,
                                                          const std::string& key);

    std::filesystem::path path_;
    PdfReaderOptions readerOptions_{};
    std::vector<char> bytes_;
    std::string version_;
    std::unordered_map<std::uint32_t, PdfXrefEntry> xref_;
    std::unordered_set<std::uint64_t> parsedXrefOffsets_;
    mutable bool pageReferencesCached_{false};
    mutable std::vector<PdfReference> pageReferencesCache_;
    std::string trailerDictionary_;
    mutable std::unique_ptr<Internal::PdfObjectResolver> objectResolver_;
};

} // namespace CPPPdf

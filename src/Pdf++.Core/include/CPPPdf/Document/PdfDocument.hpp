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
#include <string_view>
#include <cstddef>
#include <functional>
#include <list>
#include <CPPPdf/IO/PdfReader.hpp>
#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Content/PdfContentProcessor.hpp>
#include <CPPPdf/Rendering/PdfBitmap.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/Text/PdfTextExtractor.hpp>
#include <CPPPdf/Graphics/PdfImage.hpp>
#include <CPPPdf/Rendering/PdfShading.hpp>

namespace CPPPdf {

class PdfFontResource;

class PdfPage;
class PdfDisplayList;
class PdfFontResource;
namespace Internal { class PdfObjectResolver; class PdfStandardSecurity; class PdfIncrementalWriter; }

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
    PdfDocument();
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
    [[nodiscard]] bool HasPage(std::size_t pageIndex) const noexcept { return pageIndex < pageCount(); }

    // Compatibility accessors retained for existing clients.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return GetPath(); }
    [[nodiscard]] const std::string& version() const noexcept { return GetVersion(); }
    [[nodiscard]] std::uint64_t fileSize() const noexcept { return GetFileSize(); }
    [[nodiscard]] std::size_t xrefEntryCount() const noexcept { return GetXrefEntryCount(); }
    [[nodiscard]] std::size_t pageCount() const;
    [[nodiscard]] const std::string& GetTrailerDictionary() const noexcept { return trailerDictionary_; }
    [[nodiscard]] bool IsEncrypted() const noexcept { return encryption_ != nullptr; }
    [[nodiscard]] bool IsPasswordRequired() const noexcept;
    [[nodiscard]] bool AuthenticatePassword(std::string_view password);
    [[nodiscard]] bool IsOwnerPasswordAuthenticated() const noexcept;
    [[nodiscard]] std::int32_t GetPermissionBits() const noexcept;
    [[nodiscard]] const std::string& trailerDictionary() const noexcept { return GetTrailerDictionary(); }
    [[nodiscard]] bool isEncrypted() const noexcept { return IsEncrypted(); }
    [[nodiscard]] PdfDocumentInfo documentInfo() const;
    [[nodiscard]] PdfDocumentInfo GetDocumentInfo() const { return documentInfo(); }
    [[nodiscard]] PdfPageInfo pageInfo(std::size_t pageIndex) const;
    [[nodiscard]] PdfPageInfo GetPageInfo(std::size_t pageIndex) const { return pageInfo(pageIndex); }

    [[nodiscard]] std::string readIndirectObject(std::uint32_t objectNumber) const;
    [[nodiscard]] std::vector<std::uint32_t> objectNumbers() const;
    [[nodiscard]] std::optional<PdfXrefEntry> GetXrefEntry(std::uint32_t objectNumber) const;
    [[nodiscard]] const PdfObject& GetObject(const PdfReference& reference) const;
    [[nodiscard]] PdfPage GetPage(std::size_t pageIndex) const;
    [[nodiscard]] PdfReference GetPageReference(std::size_t pageIndex) const;
    [[nodiscard]] PdfReference GetCatalogReference() const;
    [[nodiscard]] std::optional<PdfReference> GetTrailerReference(const PdfName& key) const;
    [[nodiscard]] std::uint64_t GetStartXrefOffset() const { return findStartXref(); }
    [[nodiscard]] const PdfReaderOptions& readerOptions() const noexcept { return readerOptions_; }
    [[nodiscard]] std::size_t GetCachedObjectCount() const noexcept;
    [[nodiscard]] std::size_t GetObjectCacheCapacity() const noexcept;
    [[nodiscard]] std::size_t GetCachedObjectStreamCount() const noexcept;
    [[nodiscard]] std::size_t GetCachedObjectStreamBytes() const noexcept;
    [[nodiscard]] std::size_t GetObjectStreamCacheHits() const noexcept;
    [[nodiscard]] std::size_t GetObjectStreamCacheMisses() const noexcept;
    [[nodiscard]] std::shared_ptr<const PdfFontResource> GetCachedFontResource(
        PdfReference reference) const;
    [[nodiscard]] std::size_t GetCachedFontResourceCount() const noexcept;
    [[nodiscard]] std::size_t GetFontResourceCacheHits() const noexcept;
    [[nodiscard]] std::size_t GetFontResourceCacheMisses() const noexcept;
    [[nodiscard]] std::size_t GetCachedContentStreamCount() const noexcept;
    [[nodiscard]] std::size_t GetCachedContentStreamBytes() const noexcept;
    [[nodiscard]] std::size_t GetContentStreamCacheHits() const noexcept;
    [[nodiscard]] std::size_t GetContentStreamCacheMisses() const noexcept;
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
    [[nodiscard]] std::shared_ptr<const PdfFontResource> ResolvePageFont(
        std::size_t pageIndex, std::string_view resourceName) const;
    [[nodiscard]] std::shared_ptr<const PdfFontResource> ResolveFont(
        std::size_t pageIndex, std::uint32_t resourceObjectNumber,
        std::string_view resourceName) const;
    [[nodiscard]] std::array<double, 2> ResolvePageExtGStateAlpha(
        std::size_t pageIndex, std::string_view resourceName) const;
    [[nodiscard]] std::array<double, 2> ResolveExtGStateAlpha(
        std::size_t pageIndex, std::uint32_t resourceObjectNumber,
        std::string_view resourceName) const;
    [[nodiscard]] std::pair<std::array<double, 2>, PdfBlendMode> ResolveExtGState(
        std::size_t pageIndex, std::uint32_t resourceObjectNumber,
        std::string_view resourceName) const;
    [[nodiscard]] std::pair<bool, bool> ResolveTransparencyFlags(
        std::size_t pageIndex, std::uint32_t resourceObjectNumber,
        std::string_view resourceName) const;
    [[nodiscard]] std::optional<PdfDictionary> ResolveShading(
        std::size_t pageIndex, std::uint32_t resourceObjectNumber,
        std::string_view resourceName) const;
    [[nodiscard]] std::optional<PdfResolvedShading> ResolveAxialShading(
        std::size_t pageIndex, std::uint32_t resourceObjectNumber,
        std::string_view resourceName) const;
    [[nodiscard]] std::optional<PdfResolvedShading> ResolveRadialShading(
        std::size_t pageIndex, std::uint32_t resourceObjectNumber,
        std::string_view resourceName) const;
    [[nodiscard]] std::optional<PdfResolvedPattern> ResolveTilingPattern(
        std::size_t pageIndex, std::uint32_t resourceObjectNumber,
        std::string_view resourceName) const;
    using PdfContentEventHandler = std::function<void(const PdfContentEvent&)>;
    void ForEachPageContentEvent(
        std::size_t pageIndex,
        const PdfContentEventHandler& handler) const;
    [[nodiscard]] PdfDisplayList BuildPageDisplayList(std::size_t pageIndex) const;
    [[nodiscard]] std::string ExtractText(
        std::size_t pageIndex,
        const PdfTextExtractionRequest& request) const;
    [[nodiscard]] std::vector<PdfExtractedImage> ExtractImages(
        std::size_t pageIndex,
        const PdfImageExtractionOptions& options = {}) const;

private:
    struct DecodedObjectStream {
        std::string decoded;
        std::size_t first{};
        std::vector<std::pair<std::uint32_t, std::size_t>> objects;
    };

    struct ObjectStreamCacheEntry {
        std::shared_ptr<const DecodedObjectStream> stream;
        std::list<std::uint32_t>::iterator recency;
    };

    friend class Internal::PdfIncrementalWriter;
    friend class PdfWriter;
    void parse();
    void parseHeader();
    [[nodiscard]] std::uint64_t findStartXref() const;
    [[nodiscard]] std::vector<std::uint64_t> findRecoveredXrefOffsets() const;
    void parseXrefSection(std::uint64_t offset);
    void parseClassicXref(std::uint64_t offset);
    void parseXrefStream(std::uint64_t offset);
    void initializeEncryption();
    [[nodiscard]] std::string readRawIndirectObject(std::uint32_t objectNumber) const;
    [[nodiscard]] std::string EncryptObjectForIncrementalWrite(
        std::string_view object, const PdfReference& reference) const;
    [[nodiscard]] PdfReference findRootReference() const;
    [[nodiscard]] PdfReference findPagesReference(const std::string& catalogObject) const;
    [[nodiscard]] std::size_t countPagesFromNode(const PdfReference& reference,
                                                 std::unordered_map<std::uint32_t, bool>& visiting) const;
    void collectPageReferences(const PdfReference& reference,
                               std::unordered_map<std::uint32_t, bool>& visiting,
                               std::vector<PdfReference>& pages) const;

    [[nodiscard]] const std::vector<PdfReference>& pageReferences() const;
    [[nodiscard]] std::vector<PdfReference> contentReferences(const std::string& pageObject) const;
    [[nodiscard]] std::string decodeContentStream(const std::string& streamObject) const;
    [[nodiscard]] std::string decodeContentStreamReference(const PdfReference& reference) const;
    [[nodiscard]] std::string readCompressedObject(std::uint32_t objectNumber,
                                                   const PdfXrefEntry& entry) const;
    [[nodiscard]] std::shared_ptr<const DecodedObjectStream> loadObjectStream(
        std::uint32_t objectStreamNumber) const;
    [[nodiscard]] std::string recoverIndirectObject(std::uint32_t objectNumber) const;
    [[nodiscard]] std::string recoverFromObjectStreams(std::uint32_t objectNumber) const;
    [[nodiscard]] std::string_view extractStreamDataView(const std::string& streamObject) const;
    [[nodiscard]] std::string extractStreamData(const std::string& streamObject) const;
    [[nodiscard]] static std::string extractTextOperators(const std::string& content,
                                                           const PdfTextExtractionOptions& options);

    [[nodiscard]] static PdfReference parseReferenceAfterKey(const std::string& dictionary,
                                                             const std::string& key,
                                                             std::size_t maxDepth);
    [[nodiscard]] static std::vector<PdfReference> parseReferenceArrayAfterKey(const std::string& dictionary,
                                                                               const std::string& key,
                                                                               std::size_t maxDepth);
    [[nodiscard]] static std::string parseNameAfterKey(const std::string& dictionary,
                                                       const std::string& key,
                                                       std::size_t maxDepth);
    [[nodiscard]] static std::string parseStringAfterKey(const std::string& dictionary,
                                                          const std::string& key,
                                                          std::size_t maxDepth);
    [[nodiscard]] PdfObject findInheritedPageValue(const std::string& pageObject,
                                                   const std::string& key,
                                                   std::size_t maxDepth) const;
    [[nodiscard]] std::string extractPageTextFromReference(
        const PdfReference& pageReference,
        const PdfTextExtractionOptions& options = {}) const;

    [[nodiscard]] static std::size_t parseIntegerAfterKey(const std::string& dictionary,
                                                          const std::string& key,
                                                          std::size_t maxDepth);

    std::filesystem::path path_;
    std::unique_ptr<PdfInputSource> source_;
    PdfReaderOptions readerOptions_{};
    std::vector<char> ownedBytes_;
    std::span<const char> bytes_{};
    std::string version_;
    std::unordered_map<std::uint32_t, PdfXrefEntry> xref_;
    std::unordered_set<std::uint64_t> parsedXrefOffsets_;
    mutable bool pageReferencesCached_{false};
    mutable std::vector<PdfReference> pageReferencesCache_;
    std::string trailerDictionary_;
    std::optional<PdfReference> encryptionReference_;
    std::unique_ptr<Internal::PdfStandardSecurity> encryption_;
    mutable std::unique_ptr<Internal::PdfObjectResolver> objectResolver_;
    mutable std::unordered_map<std::uint32_t, ObjectStreamCacheEntry> objectStreamCache_;
    mutable std::list<std::uint32_t> objectStreamRecency_;
    mutable std::size_t cachedObjectStreamBytes_{};
    mutable std::size_t objectStreamCacheHits_{};
    mutable std::size_t objectStreamCacheMisses_{};
    mutable std::unordered_map<std::uint64_t, std::shared_ptr<PdfFontResource>> fontResourceCache_;
    mutable std::list<std::uint64_t> fontResourceRecency_;
    mutable std::size_t fontResourceCacheHits_{};
    mutable std::size_t fontResourceCacheMisses_{};
    mutable std::unordered_map<std::uint64_t, std::shared_ptr<const std::string>> contentStreamCache_;
    mutable std::list<std::uint64_t> contentStreamRecency_;
    mutable std::size_t cachedContentStreamBytes_{};
    mutable std::size_t contentStreamCacheHits_{};
    mutable std::size_t contentStreamCacheMisses_{};
};

} // namespace CPPPdf

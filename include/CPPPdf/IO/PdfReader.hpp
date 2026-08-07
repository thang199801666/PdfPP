#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace CPPPdf {

enum class PdfOpenMode { ReadOnly, Modify, Append };

struct PdfReaderLimits {
    // Maximum serialized size of one indirect object materialized by the reader.
    // This prevents malformed xref entries from forcing unbounded allocations.
    std::size_t maxIndirectObjectBytes{256ULL * 1024ULL * 1024ULL};
    std::size_t maxObjectCount{5'000'000};
    std::size_t maxRecursionDepth{256};
    std::size_t maxDecodedStreamSize{512ULL * 1024ULL * 1024ULL};
    // Maximum decoded-to-encoded byte ratio per filter stage. Zero disables
    // ratio checking while maxDecodedStreamSize remains enforced.
    std::size_t maxDecodedStreamExpansionRatio{10'000U};
    std::size_t maxObjectStreamObjects{100'000};
    std::size_t maxPageCount{1'000'000};
    // Maximum number of parsed indirect objects retained in the resolver LRU cache.
    // Zero disables retention while preserving cycle/recursion checks.
    std::size_t maxCachedObjects{16'384};
    // Decoded /ObjStm data is substantially more expensive than parsed object
    // metadata, so it has independent count and memory budgets. Either zero
    // value disables decoded object-stream retention.
    std::size_t maxCachedObjectStreams{64};
    std::size_t maxCachedObjectStreamBytes{128ULL * 1024ULL * 1024ULL};
    // Maximum number of indirect font resources retained by a document.
    std::size_t maxCachedFontResources{256};
    // Decoded page/content streams have independent count and byte budgets.
    std::size_t maxCachedContentStreams{128};
    std::size_t maxCachedContentStreamBytes{64ULL * 1024ULL * 1024ULL};
    // Files at or above this size opened by path use a read-only mapping.
    // Set to zero to keep the buffered file source for every path.
    std::uint64_t memoryMapThresholdBytes{256ULL * 1024ULL * 1024ULL};
};

struct PdfReaderOptions {
    PdfOpenMode mode{PdfOpenMode::ReadOnly};
    bool repairDamagedXref{true};
    bool strictParsing{false};
    // Empty also attempts the standard empty user password. If that fails the
    // document remains open for metadata inspection and reports PasswordRequired.
    std::string password;
    PdfReaderLimits limits{};
};

class PdfInputSource {
public:
    virtual ~PdfInputSource() = default;

    [[nodiscard]] virtual std::uint64_t Size() const = 0;
    virtual void Read(std::uint64_t offset, std::span<char> destination) = 0;
    [[nodiscard]] virtual std::span<const char> View() const noexcept { return {}; }

    // Compatibility helper. New parser code should prefer Size() + Read().
    [[nodiscard]] virtual std::vector<char> ReadAll();
};

class PdfFileInputSource final : public PdfInputSource {
public:
    explicit PdfFileInputSource(std::filesystem::path path);
    ~PdfFileInputSource() override;
    PdfFileInputSource(PdfFileInputSource&&) noexcept;
    PdfFileInputSource& operator=(PdfFileInputSource&&) noexcept;
    PdfFileInputSource(const PdfFileInputSource&) = delete;
    PdfFileInputSource& operator=(const PdfFileInputSource&) = delete;
    [[nodiscard]] std::uint64_t Size() const override;
    void Read(std::uint64_t offset, std::span<char> destination) override;
    [[nodiscard]] std::span<const char> View() const noexcept override;
    [[nodiscard]] std::vector<char> ReadAll() override;
private:
    std::filesystem::path path_;
    std::uint64_t size_{};
    class Impl;
    std::unique_ptr<Impl> impl_;
};


class PdfMappedFileInputSource final : public PdfInputSource {
public:
    explicit PdfMappedFileInputSource(std::filesystem::path path);
    ~PdfMappedFileInputSource() override;
    PdfMappedFileInputSource(PdfMappedFileInputSource&&) noexcept;
    PdfMappedFileInputSource& operator=(PdfMappedFileInputSource&&) noexcept;
    PdfMappedFileInputSource(const PdfMappedFileInputSource&) = delete;
    PdfMappedFileInputSource& operator=(const PdfMappedFileInputSource&) = delete;
    [[nodiscard]] std::uint64_t Size() const override;
    void Read(std::uint64_t offset, std::span<char> destination) override;
    [[nodiscard]] std::span<const char> View() const noexcept override;
    [[nodiscard]] std::vector<char> ReadAll() override;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class PdfMemoryInputSource final : public PdfInputSource {
public:
    explicit PdfMemoryInputSource(std::span<const std::byte> bytes);
    [[nodiscard]] std::uint64_t Size() const override;
    void Read(std::uint64_t offset, std::span<char> destination) override;
    [[nodiscard]] std::span<const char> View() const noexcept override;
private:
    std::vector<char> bytes_;
};

class PdfStreamInputSource final : public PdfInputSource {
public:
    explicit PdfStreamInputSource(std::istream& stream);
    [[nodiscard]] std::uint64_t Size() const override;
    void Read(std::uint64_t offset, std::span<char> destination) override;
private:
    std::istream& stream_;
    std::vector<char> bytes_;
};

} // namespace CPPPdf

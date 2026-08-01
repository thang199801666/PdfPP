#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <span>
#include <vector>

namespace CPPPdf {

enum class PdfOpenMode { ReadOnly, Modify, Append };

struct PdfReaderLimits {
    std::size_t maxObjectCount{5'000'000};
    std::size_t maxRecursionDepth{256};
    std::size_t maxDecodedStreamSize{512ULL * 1024ULL * 1024ULL};
    std::size_t maxObjectStreamObjects{100'000};
    std::size_t maxPageCount{1'000'000};
    // Maximum number of parsed indirect objects retained in the resolver LRU cache.
    // Zero disables retention while preserving cycle/recursion checks.
    std::size_t maxCachedObjects{16'384};
};

struct PdfReaderOptions {
    PdfOpenMode mode{PdfOpenMode::ReadOnly};
    bool repairDamagedXref{true};
    bool strictParsing{false};
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
    [[nodiscard]] std::uint64_t Size() const override;
    void Read(std::uint64_t offset, std::span<char> destination) override;
    [[nodiscard]] std::span<const char> View() const noexcept override;
    [[nodiscard]] std::vector<char> ReadAll() override;
private:
    std::filesystem::path path_;
    std::uint64_t size_{};
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

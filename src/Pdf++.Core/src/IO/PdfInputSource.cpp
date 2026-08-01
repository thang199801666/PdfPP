#include <CPPPdf/PdfReader.hpp>
#include <CPPPdf/PdfError.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace CPPPdf {
namespace {

void validateReadRange(std::uint64_t sourceSize,
                       std::uint64_t offset,
                       std::size_t requested) {
    if (offset > sourceSize || requested > sourceSize - offset) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "PDF input read is outside the available byte range.");
    }
}

} // namespace

std::vector<char> PdfInputSource::ReadAll() {
    const std::uint64_t size64 = Size();
    if (size64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "PDF input is too large for this process address space.");
    }

    std::vector<char> bytes(static_cast<std::size_t>(size64));
    if (!bytes.empty()) {
        Read(0U, bytes);
    }
    return bytes;
}

PdfFileInputSource::PdfFileInputSource(std::filesystem::path path)
    : path_(std::move(path)) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (error) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "Cannot determine PDF size: " + path_.string());
    }
    size_ = static_cast<std::uint64_t>(size);
}

std::uint64_t PdfFileInputSource::Size() const {
    return size_;
}

std::span<const char> PdfFileInputSource::View() const noexcept {
    return {};
}

void PdfFileInputSource::Read(std::uint64_t offset, std::span<char> destination) {
    validateReadRange(Size(), offset, destination.size());
    if (destination.empty()) {
        return;
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "Cannot open PDF file: " + path_.string());
    }

    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input || !input.read(destination.data(),
                              static_cast<std::streamsize>(destination.size()))) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "Cannot read PDF file: " + path_.string());
    }
}


std::vector<char> PdfFileInputSource::ReadAll() {
    if (size_ > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "PDF input is too large for this process address space.");
    }

    std::vector<char> bytes(static_cast<std::size_t>(size_));
    if (bytes.empty()) return bytes;

    std::ifstream input(path_, std::ios::binary);
    if (!input || !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "Cannot read PDF file: " + path_.string());
    }
    return bytes;
}

PdfMemoryInputSource::PdfMemoryInputSource(std::span<const std::byte> bytes) {
    bytes_.resize(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes_[i] = static_cast<char>(bytes[i]);
    }
}

std::uint64_t PdfMemoryInputSource::Size() const {
    return static_cast<std::uint64_t>(bytes_.size());
}

std::span<const char> PdfMemoryInputSource::View() const noexcept {
    return bytes_;
}

void PdfMemoryInputSource::Read(std::uint64_t offset, std::span<char> destination) {
    validateReadRange(Size(), offset, destination.size());
    if (!destination.empty()) {
        std::memcpy(destination.data(),
                    bytes_.data() + static_cast<std::size_t>(offset),
                    destination.size());
    }
}

PdfStreamInputSource::PdfStreamInputSource(std::istream& stream)
    : stream_(stream),
      bytes_(std::istreambuf_iterator<char>(stream_),
             std::istreambuf_iterator<char>()) {}

std::uint64_t PdfStreamInputSource::Size() const {
    return static_cast<std::uint64_t>(bytes_.size());
}

void PdfStreamInputSource::Read(std::uint64_t offset, std::span<char> destination) {
    validateReadRange(Size(), offset, destination.size());
    if (!destination.empty()) {
        std::memcpy(destination.data(),
                    bytes_.data() + static_cast<std::size_t>(offset),
                    destination.size());
    }
}


class PdfMappedFileInputSource::Impl final {
public:
    explicit Impl(std::filesystem::path sourcePath) : path(std::move(sourcePath)) {
#if defined(_WIN32)
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open mapped PDF file.");
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(file, &length) || length.QuadPart < 0) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot determine mapped PDF size.");
        size = static_cast<std::uint64_t>(length.QuadPart);
        if (size != 0U) {
            mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (mapping == nullptr) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot create PDF file mapping.");
            data = static_cast<const char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
            if (data == nullptr) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot map PDF file.");
        }
#else
        descriptor = ::open(path.c_str(), O_RDONLY);
        if (descriptor < 0) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open mapped PDF file: " + path.string());
        struct stat status{};
        if (::fstat(descriptor, &status) != 0 || status.st_size < 0) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot determine mapped PDF size.");
        size = static_cast<std::uint64_t>(status.st_size);
        if (size != 0U) {
            void* mapped = ::mmap(nullptr, static_cast<std::size_t>(size), PROT_READ, MAP_PRIVATE, descriptor, 0);
            if (mapped == MAP_FAILED) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot map PDF file: " + path.string());
            data = static_cast<const char*>(mapped);
        }
#endif
    }

    ~Impl() {
#if defined(_WIN32)
        if (data != nullptr) UnmapViewOfFile(data);
        if (mapping != nullptr) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
#else
        if (data != nullptr) ::munmap(const_cast<char*>(data), static_cast<std::size_t>(size));
        if (descriptor >= 0) ::close(descriptor);
#endif
    }

    std::filesystem::path path;
    std::uint64_t size{};
    const char* data{};
#if defined(_WIN32)
    HANDLE file{INVALID_HANDLE_VALUE};
    HANDLE mapping{};
#else
    int descriptor{-1};
#endif
};

PdfMappedFileInputSource::PdfMappedFileInputSource(std::filesystem::path path)
    : impl_(std::make_unique<Impl>(std::move(path))) {}
PdfMappedFileInputSource::~PdfMappedFileInputSource() = default;
PdfMappedFileInputSource::PdfMappedFileInputSource(PdfMappedFileInputSource&&) noexcept = default;
PdfMappedFileInputSource& PdfMappedFileInputSource::operator=(PdfMappedFileInputSource&&) noexcept = default;
std::uint64_t PdfMappedFileInputSource::Size() const { return impl_->size; }
std::span<const char> PdfMappedFileInputSource::View() const noexcept {
    return {impl_->data, static_cast<std::size_t>(impl_->size)};
}
void PdfMappedFileInputSource::Read(const std::uint64_t offset, const std::span<char> destination) {
    validateReadRange(impl_->size, offset, destination.size());
    if (!destination.empty()) std::memcpy(destination.data(), impl_->data + static_cast<std::size_t>(offset), destination.size());
}
std::vector<char> PdfMappedFileInputSource::ReadAll() {
    if (impl_->size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Mapped PDF is too large for this process address space.");
    }
    if (impl_->size == 0U) return {};
    return std::vector<char>(impl_->data, impl_->data + static_cast<std::size_t>(impl_->size));
}

} // namespace CPPPdf

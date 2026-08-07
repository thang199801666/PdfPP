#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace CPPPdf {

struct PdfReference;
class PdfObject;
class PdfArray;
class PdfDictionary;
class PdfStream;

enum class PdfObjectType {
    Null,
    Boolean,
    Integer,
    Real,
    Name,
    String,
    Array,
    Dictionary,
    Stream,
    IndirectReference
};

class PdfName final {
public:
    PdfName() = default;
    explicit PdfName(std::string value);
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
    friend bool operator==(const PdfName&, const PdfName&) = default;

    static const PdfName Type;
    static const PdfName Page;
    static const PdfName Pages;
    static const PdfName Contents;
    static const PdfName Resources;
    static const PdfName Font;
    static const PdfName MediaBox;
    static const PdfName CropBox;
    static const PdfName Rotate;
    static const PdfName Root;
    static const PdfName Info;

private:
    std::string value_;
};

struct PdfNameHash final {
    std::size_t operator()(const PdfName& name) const noexcept;
};

class PdfArray final {
public:
    using Storage = std::vector<PdfObject>;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const PdfObject& at(std::size_t index) const;
    [[nodiscard]] PdfObject& at(std::size_t index);
    void reserve(std::size_t capacity);
    void push_back(PdfObject value);
    [[nodiscard]] const Storage& values() const noexcept;
private:
    Storage values_;
};

class PdfDictionary final {
public:
    using Storage = std::unordered_map<PdfName, PdfObject, PdfNameHash>;
    [[nodiscard]] bool Contains(const PdfName& key) const;
    [[nodiscard]] const PdfObject* Find(const PdfName& key) const noexcept;
    [[nodiscard]] PdfObject* Find(const PdfName& key) noexcept;
    [[nodiscard]] const PdfObject& Get(const PdfName& key) const;
    [[nodiscard]] std::optional<PdfName> GetAsName(const PdfName& key) const;
    [[nodiscard]] const PdfArray* GetAsArray(const PdfName& key) const noexcept;
    [[nodiscard]] const PdfDictionary* GetAsDictionary(const PdfName& key) const noexcept;
    void reserve(std::size_t capacity);
    void Put(PdfName key, PdfObject value);
    bool Remove(const PdfName& key);
    [[nodiscard]] const Storage& values() const noexcept;
private:
    Storage values_;
};

class PdfStream final {
public:
    PdfStream() = default;
    PdfStream(PdfDictionary dictionary, std::vector<std::byte> bytes);
    [[nodiscard]] const PdfDictionary& dictionary() const noexcept { return dictionary_; }
    [[nodiscard]] PdfDictionary& dictionary() noexcept { return dictionary_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
private:
    PdfDictionary dictionary_;
    std::vector<std::byte> bytes_;
};

class PdfObject final {
public:
    using ArrayPtr = std::shared_ptr<PdfArray>;
    using DictionaryPtr = std::shared_ptr<PdfDictionary>;
    using StreamPtr = std::shared_ptr<PdfStream>;
    using Value = std::variant<std::monostate, bool, std::int64_t, double, PdfName, std::string,
                               ArrayPtr, DictionaryPtr, StreamPtr, std::uint64_t>;

    PdfObject() = default;
    explicit PdfObject(bool value);
    explicit PdfObject(std::int64_t value);
    explicit PdfObject(double value);
    explicit PdfObject(PdfName value);
    explicit PdfObject(std::string value);
    explicit PdfObject(PdfArray value);
    explicit PdfObject(PdfDictionary value);
    explicit PdfObject(PdfStream value);
    static PdfObject IndirectReference(std::uint32_t objectNumber, std::uint16_t generation = 0);

    [[nodiscard]] PdfObjectType type() const noexcept { return type_; }
    [[nodiscard]] bool IsNull() const noexcept { return type_ == PdfObjectType::Null; }
    [[nodiscard]] const PdfName* AsName() const noexcept;
    [[nodiscard]] const std::string* AsString() const noexcept;
    [[nodiscard]] const PdfArray* AsArray() const noexcept;
    [[nodiscard]] const PdfDictionary* AsDictionary() const noexcept;
    [[nodiscard]] const PdfStream* AsStream() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> AsInteger() const noexcept;
    [[nodiscard]] std::optional<double> AsReal() const noexcept;
    [[nodiscard]] std::optional<bool> AsBoolean() const noexcept;
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint16_t>> AsReference() const noexcept;

private:
    PdfObjectType type_{PdfObjectType::Null};
    Value value_{};
};

// These definitions must appear after PdfObject is complete. Keeping them in
// the earlier class bodies is accepted by some standard libraries but fails
// with Clang when vector/unordered_map pointer arithmetic is instantiated.
inline std::size_t PdfArray::size() const noexcept { return values_.size(); }
inline bool PdfArray::empty() const noexcept { return values_.empty(); }
inline const PdfArray::Storage& PdfArray::values() const noexcept { return values_; }
inline const PdfDictionary::Storage& PdfDictionary::values() const noexcept { return values_; }

} // namespace CPPPdf

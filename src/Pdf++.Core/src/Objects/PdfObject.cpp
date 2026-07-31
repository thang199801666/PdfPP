#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include <functional>
#include <stdexcept>

namespace CPPPdf {

const PdfName PdfName::Type{"Type"};
const PdfName PdfName::Page{"Page"};
const PdfName PdfName::Pages{"Pages"};
const PdfName PdfName::Contents{"Contents"};
const PdfName PdfName::Resources{"Resources"};
const PdfName PdfName::Font{"Font"};
const PdfName PdfName::MediaBox{"MediaBox"};
const PdfName PdfName::CropBox{"CropBox"};
const PdfName PdfName::Rotate{"Rotate"};
const PdfName PdfName::Root{"Root"};
const PdfName PdfName::Info{"Info"};

PdfName::PdfName(std::string value) : value_(std::move(value)) {
    if (!value_.empty() && value_.front() == '/') value_.erase(value_.begin());
}
std::size_t PdfNameHash::operator()(const PdfName& name) const noexcept {
    return std::hash<std::string>{}(name.value());
}
const PdfObject& PdfArray::at(std::size_t index) const { return values_.at(index); }
PdfObject& PdfArray::at(std::size_t index) { return values_.at(index); }
void PdfArray::push_back(PdfObject value) { values_.push_back(std::move(value)); }

bool PdfDictionary::Contains(const PdfName& key) const { return values_.contains(key); }
const PdfObject* PdfDictionary::Find(const PdfName& key) const noexcept {
    const auto it = values_.find(key); return it == values_.end() ? nullptr : &it->second;
}
PdfObject* PdfDictionary::Find(const PdfName& key) noexcept {
    const auto it = values_.find(key); return it == values_.end() ? nullptr : &it->second;
}
const PdfObject& PdfDictionary::Get(const PdfName& key) const {
    const auto* value = Find(key);
    if (!value) throw PdfException(PdfErrorCode::MalformedObject, "Missing dictionary key /" + key.value());
    return *value;
}
std::optional<PdfName> PdfDictionary::GetAsName(const PdfName& key) const {
    const auto* value = Find(key); if (!value) return std::nullopt;
    const auto* name = value->AsName(); return name ? std::optional<PdfName>(*name) : std::nullopt;
}
const PdfArray* PdfDictionary::GetAsArray(const PdfName& key) const noexcept {
    const auto* value = Find(key); return value ? value->AsArray() : nullptr;
}
const PdfDictionary* PdfDictionary::GetAsDictionary(const PdfName& key) const noexcept {
    const auto* value = Find(key); return value ? value->AsDictionary() : nullptr;
}
void PdfDictionary::Put(PdfName key, PdfObject value) { values_.insert_or_assign(std::move(key), std::move(value)); }
bool PdfDictionary::Remove(const PdfName& key) { return values_.erase(key) != 0; }

PdfStream::PdfStream(PdfDictionary dictionary, std::vector<std::byte> bytes)
    : dictionary_(std::move(dictionary)), bytes_(std::move(bytes)) {}

PdfObject::PdfObject(bool value) : type_(PdfObjectType::Boolean), value_(value) {}
PdfObject::PdfObject(std::int64_t value) : type_(PdfObjectType::Integer), value_(value) {}
PdfObject::PdfObject(double value) : type_(PdfObjectType::Real), value_(value) {}
PdfObject::PdfObject(PdfName value) : type_(PdfObjectType::Name), value_(std::move(value)) {}
PdfObject::PdfObject(std::string value) : type_(PdfObjectType::String), value_(std::move(value)) {}
PdfObject::PdfObject(PdfArray value) : type_(PdfObjectType::Array), value_(std::make_shared<PdfArray>(std::move(value))) {}
PdfObject::PdfObject(PdfDictionary value) : type_(PdfObjectType::Dictionary), value_(std::make_shared<PdfDictionary>(std::move(value))) {}
PdfObject::PdfObject(PdfStream value) : type_(PdfObjectType::Stream), value_(std::make_shared<PdfStream>(std::move(value))) {}
PdfObject PdfObject::IndirectReference(std::uint32_t objectNumber, std::uint16_t generation) {
    PdfObject result; result.type_ = PdfObjectType::IndirectReference;
    result.value_ = (static_cast<std::uint64_t>(objectNumber) << 16U) | generation; return result;
}
const PdfName* PdfObject::AsName() const noexcept { return std::get_if<PdfName>(&value_); }
const std::string* PdfObject::AsString() const noexcept { return std::get_if<std::string>(&value_); }
const PdfArray* PdfObject::AsArray() const noexcept { const auto* p=std::get_if<ArrayPtr>(&value_); return p&&*p?p->get():nullptr; }
const PdfDictionary* PdfObject::AsDictionary() const noexcept { const auto* p=std::get_if<DictionaryPtr>(&value_); return p&&*p?p->get():nullptr; }
const PdfStream* PdfObject::AsStream() const noexcept { const auto* p=std::get_if<StreamPtr>(&value_); return p&&*p?p->get():nullptr; }
std::optional<std::int64_t> PdfObject::AsInteger() const noexcept { if (auto p=std::get_if<std::int64_t>(&value_)) return *p; return std::nullopt; }
std::optional<double> PdfObject::AsReal() const noexcept { if (auto p=std::get_if<double>(&value_)) return *p; if(auto i=AsInteger()) return static_cast<double>(*i); return std::nullopt; }
std::optional<bool> PdfObject::AsBoolean() const noexcept { if (auto p=std::get_if<bool>(&value_)) return *p; return std::nullopt; }
std::optional<std::pair<std::uint32_t, std::uint16_t>> PdfObject::AsReference() const noexcept {
    if (type_ != PdfObjectType::IndirectReference) {
        return std::nullopt;
    }
    const auto* reference = std::get_if<std::uint64_t>(&value_);
    if (reference == nullptr) {
        return std::nullopt;
    }
    return std::pair{static_cast<std::uint32_t>(*reference >> 16U),
                     static_cast<std::uint16_t>(*reference & 0xFFFFU)};
}

} // namespace CPPPdf

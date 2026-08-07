#include <CPPPdf/Security/PdfPades.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Writer/PdfIncrementalWriter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace CPPPdf {
namespace {

std::string readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open input PDF: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

const PdfObject* resolveObject(const PdfDocument& document, const PdfObject* value) {
    if (value == nullptr) return nullptr;
    if (const auto reference = value->AsReference()) {
        return &document.GetObject({reference->first, reference->second});
    }
    return value;
}

const PdfDictionary* resolveDictionary(const PdfDocument& document, const PdfObject* value) {
    const auto* resolved = resolveObject(document, value);
    if (resolved == nullptr) return nullptr;
    if (const auto* dictionary = resolved->AsDictionary()) return dictionary;
    if (const auto* stream = resolved->AsStream()) return &stream->dictionary();
    return nullptr;
}

PdfArray copyResolvedArray(const PdfDocument& document, const PdfObject* value) {
    PdfArray result;
    const auto* resolved = resolveObject(document, value);
    const auto* array = resolved == nullptr ? nullptr : resolved->AsArray();
    if (array == nullptr) return result;
    result.reserve(array->size());
    for (const auto& item : array->values()) result.push_back(item);
    return result;
}

std::uint32_t rotateLeft(const std::uint32_t value, const unsigned int amount) noexcept {
    return std::rotl(value, static_cast<int>(amount));
}

std::array<std::uint8_t, 20> sha1(const std::span<const std::byte> input) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(input.size() + 72U);
    for (const auto value : input) bytes.push_back(std::to_integer<std::uint8_t>(value));
    const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8U;
    bytes.push_back(std::uint8_t{0x80});
    while ((bytes.size() % 64U) != 56U) bytes.push_back(std::uint8_t{0});
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xFFU));
    }

    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xEFCDAB89U;
    std::uint32_t h2 = 0x98BADCFEU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xC3D2E1F0U;

    for (std::size_t offset = 0U; offset < bytes.size(); offset += 64U) {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0U; index < 16U; ++index) {
            const std::size_t p = offset + index * 4U;
            words[index] = (static_cast<std::uint32_t>(bytes[p]) << 24U) |
                           (static_cast<std::uint32_t>(bytes[p + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(bytes[p + 2U]) << 8U) |
                           static_cast<std::uint32_t>(bytes[p + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            words[index] = rotateLeft(words[index - 3U] ^ words[index - 8U] ^
                                      words[index - 14U] ^ words[index - 16U], 1U);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;
        for (std::size_t index = 0U; index < 80U; ++index) {
            std::uint32_t f{};
            std::uint32_t k{};
            if (index < 20U) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            } else if (index < 40U) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            } else if (index < 60U) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }
            const std::uint32_t temporary = rotateLeft(a, 5U) + f + e + k + words[index];
            e = d;
            d = c;
            c = rotateLeft(b, 30U);
            b = a;
            a = temporary;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    const std::array<std::uint32_t, 5> state{h0, h1, h2, h3, h4};
    std::array<std::uint8_t, 20> digest{};
    for (std::size_t index = 0U; index < state.size(); ++index) {
        digest[index * 4U] = static_cast<std::uint8_t>(state[index] >> 24U);
        digest[index * 4U + 1U] = static_cast<std::uint8_t>(state[index] >> 16U);
        digest[index * 4U + 2U] = static_cast<std::uint8_t>(state[index] >> 8U);
        digest[index * 4U + 3U] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

std::string uppercaseHex(const std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(bytes.size() * 2U);
    for (const auto value : bytes) {
        output.push_back(digits[value >> 4U]);
        output.push_back(digits[value & 0x0FU]);
    }
    return output;
}

std::string resolveVriKey(const PdfDss::DssOptions& options) {
    if (!options.signatureContents.empty()) {
        const auto digest = sha1(options.signatureContents);
        return uppercaseHex(digest);
    }
    return options.vriSignatureName.empty() ? "Signature1" : options.vriSignatureName;
}

PdfReference writeBlob(Internal::PdfIncrementalWriter& writer, std::uint32_t& nextObject,
                       const std::span<const std::byte> bytes) {
    const PdfReference reference{nextObject++, 0U};
    std::ostringstream body;
    body << "<< /Length " << bytes.size() << " >>\nstream\n";
    body.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    body << "\nendstream";
    writer.WriteRawObject(reference, body.str());
    return reference;
}

void appendBlobReferences(Internal::PdfIncrementalWriter& writer, std::uint32_t& nextObject,
                          const std::vector<std::vector<std::byte>>& blobs,
                          PdfArray& destination, PdfArray* vriDestination = nullptr) {
    for (const auto& blob : blobs) {
        const auto reference = writeBlob(writer, nextObject, blob);
        const auto object = PdfObject::IndirectReference(reference.objectNumber, reference.generation);
        destination.push_back(object);
        if (vriDestination != nullptr) vriDestination->push_back(object);
    }
}

void putArrayIfNotEmpty(PdfDictionary& dictionary, const char* key, PdfArray array) {
    if (!array.empty()) dictionary.Put(PdfName(key), PdfObject(std::move(array)));
}

} // namespace

PdfDss::DssResult PdfDss::AddDocumentSecurityStore(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const DssOptions& options,
    const PdfReaderOptions& readerOptions) {
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    DssResult result;
    result.outputPath = outputPath;
    result.certificateCount = options.certificates.size();
    result.crlCount = options.crls.size();
    result.ocspCount = options.ocspResponses.size();
    result.timestampCount = options.timestamps.size();
    result.vriKey = resolveVriKey(options);

    const bool hasMaterial = !options.certificates.empty() || !options.crls.empty() ||
        !options.ocspResponses.empty() || !options.timestamps.empty();
    if (!hasMaterial) {
        const std::string source = readFileBytes(inputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        return result;
    }

    const PdfReference catalogReference = document.GetCatalogReference();
    const PdfObject& catalogObject = document.GetObject(catalogReference);
    const PdfDictionary* catalog = catalogObject.AsDictionary();
    if (!catalog) throw PdfException(PdfErrorCode::MalformedObject, "Catalog is not a dictionary.");

    PdfDictionary dss;
    if (const auto* existing = resolveDictionary(document, catalog->Find(PdfName("DSS")))) dss = *existing;
    dss.Put(PdfName("Type"), PdfObject(PdfName("DSS")));

    PdfArray certificateReferences = copyResolvedArray(document, dss.Find(PdfName("Certs")));
    PdfArray crlReferences = copyResolvedArray(document, dss.Find(PdfName("CRLs")));
    PdfArray ocspReferences = copyResolvedArray(document, dss.Find(PdfName("OCSPs")));

    Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
    std::uint32_t nextObject = Internal::PdfIncrementalWriter::NextObjectNumber(document);

    PdfArray vriCertificateReferences;
    PdfArray vriCrlReferences;
    PdfArray vriOcspReferences;
    PdfArray vriTimestampReferences;
    appendBlobReferences(writer, nextObject, options.certificates,
                         certificateReferences, &vriCertificateReferences);
    appendBlobReferences(writer, nextObject, options.crls,
                         crlReferences, &vriCrlReferences);
    appendBlobReferences(writer, nextObject, options.ocspResponses,
                         ocspReferences, &vriOcspReferences);
    PdfArray unusedTimestampGlobal;
    appendBlobReferences(writer, nextObject, options.timestamps,
                         unusedTimestampGlobal, &vriTimestampReferences);

    putArrayIfNotEmpty(dss, "Certs", std::move(certificateReferences));
    putArrayIfNotEmpty(dss, "CRLs", std::move(crlReferences));
    putArrayIfNotEmpty(dss, "OCSPs", std::move(ocspReferences));

    if (options.includeVriEntry) {
        PdfDictionary vriDictionary;
        if (const auto* existing = resolveDictionary(document, dss.Find(PdfName("VRI")))) {
            vriDictionary = *existing;
        }

        PdfDictionary vriEntry;
        if (const auto* existingEntry = resolveDictionary(
                document, vriDictionary.Find(PdfName(result.vriKey)))) {
            vriEntry = *existingEntry;
        }
        auto mergeVriArray = [&](const char* key, PdfArray newValues) {
            PdfArray merged = copyResolvedArray(document, vriEntry.Find(PdfName(key)));
            for (const auto& value : newValues.values()) merged.push_back(value);
            putArrayIfNotEmpty(vriEntry, key, std::move(merged));
        };
        mergeVriArray("Cert", std::move(vriCertificateReferences));
        mergeVriArray("CRL", std::move(vriCrlReferences));
        mergeVriArray("OCSP", std::move(vriOcspReferences));
        mergeVriArray("TS", std::move(vriTimestampReferences));
        if (!options.validationTime.empty()) {
            vriEntry.Put(PdfName("TU"), PdfObject(options.validationTime));
        }
        vriDictionary.Put(PdfName(result.vriKey), PdfObject(std::move(vriEntry)));
        dss.Put(PdfName("VRI"), PdfObject(std::move(vriDictionary)));
    }

    PdfDictionary updatedCatalog = *catalog;
    updatedCatalog.Put(PdfName("DSS"), PdfObject(std::move(dss)));
    writer.WriteDictionary(catalogReference, updatedCatalog);
    writer.Finish(nextObject);
    return result;
}

bool PdfDss::HasDocumentSecurityStore(
    const std::filesystem::path& path,
    const PdfReaderOptions& readerOptions) {
    const PdfDocument document = PdfDocument::Open(path, readerOptions);
    const PdfReference catalogReference = document.GetCatalogReference();
    const PdfObject& catalogObject = document.GetObject(catalogReference);
    const PdfDictionary* catalog = catalogObject.AsDictionary();
    return catalog != nullptr && catalog->Contains(PdfName("DSS"));
}

} // namespace CPPPdf

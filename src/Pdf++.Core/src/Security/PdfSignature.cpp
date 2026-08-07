#include <CPPPdf/Security/PdfSignature.hpp>
#include <CPPPdf/Security/PdfCms.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/PdfError.hpp>
#include <CPPPdf/Forms/PdfAcroForm.hpp>
#include "Internal/Writer/PdfIncrementalWriter.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"
#include "Internal/Security/PdfCrypto.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf {
namespace {

std::string readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open PDF: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeFileBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot write PDF: " + path.string());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw PdfException(PdfErrorCode::FileOpenFailed, "Failed while writing PDF: " + path.string());
}

std::uint32_t nextObjectNumber(const PdfDocument& document) {
    std::uint32_t maximum = 0U;
    for (const auto number : document.objectNumbers()) maximum = std::max(maximum, number);
    if (maximum == std::numeric_limits<std::uint32_t>::max()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature, "No free PDF object number remains.");
    }
    return maximum + 1U;
}

std::string escapeLiteral(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8U);
    for (const char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '(': result += "\\("; break;
        case ')': result += "\\)"; break;
        case '\r': result += "\\r"; break;
        case '\n': result += "\\n"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

std::string pdfDateString() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_MSC_VER)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream output;
    output << "D:" << std::put_time(&utc, "%Y%m%d%H%M%S") << "+00'00'";
    return output.str();
}

std::string subFilterName(const PdfSignatureSubFilter subFilter) {
    switch (subFilter) {
    case PdfSignatureSubFilter::AdbePkcs7Sha1: return "adbe.pkcs7.sha1";
    case PdfSignatureSubFilter::EtsiCAdESDetached: return "ETSI.CAdES.detached";
    case PdfSignatureSubFilter::AdbePkcs7Detached: return "adbe.pkcs7.detached";
    case PdfSignatureSubFilter::None: return {};
    }
    return {};
}

std::string hexEncode(std::span<const std::byte> bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        const auto value = std::to_integer<std::uint8_t>(byte);
        output.push_back(digits[value >> 4U]);
        output.push_back(digits[value & 0x0FU]);
    }
    return output;
}

// Locates the signature /Contents placeholder. Returns the byte offset of the
// '<' character, or npos when the placeholder is missing.
std::size_t findContentsPlaceholder(const std::string& bytes, const std::size_t hexLength) {
    const std::string pattern = "/Contents <" + std::string(hexLength, '0') + ">";
    const auto found = bytes.find(pattern);
    if (found == std::string::npos) return std::string::npos;
    return found + pattern.find('<');
}

// Locates the /ByteRange array placeholder and overwrites its three numeric
// fields in place (fixed 20-digit width) so the file size never changes.
void patchByteRange(std::string& bytes, const std::array<std::uint64_t, 4>& range) {
    const std::string marker = "/ByteRange [0 00000000000000000000 00000000000000000000 00000000000000000000]";
    const auto pos = bytes.find(marker);
    if (pos == std::string::npos) {
        throw PdfException(PdfErrorCode::MalformedObject, "Signature ByteRange placeholder not found.");
    }
    const auto writeField = [&](const std::size_t offset, const std::uint64_t value) {
        std::ostringstream field;
        field << std::setw(20) << std::setfill('0') << value;
        for (std::size_t i = 0; i < 20U; ++i) bytes[pos + offset + i] = field.str()[i];
    };
    // Layout: "/ByteRange [0 " then three 20-digit fields separated by spaces,
    // terminated by ']'. The literal '0' is range[0]; the three placeholder
    // fields hold range[1], range[2], and range[3].
    writeField(14U, range[1]);
    writeField(35U, range[2]);
    writeField(56U, range[3]);
}

std::array<std::uint64_t, 4> arrayToByteRange(const PdfArray& array) {
    std::array<std::uint64_t, 4> result{};
    for (std::size_t i = 0; i < 4U && i < array.size(); ++i) {
        const auto integer = array.at(i).AsInteger();
        if (!integer || *integer < 0) return {};
        result[i] = static_cast<std::uint64_t>(*integer);
    }
    return result;
}

PdfObject rectangleArray(const PdfRectangle& rectangle) {
    PdfArray array;
    array.push_back(PdfObject(rectangle.left));
    array.push_back(PdfObject(rectangle.bottom));
    array.push_back(PdfObject(rectangle.right));
    array.push_back(PdfObject(rectangle.top));
    return PdfObject(std::move(array));
}

} // namespace

PdfSignaturePreparation PdfSignatureManager::PrepareForSigning(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const PdfSignatureFieldOptions& options,
    const PdfReaderOptions& readerOptions) {
    if (inputPath == outputPath) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Signature preparation requires different input and output paths.");
    }
    if (options.contentsSize == 0U || options.contentsSize > 1U << 20U) {
        throw PdfException(PdfErrorCode::InvalidArgument, "Invalid signature contents size.");
    }
    PdfDocument document = PdfDocument::Open(inputPath, readerOptions);
    if (document.IsEncrypted()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "Signing encrypted documents is not yet supported.");
    }
    const PdfSignatureSubFilter subFilter = options.subFilter;
    const std::string subFilterText = subFilterName(subFilter);

    std::uint32_t objectNumber = nextObjectNumber(document);
    const PdfReference signatureReference{objectNumber++, 0U};
    const PdfReference fieldReference{objectNumber++, 0U};
    const PdfReference widgetReference{objectNumber++, 0U};

    // Signature dictionary body is serialized manually so the /ByteRange and
    // /Contents placeholders have a known, fixed byte layout for in-place
    // patching.
    const std::size_t hexLength = 2U * options.contentsSize;
    std::ostringstream signatureBody;
    signatureBody << "<< /Type /Sig /Filter /" << escapeLiteral(options.filter)
                  << " /SubFilter /" << subFilterText
                  << "\n/ByteRange [0 00000000000000000000 00000000000000000000 00000000000000000000]"
                  << "\n/Contents <" << std::string(hexLength, '0') << '>'
                  << "\n/M (" << escapeLiteral(pdfDateString()) << ')';
    if (!options.signerName.empty()) signatureBody << "\n/Name (" << escapeLiteral(options.signerName) << ')';
    if (!options.reason.empty()) signatureBody << "\n/Reason (" << escapeLiteral(options.reason) << ')';
    if (!options.location.empty()) signatureBody << "\n/Location (" << escapeLiteral(options.location) << ')';
    if (!options.contactInfo.empty()) signatureBody << "\n/ContactInfo (" << escapeLiteral(options.contactInfo) << ')';
    signatureBody << " >>";

    const PdfReference catalogReference = document.GetCatalogReference();
    const PdfDictionary* catalog = document.GetObject(catalogReference).AsDictionary();
    if (!catalog) throw PdfException(PdfErrorCode::MalformedObject, "Catalog is not a dictionary.");

    // Resolve or create the AcroForm dictionary.
    PdfDictionary acroForm;
    std::optional<PdfReference> acroFormReference;
    if (const PdfObject* existing = catalog->Find(PdfName("AcroForm"))) {
        if (const auto reference = existing->AsReference()) {
            acroFormReference = PdfReference{reference->first, reference->second};
            const PdfDictionary* form = document.GetObject(*acroFormReference).AsDictionary();
            if (!form) throw PdfException(PdfErrorCode::MalformedObject, "AcroForm is not a dictionary.");
            acroForm = *form;
        } else if (const PdfDictionary* form = existing->AsDictionary()) {
            acroForm = *form;
        }
    } else {
        acroFormReference = PdfReference{objectNumber++, 0U};
    }
    PdfArray fields;
    if (const PdfArray* existingFields = acroForm.GetAsArray(PdfName("Fields"))) fields = *existingFields;
    fields.push_back(PdfObject::IndirectReference(fieldReference.objectNumber, fieldReference.generation));
    acroForm.Put(PdfName("Fields"), PdfObject(std::move(fields)));
    acroForm.Put(PdfName("SigFlags"), PdfObject(std::int64_t{3}));

    // Field and widget dictionaries.
    PdfDictionary field;
    field.Put(PdfName("FT"), PdfObject(PdfName("Sig")));
    field.Put(PdfName("T"), PdfObject(options.fieldName));
    field.Put(PdfName("Ff"), PdfObject(std::int64_t{0}));
    field.Put(PdfName("V"), PdfObject::IndirectReference(signatureReference.objectNumber));
    PdfArray kids;
    kids.push_back(PdfObject::IndirectReference(widgetReference.objectNumber, widgetReference.generation));
    field.Put(PdfName("Kids"), PdfObject(std::move(kids)));

    const PdfReference pageReference = document.GetPageReference(options.pageIndex);
    PdfDictionary widget;
    widget.Put(PdfName("Type"), PdfObject(PdfName("Annot")));
    widget.Put(PdfName("Subtype"), PdfObject(PdfName("Widget")));
    widget.Put(PdfName("Parent"), PdfObject::IndirectReference(fieldReference.objectNumber));
    widget.Put(PdfName("Rect"), rectangleArray(options.rectangle));
    widget.Put(PdfName("P"), PdfObject::IndirectReference(pageReference.objectNumber, pageReference.generation));
    widget.Put(PdfName("F"), PdfObject(std::int64_t{4}));

    // Updated page dictionary with the widget in /Annots.
    const PdfDictionary* page = document.GetObject(pageReference).AsDictionary();
    if (!page) throw PdfException(PdfErrorCode::MalformedObject, "Page object is not a dictionary.");
    PdfDictionary revisedPage = *page;
    PdfArray annots;
    if (const PdfObject* annotsObject = page->Find(PdfName("Annots"))) {
        if (const PdfArray* direct = annotsObject->AsArray()) {
            for (const auto& value : direct->values()) annots.push_back(value);
        } else if (const auto reference = annotsObject->AsReference()) {
            const PdfObject& resolved = document.GetObject(PdfReference{reference->first, reference->second});
            if (const PdfArray* array = resolved.AsArray()) {
                for (const auto& value : array->values()) annots.push_back(value);
            }
        }
    }
    annots.push_back(PdfObject::IndirectReference(widgetReference.objectNumber, widgetReference.generation));
    revisedPage.Put(PdfName("Annots"), PdfObject(std::move(annots)));

    // Updated catalog with the AcroForm reference.
    PdfDictionary revisedCatalog = *catalog;
    revisedCatalog.Put(PdfName("AcroForm"),
        PdfObject::IndirectReference(acroFormReference->objectNumber, acroFormReference->generation));

    {
        Internal::PdfIncrementalWriter writer(inputPath, outputPath, document);
        writer.WriteRawObject(signatureReference, signatureBody.str());
        std::ostringstream fieldBody;
        Internal::PdfObjectSerializer::WriteDictionary(fieldBody, field);
        writer.WriteRawObject(fieldReference, fieldBody.str());
        std::ostringstream widgetBody;
        Internal::PdfObjectSerializer::WriteDictionary(widgetBody, widget);
        writer.WriteRawObject(widgetReference, widgetBody.str());
        writer.WriteDictionary(pageReference, revisedPage);
        writer.WriteDictionary(catalogReference, revisedCatalog);
        if (acroFormReference.has_value()) writer.WriteDictionary(*acroFormReference, acroForm);
        writer.Finish(objectNumber);
    }

    // Locate the /Contents placeholder, compute the byte range, patch the
    // /ByteRange values in place, then derive the digest input.
    std::string fileBytes = readFileBytes(outputPath);
    const std::size_t contentsStart = findContentsPlaceholder(fileBytes, hexLength);
    if (contentsStart == std::string::npos) {
        throw PdfException(PdfErrorCode::MalformedObject, "Signature /Contents placeholder not found.");
    }
    const std::size_t contentsEnd = contentsStart + hexLength + 2U;
    const std::size_t fileSize = fileBytes.size();
    const std::array<std::uint64_t, 4> byteRange{
        0U,
        static_cast<std::uint64_t>(contentsStart),
        static_cast<std::uint64_t>(contentsEnd),
        static_cast<std::uint64_t>(fileSize - contentsEnd)
    };
    patchByteRange(fileBytes, byteRange);
    writeFileBytes(outputPath, fileBytes);

    PdfSignaturePreparation preparation;
    preparation.outputPath = outputPath;
    preparation.fieldName = options.fieldName;
    preparation.byteRange = byteRange;
    preparation.contentsOffset = contentsStart;
    preparation.contentsHexLength = hexLength;
    preparation.digestInput.reserve(contentsStart + (fileSize - contentsEnd));
    preparation.digestInput.insert(preparation.digestInput.end(),
        reinterpret_cast<const std::byte*>(fileBytes.data()),
        reinterpret_cast<const std::byte*>(fileBytes.data()) + contentsStart);
    preparation.digestInput.insert(preparation.digestInput.end(),
        reinterpret_cast<const std::byte*>(fileBytes.data()) + contentsEnd,
        reinterpret_cast<const std::byte*>(fileBytes.data()) + fileSize);
    return preparation;
}

void PdfSignatureManager::ApplySignature(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& outputPath,
    const std::span<const std::byte> signatureBytes) {
    std::string fileBytes = readFileBytes(preparedPath);
    // Locate the placeholder by scanning for a long run of hex zeros inside
    // /Contents; the signature value is then written over it.
    std::size_t contentsStart = std::string::npos;
    std::size_t hexLength = 0U;
    const std::string needle = "/Contents <";
    for (std::size_t pos = 0U; (pos = fileBytes.find(needle, pos)) != std::string::npos; pos += needle.size()) {
        std::size_t cursor = pos + needle.size();
        std::size_t zeros = 0U;
        while (cursor < fileBytes.size() && fileBytes[cursor] == '0') {
            ++zeros;
            ++cursor;
        }
        if (zeros >= 64U && cursor < fileBytes.size() && fileBytes[cursor] == '>') {
            contentsStart = pos + needle.size() - 1U;
            hexLength = zeros;
            break;
        }
    }
    if (contentsStart == std::string::npos || hexLength == 0U) {
        throw PdfException(PdfErrorCode::MalformedObject,
                           "Prepared signature /Contents placeholder not found.");
    }
    if (signatureBytes.size() > hexLength / 2U) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Signature value exceeds the reserved /Contents capacity.");
    }
    std::string hex = hexEncode(signatureBytes);
    hex.append(hexLength - hex.size(), '0');
    for (std::size_t i = 0; i < hexLength; ++i) {
        fileBytes[contentsStart + 1U + i] = hex[i];
    }
    writeFileBytes(outputPath, fileBytes);
}

std::filesystem::path PdfSignatureManager::Sign(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    const Signer& signer,
    const PdfSignatureFieldOptions& options,
    const PdfReaderOptions& readerOptions) {
    if (!signer) {
        throw PdfException(PdfErrorCode::InvalidArgument, "External signer callback is required.");
    }
    const auto preparation = PrepareForSigning(inputPath, outputPath, options, readerOptions);
    const auto signature = signer(preparation.digestInput);
    ApplySignature(preparation.outputPath, preparation.outputPath, signature);
    return preparation.outputPath;
}

std::vector<PdfSignatureInfo> PdfSignatureManager::GetSignatures(const PdfDocument& document) {
    std::vector<PdfSignatureInfo> result;
    const auto fields = PdfAcroForm::GetFields(document);
    for (const auto& field : fields) {
        if (field.type != PdfFormFieldType::Signature) continue;
        PdfSignatureInfo info;
        info.fieldName = field.name;
        info.fieldReference = field.reference;
        const PdfObject& fieldObject = document.GetObject(field.reference);
        const PdfDictionary* fieldDictionary = fieldObject.AsDictionary();
        if (!fieldDictionary) continue;
        const PdfObject* valueObject = fieldDictionary->Find(PdfName("V"));
        if (!valueObject) continue;
        const PdfDictionary* signature = nullptr;
        if (const auto reference = valueObject->AsReference()) {
            signature = document.GetObject(PdfReference{reference->first, reference->second}).AsDictionary();
        } else {
            signature = valueObject->AsDictionary();
        }
        if (!signature) continue;
        if (const auto name = signature->Find(PdfName("Name"))) {
            if (const std::string* text = name->AsString()) info.signerName = *text;
            else if (const PdfName* pdfName = name->AsName()) info.signerName = pdfName->value();
        }
        if (const auto reason = signature->Find(PdfName("Reason"))) {
            if (const std::string* text = reason->AsString()) info.reason = *text;
        }
        if (const auto location = signature->Find(PdfName("Location"))) {
            if (const std::string* text = location->AsString()) info.location = *text;
        }
        if (const auto modification = signature->Find(PdfName("M"))) {
            if (const std::string* text = modification->AsString()) info.modificationDate = *text;
        }
        if (const auto subFilter = signature->GetAsName(PdfName("SubFilter"))) info.subFilter = subFilter->value();
        if (const PdfArray* rangeArray = signature->GetAsArray(PdfName("ByteRange"))) {
            info.byteRange = arrayToByteRange(*rangeArray);
            info.hasByteRange = info.byteRange[0] == 0U || info.byteRange[1] != 0U;
        }
        if (const PdfObject* contents = signature->Find(PdfName("Contents"))) {
            if (const std::string* text = contents->AsString()) {
                info.contents.assign(reinterpret_cast<const std::byte*>(text->data()),
                                     reinterpret_cast<const std::byte*>(text->data()) + text->size());
                // A zero-filled placeholder means the signature has not been
                // applied yet.
                const bool allZero = std::all_of(info.contents.begin(), info.contents.end(),
                    [](const std::byte byte) { return byte == std::byte{0}; });
                info.hasContents = !info.contents.empty() && !allZero;
            }
        }
        result.push_back(std::move(info));
    }
    return result;
}

std::vector<PdfSignatureInfo> PdfSignatureManager::GetSignatures(
    const std::filesystem::path& path, const PdfReaderOptions& readerOptions) {
    const PdfDocument document = PdfDocument::Open(path, readerOptions);
    return GetSignatures(document);
}

namespace {

std::string readFileText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot open file for signature verification.");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// Locates the document file path for a PdfDocument (used to recompute the
// ByteRange digest). The reader keeps the source path internally when opened
// from a file; otherwise verification against a memory document is not possible.
std::string readDocumentBytes(const PdfDocument& document) {
    const auto path = document.GetPath();
    if (path.empty()) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Signature verification requires a file-backed document.");
    }
    return readFileText(path);
}

} // namespace

PdfSignatureVerification PdfSignatureManager::VerifySignature(
    const PdfDocument& document, const std::size_t signatureIndex) {
    const auto signatures = GetSignatures(document);
    if (signatureIndex >= signatures.size()) {
        return {PdfSignatureVerificationStatus::Malformed, "No signature field at the requested index.", {}};
    }
    const auto& info = signatures[signatureIndex];
    if (!info.hasContents || !info.hasByteRange) {
        return {PdfSignatureVerificationStatus::NotApplicable, "Signature field is empty or unsigned.", {}};
    }

    // Recompute the SHA-256 digest over the two ByteRange regions.
    const std::string file = readDocumentBytes(document);
    const std::size_t a = static_cast<std::size_t>(info.byteRange[0]);
    const std::size_t b = static_cast<std::size_t>(info.byteRange[1]);
    const std::size_t c = static_cast<std::size_t>(info.byteRange[2]);
    const std::size_t d = static_cast<std::size_t>(info.byteRange[3]);
    if (a + b > file.size() || c + d > file.size()) {
        return {PdfSignatureVerificationStatus::Malformed, "ByteRange extends beyond the file.", {}};
    }
    std::vector<std::uint8_t> digestInput;
    digestInput.reserve(b + d);
    for (std::size_t i = a; i < a + b; ++i) digestInput.push_back(static_cast<std::uint8_t>(file[i]));
    for (std::size_t i = c; i < c + d; ++i) digestInput.push_back(static_cast<std::uint8_t>(file[i]));
    const auto digest = Internal::Sha256(digestInput);

    // Parse the CMS/PKCS#7 value from /Contents (optional for digest check).
    std::vector<std::uint8_t> contents(info.contents.size());
    for (std::size_t i = 0; i < info.contents.size(); ++i) contents[i] = std::to_integer<std::uint8_t>(info.contents[i]);
    PdfCms::SignedDataInfo cmsInfo;
    const bool hasCms = PdfCms::ParseSignedData(contents, cmsInfo);
    PdfSignatureVerification result;
    result.status = PdfSignatureVerificationStatus::Valid;
    if (hasCms) {
        if (cmsInfo.certificates.empty()) {
            result.status = PdfSignatureVerificationStatus::Malformed;
            result.detail = "CMS SignedData does not contain a signer certificate.";
            return result;
        }
        const auto& certificateDer = cmsInfo.certificates.front();
        result.certificateDer.assign(reinterpret_cast<const std::byte*>(certificateDer.data()),
                                     reinterpret_cast<const std::byte*>(certificateDer.data()) + certificateDer.size());

        std::array<std::uint8_t, 32> signatureDigest = digest;
        if (cmsInfo.hasSignedAttributes) {
            if (!cmsInfo.hasContentType || !cmsInfo.hasMessageDigest || cmsInfo.signedAttributesDer.empty()) {
                result.status = PdfSignatureVerificationStatus::Malformed;
                result.detail = "CMS signed attributes are missing content-type or message-digest.";
                return result;
            }
            if (!std::equal(digest.begin(), digest.end(), cmsInfo.messageDigest.begin())) {
                result.status = PdfSignatureVerificationStatus::DigestMismatch;
                result.detail = "CMS message-digest does not match the PDF ByteRange digest.";
                return result;
            }
            signatureDigest = Internal::Sha256(cmsInfo.signedAttributesDer);
        }

        // Recover the RSA public key from the embedded certificate and verify
        // either the canonical signed-attributes digest or the legacy direct
        // ByteRange digest when no signed attributes are present.
        PdfCms::RsaPublicKey publicKey;
        if (!PdfCms::ParsePublicKeyFromCertificate(certificateDer, publicKey) ||
            !PdfCms::RsaSha256Verify(publicKey, signatureDigest, cmsInfo.signature)) {
            result.status = PdfSignatureVerificationStatus::InvalidSignature;
            result.detail = "RSA signature verification failed for the CMS signer information.";
            return result;
        }
        result.detail = cmsInfo.hasSignedAttributes
            ? "CMS signed attributes, SHA-256 ByteRange digest, and RSA signature verified."
            : "SHA-256 ByteRange digest and legacy RSA signature verified.";
    } else {
        result.detail = "ByteRange digest computed (no parseable CMS value for RSA verification).";
    }
    return result;
}

PdfSignatureVerification PdfSignatureManager::VerifySignature(
    const std::filesystem::path& path, const std::size_t signatureIndex,
    const PdfReaderOptions& readerOptions) {
    const PdfDocument document = PdfDocument::Open(path, readerOptions);
    return VerifySignature(document, signatureIndex);
}

} // namespace CPPPdf

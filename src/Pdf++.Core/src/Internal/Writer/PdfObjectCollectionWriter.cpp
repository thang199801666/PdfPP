#include "Internal/Writer/PdfObjectCollectionWriter.hpp"

#include <CPPPdf/Objects/PdfObject.hpp>
#include <CPPPdf/PdfError.hpp>

#include <zlib.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace CPPPdf::Internal {
namespace {

[[nodiscard]] std::string compressBytes(const std::span<const std::byte> input) {
    uLongf outputSize = compressBound(static_cast<uLong>(input.size()));
    std::string output(outputSize, '\0');
    const int status = compress2(
        reinterpret_cast<Bytef*>(output.data()), &outputSize,
        reinterpret_cast<const Bytef*>(input.data()), static_cast<uLong>(input.size()),
        Z_BEST_SPEED);
    if (status != Z_OK) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot compress PDF stream data.");
    output.resize(outputSize);
    return output;
}

} // namespace

void PdfObjectCollectionWriter::Write(std::ostream& out,
                                      const PdfObjectCollectionWriterOptions& options,
                                      const std::vector<std::string>& objects,
                                      const std::size_t catalogObject,
                                      const std::size_t infoObject,
                                      const std::size_t encryptionObject,
                                      const PdfStandardSecurity* security,
                                      const std::array<std::uint8_t, 16>& fileId) {
    const bool hasDocumentInfo = infoObject != 0U;
    out << "%PDF-" << options.pdfVersion << "\n%\xE2\xE3\xCF\xD3\n";

    std::vector<std::size_t> objectStreamMembers;
    if (options.writeXrefStream && options.writeObjectStreams) {
        for (std::size_t i = 1; i < objects.size(); ++i) {
            if (i == catalogObject || i == encryptionObject) continue;
            const std::string& body = objects[i];
            if (body.find("endstream") != std::string::npos) continue;
            if (body.size() > 128U) continue;
            objectStreamMembers.push_back(i);
        }
    }
    const bool hasObjectStream = objectStreamMembers.size() >= 3U;

    std::vector<std::size_t> objectStreamIndices(objects.size(), std::string::npos);
    std::string objectStreamPayload;
    std::string objectStreamHeader;
    std::size_t objectStreamObject = 0U;
    if (hasObjectStream) {
        objectStreamObject = objects.size();
        std::ostringstream header;
        for (std::size_t index = 0U; index < objectStreamMembers.size(); ++index) {
            const std::size_t member = objectStreamMembers[index];
            objectStreamIndices[member] = index;
            header << member << ' ' << objectStreamPayload.size() << '\n';
            objectStreamPayload += objects[member];
        }
        objectStreamHeader = header.str();
    }

    std::vector<std::uint64_t> offsets(objects.size());
    for (std::size_t i = 1; i < objects.size(); ++i) {
        if (hasObjectStream && objectStreamIndices[i] != std::string::npos) continue;
        offsets[i] = static_cast<std::uint64_t>(out.tellp());
        out << i << " 0 obj\n";
        const std::string body = security && i != encryptionObject
            ? security->EncryptObject(objects[i], static_cast<std::uint32_t>(i), 0U)
            : objects[i];
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out << "\nendobj\n";
    }
    const std::uint64_t objectStreamOffset = hasObjectStream
        ? static_cast<std::uint64_t>(out.tellp()) : 0U;
    if (hasObjectStream) {
        const std::string fullPayload = objectStreamHeader + objectStreamPayload;
        const auto compressedPayload = compressBytes(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(fullPayload.data()),
            fullPayload.size()));
        std::ostringstream streamBody;
        streamBody << "<< /Type /ObjStm /N " << objectStreamMembers.size()
                   << " /First " << objectStreamHeader.size()
                   << " /Filter /FlateDecode /Length " << compressedPayload.size()
                   << " >>\nstream\n";
        streamBody.write(compressedPayload.data(),
                         static_cast<std::streamsize>(compressedPayload.size()));
        streamBody << "\nendstream";
        std::string body = streamBody.str();
        if (security) {
            body = security->EncryptObject(body, static_cast<std::uint32_t>(objectStreamObject), 0U);
        }
        out << objectStreamObject << " 0 obj\n";
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out << "\nendobj\n";
    }
    const auto startxref = static_cast<std::uint64_t>(out.tellp());
    const std::size_t xrefObject = hasObjectStream ? objectStreamObject + 1U : objects.size();

    if (!options.writeXrefStream) {
        out << "xref\n0 " << objects.size() << "\n0000000000 65535 f \n";
        for (std::size_t i = 1; i < objects.size(); ++i) {
            out << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
        }
        out << "trailer\n<< /Size " << objects.size() << " /Root " << catalogObject << " 0 R";
        if (hasDocumentInfo) out << " /Info " << infoObject << " 0 R";
        if (security) out << " /Encrypt " << encryptionObject << " 0 R /ID [<"
                          << PdfHex(fileId) << "><" << PdfHex(fileId) << ">]";
        out << " >>\nstartxref\n" << startxref << "\n%%EOF\n";
        return;
    }

    // Write an /XRef stream as the cross-reference table. Entries use
    // /W [1 4 2]: type (1), offset (4), generation (2). Compressed objects
    // get a type 2 entry whose fields are the object stream number and the
    // offset of the object within that stream. The xref stream object itself
    // is the last object and is never encrypted.
    const std::size_t totalObjects = xrefObject + 1U;
    std::string entries;
    entries.reserve(totalObjects * 7U);
    const auto appendBigEndian = [&entries](std::uint64_t value, std::size_t width) {
        for (std::size_t shift = 0U; shift < width; ++shift) {
            entries.push_back(static_cast<char>(
                (value >> ((width - 1U - shift) * 8U)) & 0xFFU));
        }
    };
    appendBigEndian(0U, 1U);   // type 0: free entry head
    appendBigEndian(0U, 4U);   // next free object = 0
    appendBigEndian(65535U, 2U);
    for (std::size_t i = 1; i < objects.size(); ++i) {
        if (hasObjectStream && objectStreamIndices[i] != std::string::npos) {
            appendBigEndian(2U, 1U);
            appendBigEndian(objectStreamObject, 4U);
            appendBigEndian(objectStreamIndices[i], 2U);
        } else {
            appendBigEndian(1U, 1U);
            appendBigEndian(offsets[i], 4U);
            appendBigEndian(0U, 2U);
        }
    }
    if (hasObjectStream) {
        appendBigEndian(1U, 1U);   // the object stream object itself
        appendBigEndian(objectStreamOffset, 4U);
        appendBigEndian(0U, 2U);
    }
    appendBigEndian(1U, 1U);   // the xref stream object itself
    appendBigEndian(startxref, 4U);
    appendBigEndian(0U, 2U);

    const auto compressedEntries = compressBytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(entries.data()), entries.size()));
    const auto xrefStreamOffset = static_cast<std::uint64_t>(out.tellp());
    out << xrefObject << " 0 obj\n<< /Type /XRef /Size " << totalObjects
        << " /Root " << catalogObject << " 0 R";
    if (hasDocumentInfo) out << " /Info " << infoObject << " 0 R";
    if (security) out << " /Encrypt " << encryptionObject << " 0 R /ID [<"
                      << PdfHex(fileId) << "><" << PdfHex(fileId) << ">]";
    out << " /W [1 4 2] /Index [0 " << totalObjects << "] /Filter /FlateDecode /Length "
        << compressedEntries.size() << " >>\nstream\n";
    out.write(compressedEntries.data(), static_cast<std::streamsize>(compressedEntries.size()));
    out << "\nendstream\nendobj\nstartxref\n" << xrefStreamOffset << "\n%%EOF\n";
}

} // namespace CPPPdf::Internal

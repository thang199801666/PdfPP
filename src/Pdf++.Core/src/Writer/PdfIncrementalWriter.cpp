#include "Internal/Writer/PdfIncrementalWriter.hpp"

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Security/PdfStandardSecurity.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"

#include <zlib.h>
#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace CPPPdf::Internal {
namespace {

[[nodiscard]] std::string compressBytes(const std::span<const std::byte> input) {
    uLongf outputSize = compressBound(static_cast<uLong>(input.size()));
    std::string output(outputSize, '\0');
    const int status = compress2(
        reinterpret_cast<Bytef*>(output.data()), &outputSize,
        reinterpret_cast<const Bytef*>(input.data()), static_cast<uLong>(input.size()),
        Z_BEST_SPEED);
    if (status != Z_OK) {
        throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot compress PDF stream data.");
    }
    output.resize(outputSize);
    return output;
}

} // namespace

PdfIncrementalWriter::PdfIncrementalWriter(const std::filesystem::path& inputPath,
                                           const std::filesystem::path& outputPath,
                                           const PdfDocument& document)
    : PdfIncrementalWriter(inputPath, outputPath, document, PdfIncrementalWriterOptions{}) {
}

PdfIncrementalWriter::PdfIncrementalWriter(const std::filesystem::path& inputPath,
                                           const std::filesystem::path& outputPath,
                                           const PdfDocument& document,
                                           const PdfIncrementalWriterOptions& options)
    : document_(document), output_(outputPath, std::ios::binary | std::ios::trunc),
      writeXrefStream_(options.writeXrefStream),
      writeObjectStreams_(options.writeObjectStreams) {
    // A classic xref table cannot carry type-2 entries, so object-stream
    // output requires an xref stream in the same revision.
    if (writeObjectStreams_) writeXrefStream_ = true;
    if (std::filesystem::absolute(inputPath).lexically_normal() ==
        std::filesystem::absolute(outputPath).lexically_normal()) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Incremental update requires different input and output paths.");
    }
    if (!output_) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "Cannot create incremental PDF: " + outputPath.string());
    }
    if (writeObjectStreams_) {
        try {
            catalogObject_ = document.findRootReference().objectNumber;
        } catch (...) {
            catalogObject_ = 0U;
        }
    }
    std::ifstream input(inputPath, std::ios::binary);
    if (!input) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "Cannot open input PDF: " + inputPath.string());
    }
    std::array<char, 64U * 1024U> buffer{};
    char last{};
    bool copied = false;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) break;
        output_.write(buffer.data(), count);
        last = buffer[static_cast<std::size_t>(count - 1)];
        copied = true;
    }
    if (!output_ || input.bad()) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "Failed while copying the source PDF.");
    }
    if (copied && last != '\n' && last != '\r') output_.put('\n');
}

void PdfIncrementalWriter::WriteRawObject(const PdfReference& reference,
                                          const std::string_view body) {
    if (finished_) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Cannot append an object after incremental writer finalization.");
    }
    if (entries_.contains(reference.objectNumber) ||
        objectStreamBodies_.contains(reference.objectNumber)) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "An indirect object was written twice in one incremental revision.");
    }
    if (writeObjectStreams_ && canLiveInObjectStream(reference, body)) {
        objectStreamBodies_[reference.objectNumber] = std::string(body);
        return;
    }
    std::string serialized(body);
    if (document_.IsEncrypted()) {
        serialized = document_.EncryptObjectForIncrementalWrite(serialized, reference);
    }
    entries_[reference.objectNumber] = {
        static_cast<std::uint64_t>(output_.tellp()), reference.generation};
    output_ << reference.objectNumber << ' ' << reference.generation << " obj\n";
    output_.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    output_ << "\nendobj\n";
}

bool PdfIncrementalWriter::canLiveInObjectStream(const PdfReference& reference,
                                                 std::string_view body) const {
    if (reference.generation != 0U) return false;
    if (body.size() > 128U) return false;
    if (body.find("endstream") != std::string_view::npos) return false;
    if (reference.objectNumber == catalogObject_) return false;
    if (document_.encryptionReference_ &&
        reference.objectNumber == document_.encryptionReference_->objectNumber) {
        return false;
    }
    return true;
}

void PdfIncrementalWriter::WriteObject(const PdfReference& reference, const PdfObject& object) {
    std::ostringstream body;
    PdfObjectSerializer::WriteObject(body, object);
    WriteRawObject(reference, body.str());
}

void PdfIncrementalWriter::WriteDictionary(const PdfReference& reference,
                                           const PdfDictionary& dictionary) {
    std::ostringstream body;
    PdfObjectSerializer::WriteDictionary(body, dictionary);
    WriteRawObject(reference, body.str());
}

void PdfIncrementalWriter::writeObjectStream(std::uint32_t& size) {
    if (objectStreamBodies_.empty()) return;
    std::uint32_t maximum = size > 0U ? size - 1U : 0U;
    for (const auto& [number, body] : objectStreamBodies_) {
        (void)body;
        maximum = std::max(maximum, number);
    }
    objectStreamNumber_ = maximum + 1U;
    std::ostringstream header;
    std::string payload;
    for (const auto& [number, body] : objectStreamBodies_) {
        objectStreamOffsets_[number] = static_cast<std::uint32_t>(payload.size());
        header << number << ' ' << payload.size() << '\n';
        payload += body;
    }
    const std::string headerText = header.str();
    const std::string fullPayload = headerText + payload;
    const std::string compressed = compressBytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(fullPayload.data()), fullPayload.size()));
    std::ostringstream streamBody;
    streamBody << "<< /Type /ObjStm /N " << objectStreamBodies_.size()
               << " /First " << headerText.size()
               << " /Filter /FlateDecode /Length " << compressed.size()
               << " >>\nstream\n";
    streamBody.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
    streamBody << "\nendstream";
    std::string body = streamBody.str();
    if (document_.IsEncrypted()) {
        body = document_.EncryptObjectForIncrementalWrite(
            body, PdfReference{objectStreamNumber_, 0U});
    }
    entries_[objectStreamNumber_] = {
        static_cast<std::uint64_t>(output_.tellp()), 0U};
    output_ << objectStreamNumber_ << " 0 obj\n";
    output_.write(body.data(), static_cast<std::streamsize>(body.size()));
    output_ << "\nendobj\n";
    size = std::max(size, objectStreamNumber_ + 1U);
}

void PdfIncrementalWriter::Finish(const std::uint32_t trailerSize) {
    if (finished_) return;
    std::uint32_t size = trailerSize;
    if (writeObjectStreams_) writeObjectStream(size);
    const auto xrefOffset = static_cast<std::uint64_t>(output_.tellp());
    if (writeXrefStream_) {
        writeXrefStream(size, document_.GetStartXrefOffset());
    } else {
        writeClassicXref(size, document_.GetStartXrefOffset());
    }
    output_ << "\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    if (!output_) {
        throw PdfException(PdfErrorCode::FileOpenFailed,
                           "Failed while writing the incremental PDF revision.");
    }
    finished_ = true;
}

void PdfIncrementalWriter::writeClassicXref(const std::uint32_t trailerSize,
                                            const std::uint64_t previousOffset) {
    const auto xrefOffset = static_cast<std::uint64_t>(output_.tellp());
    output_ << "xref\n";
    auto iterator = entries_.begin();
    while (iterator != entries_.end()) {
        const auto first = iterator->first;
        auto end = iterator;
        std::uint32_t expected = first;
        while (end != entries_.end() && end->first == expected) {
            ++end;
            ++expected;
        }
        output_ << first << ' ' << (expected - first) << '\n';
        for (auto current = iterator; current != end; ++current) {
            output_ << std::setw(10) << std::setfill('0') << current->second.first << ' '
                    << std::setw(5) << std::setfill('0') << current->second.second << " n \n";
        }
        iterator = end;
    }

    const PdfObject parsed = PdfObjectParser::Parse(
        document_.GetTrailerDictionary(), document_.readerOptions().limits.maxRecursionDepth);
    const PdfDictionary* sourceTrailer = parsed.AsDictionary();
    if (!sourceTrailer) {
        throw PdfException(PdfErrorCode::TrailerNotFound,
                           "Source trailer is not a PDF dictionary.");
    }
    PdfDictionary trailer = *sourceTrailer;
    trailer.Put(PdfName("Size"), PdfObject(static_cast<std::int64_t>(trailerSize)));
    trailer.Put(PdfName("Prev"),
                PdfObject(static_cast<std::int64_t>(previousOffset)));
    trailer.Remove(PdfName("XRefStm"));
    output_ << "trailer\n";
    if (document_.encryption_) {
        trailer.Remove(PdfName("ID"));
        std::ostringstream trailerBody;
        PdfObjectSerializer::WriteDictionary(trailerBody, trailer);
        std::string serializedTrailer = trailerBody.str();
        const auto end = serializedTrailer.rfind(">>");
        if (end == std::string::npos) {
            throw PdfException(PdfErrorCode::TrailerNotFound,
                               "Cannot serialize encrypted incremental trailer.");
        }
        const std::string id = PdfHex(document_.encryption_->FileId());
        serializedTrailer.insert(end, "/ID [<" + id + "><" + id + ">]\n");
        output_ << serializedTrailer;
    } else {
        PdfObjectSerializer::WriteDictionary(output_, trailer);
    }
    (void)xrefOffset;
}

void PdfIncrementalWriter::writeXrefStream(const std::uint32_t trailerSize,
                                           const std::uint64_t previousOffset) {
    const auto xrefStreamOffset = static_cast<std::uint64_t>(output_.tellp());

    // The revision's xref stream must cover every object written in this
    // revision: objects stored immediately, compressed members of the object
    // stream, the object stream itself, and finally the xref stream object.
    std::vector<std::uint32_t> numbers;
    numbers.reserve(entries_.size() + objectStreamBodies_.size() + 1U);
    for (const auto& [number, entry] : entries_) {
        (void)entry;
        numbers.push_back(number);
    }
    for (const auto& [number, body] : objectStreamBodies_) {
        (void)body;
        numbers.push_back(number);
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());

    std::uint32_t maximum = trailerSize > 0U ? trailerSize - 1U : 0U;
    for (const auto number : numbers) maximum = std::max(maximum, number);
    const std::uint32_t xrefObject = maximum + 1U;
    const std::uint32_t totalObjects = xrefObject + 1U;

    std::string entries;
    entries.reserve(totalObjects * 7U);
    const auto appendBigEndian = [&entries](std::uint64_t value, std::size_t width) {
        for (std::size_t shift = 0U; shift < width; ++shift) {
            entries.push_back(static_cast<char>(
                (value >> ((width - 1U - shift) * 8U)) & 0xFFU));
        }
    };

    PdfArray indexArray;
    std::size_t runBegin = 0U;
    while (runBegin < numbers.size()) {
        const std::uint32_t first = numbers[runBegin];
        std::size_t runEnd = runBegin + 1U;
        while (runEnd < numbers.size() && numbers[runEnd] == numbers[runEnd - 1U] + 1U) {
            ++runEnd;
        }
        for (std::size_t i = runBegin; i < runEnd; ++i) {
            const std::uint32_t number = numbers[i];
            if (const auto compressed = objectStreamOffsets_.find(number);
                compressed != objectStreamOffsets_.end()) {
                appendBigEndian(2U, 1U);
                appendBigEndian(objectStreamNumber_, 4U);
                appendBigEndian(compressed->second, 2U);
            } else {
                const auto entry = entries_.find(number);
                appendBigEndian(1U, 1U);
                appendBigEndian(entry->second.first, 4U);
                appendBigEndian(entry->second.second, 2U);
            }
        }
        indexArray.push_back(PdfObject(static_cast<std::int64_t>(first)));
        indexArray.push_back(PdfObject(static_cast<std::int64_t>(runEnd - runBegin)));
        runBegin = runEnd;
    }
    appendBigEndian(1U, 1U);
    appendBigEndian(xrefStreamOffset, 4U);
    appendBigEndian(0U, 2U);
    indexArray.push_back(PdfObject(static_cast<std::int64_t>(xrefObject)));
    indexArray.push_back(PdfObject(std::int64_t{1}));

    const std::string compressedEntries = compressBytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(entries.data()), entries.size()));

    const PdfObject parsed = PdfObjectParser::Parse(
        document_.GetTrailerDictionary(), document_.readerOptions().limits.maxRecursionDepth);
    const PdfDictionary* sourceTrailer = parsed.AsDictionary();
    if (!sourceTrailer) {
        throw PdfException(PdfErrorCode::TrailerNotFound,
                           "Source trailer is not a PDF dictionary.");
    }
    PdfDictionary dictionary = *sourceTrailer;
    dictionary.Remove(PdfName("Size"));
    dictionary.Remove(PdfName("Prev"));
    dictionary.Remove(PdfName("XRefStm"));
    dictionary.Put(PdfName("Type"), PdfObject(PdfName("XRef")));
    dictionary.Put(PdfName("Size"), PdfObject(static_cast<std::int64_t>(totalObjects)));
    dictionary.Put(PdfName("Prev"), PdfObject(static_cast<std::int64_t>(previousOffset)));
    PdfArray widths;
    widths.push_back(PdfObject(std::int64_t{1}));
    widths.push_back(PdfObject(std::int64_t{4}));
    widths.push_back(PdfObject(std::int64_t{2}));
    dictionary.Put(PdfName("W"), PdfObject(std::move(widths)));
    dictionary.Put(PdfName("Index"), PdfObject(std::move(indexArray)));
    dictionary.Remove(PdfName("Filter"));
    dictionary.Put(PdfName("Filter"), PdfObject(PdfName("FlateDecode")));
    dictionary.Put(PdfName("Length"), PdfObject(static_cast<std::int64_t>(compressedEntries.size())));

    std::ostringstream output;
    PdfObjectSerializer::WriteDictionary(output, dictionary);
    std::string serialized = output.str();
    if (document_.encryption_) {
        const std::string id = PdfHex(document_.encryption_->FileId());
        const auto dictEnd = serialized.rfind(">>");
        if (dictEnd == std::string::npos) {
            throw PdfException(PdfErrorCode::FileOpenFailed,
                               "Cannot serialize encrypted incremental xref stream.");
        }
        serialized.insert(dictEnd, "/ID [<" + id + "><" + id + ">]");
    }

    output_ << xrefObject << " 0 obj\n";
    output_.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    output_ << "\nstream\n";
    output_.write(compressedEntries.data(), static_cast<std::streamsize>(compressedEntries.size()));
    output_ << "\nendstream\nendobj\n";
}

std::uint32_t PdfIncrementalWriter::NextObjectNumber(const PdfDocument& document) {
    std::uint32_t maximum = 0U;
    for (const auto number : document.objectNumbers()) maximum = std::max(maximum, number);
    if (maximum == std::numeric_limits<std::uint32_t>::max()) {
        throw PdfException(PdfErrorCode::UnsupportedFeature,
                           "No free PDF object number remains.");
    }
    return maximum + 1U;
}

} // namespace CPPPdf::Internal

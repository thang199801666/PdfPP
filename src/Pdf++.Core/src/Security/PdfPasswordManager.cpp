#include <CPPPdf/Security/PdfSecurity.hpp>

#include <CPPPdf/Document/PdfDocument.hpp>
#include <CPPPdf/PdfError.hpp>
#include "Internal/Parsing/PdfObjectParser.hpp"
#include "Internal/Security/PdfStandardSecurity.hpp"
#include "Internal/Writer/PdfObjectSerializer.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace CPPPdf {
namespace {

void rewrite(const std::filesystem::path& input,
             const std::filesystem::path& output,
             const std::string& currentPassword,
             const std::optional<PdfEncryptionOptions>& replacement) {
    if (std::filesystem::absolute(input).lexically_normal() ==
        std::filesystem::absolute(output).lexically_normal()) {
        throw PdfException(PdfErrorCode::InvalidArgument,
                           "Password rewrite requires different input and output paths.");
    }

    PdfReaderOptions readerOptions;
    readerOptions.password = currentPassword;
    PdfDocument document = PdfDocument::OpenMapped(input, readerOptions);
    if (document.IsPasswordRequired()) {
        throw PdfException(PdfErrorCode::PasswordRequired,
                           "A valid current password is required to rewrite this PDF.");
    }

    PdfDictionary trailer;
    const PdfObject parsedTrailer = Internal::PdfObjectParser::Parse(
        document.GetTrailerDictionary(), readerOptions.limits.maxRecursionDepth);
    if (const auto* parsed = parsedTrailer.AsDictionary()) {
        trailer = *parsed;
    } else {
        throw PdfException(PdfErrorCode::TrailerNotFound, "Cannot parse the source PDF trailer.");
    }
    const auto oldEncryption = document.GetTrailerReference(PdfName("Encrypt"));
    trailer.Remove(PdfName("Encrypt"));
    trailer.Remove(PdfName("Prev"));
    trailer.Remove(PdfName("XRefStm"));
    trailer.Remove(PdfName("ID"));

    std::vector<std::uint32_t> numbers = document.objectNumbers();
    if (oldEncryption) {
        numbers.erase(std::remove(numbers.begin(), numbers.end(), oldEncryption->objectNumber),
                      numbers.end());
    }
    std::uint32_t maximum = numbers.empty() ? 0U : numbers.back();
    const std::uint32_t encryptionNumber = replacement ? maximum + 1U : 0U;
    if (replacement) maximum = encryptionNumber;

    const auto fileId = Internal::GeneratePdfFileId();
    const auto security = replacement
        ? std::optional<Internal::PdfStandardSecurity>(
              Internal::PdfStandardSecurity::Create(*replacement, fileId))
        : std::nullopt;

    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) throw PdfException(PdfErrorCode::FileOpenFailed, "Cannot create PDF output file.");
    stream << "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";

    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint16_t>> offsets;
    for (const auto number : numbers) {
        const auto entry = document.GetXrefEntry(number);
        const std::uint16_t generation = entry ? entry->generation : std::uint16_t{0};
        std::ostringstream body;
        Internal::PdfObjectSerializer::WriteObject(
            body, document.GetObject(PdfReference{number, generation}));
        std::string serialized = body.str();
        if (security) serialized = security->EncryptObject(serialized, number, generation);
        offsets[number] = {static_cast<std::uint64_t>(stream.tellp()), generation};
        stream << number << ' ' << generation << " obj\n";
        stream.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        stream << "\nendobj\n";
    }
    if (security) {
        offsets[encryptionNumber] = {
            static_cast<std::uint64_t>(stream.tellp()), std::uint16_t{0}};
        stream << encryptionNumber << " 0 obj\n" << security->EncryptionDictionary()
               << "\nendobj\n";
    }

    const auto xrefOffset = static_cast<std::uint64_t>(stream.tellp());
    stream << "xref\n0 " << (maximum + 1U) << "\n0000000000 65535 f \n";
    for (std::uint32_t number = 1; number <= maximum; ++number) {
        const auto found = offsets.find(number);
        if (found == offsets.end()) {
            stream << "0000000000 00000 f \n";
        } else {
            stream << std::setw(10) << std::setfill('0') << found->second.first << ' '
                   << std::setw(5) << found->second.second << " n \n";
        }
    }

    trailer.Put(PdfName("Size"), PdfObject(static_cast<std::int64_t>(maximum + 1U)));
    if (security) {
        trailer.Put(PdfName("Encrypt"), PdfObject::IndirectReference(encryptionNumber, 0U));
    }
    std::ostringstream trailerBody;
    Internal::PdfObjectSerializer::WriteDictionary(trailerBody, trailer);
    std::string serializedTrailer = trailerBody.str();
    const auto dictionaryEnd = serializedTrailer.rfind(">>");
    if (dictionaryEnd == std::string::npos) {
        throw PdfException(PdfErrorCode::TrailerNotFound, "Cannot serialize the output trailer.");
    }
    serializedTrailer.insert(dictionaryEnd, "/ID [<" + Internal::PdfHex(fileId) + "><" +
        Internal::PdfHex(fileId) + ">]\n");
    stream << "trailer\n" << serializedTrailer;
    stream << "\nstartxref\n" << xrefOffset << "\n%%EOF\n";
    if (!stream) throw PdfException(PdfErrorCode::FileOpenFailed, "Failed while writing secured PDF.");
}

} // namespace

void PdfPasswordManager::Encrypt(const std::filesystem::path& input,
                                 const std::filesystem::path& output,
                                 const PdfEncryptionOptions& options,
                                 std::string currentPassword) {
    rewrite(input, output, currentPassword, options);
}

void PdfPasswordManager::RemovePassword(const std::filesystem::path& input,
                                        const std::filesystem::path& output,
                                        std::string currentPassword) {
    rewrite(input, output, currentPassword, std::nullopt);
}

void PdfPasswordManager::ChangePassword(const std::filesystem::path& input,
                                        const std::filesystem::path& output,
                                        std::string currentPassword,
                                        const PdfEncryptionOptions& options) {
    rewrite(input, output, currentPassword, options);
}

} // namespace CPPPdf

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace CPPPdf {

enum class PdfEncryptionAlgorithm {
    Aes128,
    Rc4_128
};

struct PdfPermissions final {
    bool print{true};
    bool modify{true};
    bool copy{true};
    bool annotate{true};
    bool fillForms{true};
    bool accessibility{true};
    bool assemble{true};
    bool highQualityPrint{true};
};

struct PdfEncryptionOptions final {
    std::string userPassword;
    std::string ownerPassword;
    PdfEncryptionAlgorithm algorithm{PdfEncryptionAlgorithm::Aes128};
    PdfPermissions permissions{};
    bool encryptMetadata{true};
};

// Rewrites an existing PDF while changing its Standard Security Handler.
// Input and output must be different files.
class PdfPasswordManager final {
public:
    static void Encrypt(const std::filesystem::path& input,
                        const std::filesystem::path& output,
                        const PdfEncryptionOptions& options,
                        std::string currentPassword = {});
    static void RemovePassword(const std::filesystem::path& input,
                               const std::filesystem::path& output,
                               std::string currentPassword);
    static void ChangePassword(const std::filesystem::path& input,
                               const std::filesystem::path& output,
                               std::string currentPassword,
                               const PdfEncryptionOptions& options);
};

} // namespace CPPPdf

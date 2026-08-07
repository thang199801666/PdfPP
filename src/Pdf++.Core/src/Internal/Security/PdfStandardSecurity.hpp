#pragma once

#include <CPPPdf/Core/PdfTypes.hpp>
#include <CPPPdf/Security/PdfSecurity.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CPPPdf::Internal {

class PdfStandardSecurity final {
public:
    static PdfStandardSecurity Create(const PdfEncryptionOptions& options,
                                      std::span<const std::uint8_t, 16> fileId);
    static PdfStandardSecurity Parse(std::string_view dictionary,
                                     std::span<const std::uint8_t> fileId);

    [[nodiscard]] bool Authenticate(std::string_view password);
    [[nodiscard]] bool IsAuthenticated() const noexcept { return authenticated_; }
    [[nodiscard]] bool IsOwnerAuthenticated() const noexcept { return ownerAuthenticated_; }
    [[nodiscard]] PdfEncryptionAlgorithm Algorithm() const noexcept { return algorithm_; }
    [[nodiscard]] std::int32_t PermissionBits() const noexcept { return permissions_; }
    [[nodiscard]] bool EncryptMetadata() const noexcept { return encryptMetadata_; }
    [[nodiscard]] const std::array<std::uint8_t, 16>& FileId() const noexcept { return fileId_; }

    [[nodiscard]] std::string EncryptionDictionary() const;
    [[nodiscard]] std::string EncryptObject(std::string_view object,
                                            std::uint32_t objectNumber,
                                            std::uint16_t generation) const;
    [[nodiscard]] std::string DecryptObject(std::string_view object,
                                            std::uint32_t objectNumber,
                                            std::uint16_t generation) const;

private:
    [[nodiscard]] std::string TransformObject(std::string_view object,
                                              std::uint32_t objectNumber,
                                              std::uint16_t generation,
                                              bool encrypt) const;
    [[nodiscard]] std::vector<std::uint8_t> Crypt(std::span<const std::uint8_t> input,
                                                  std::uint32_t objectNumber,
                                                  std::uint16_t generation,
                                                  bool encrypt) const;
    [[nodiscard]] bool AuthenticateAes256(std::string_view password, bool owner);
    [[nodiscard]] bool ValidateAes256Permissions() const;

    PdfEncryptionAlgorithm algorithm_{PdfEncryptionAlgorithm::Aes128};
    std::int32_t permissions_{-4};
    bool encryptMetadata_{true};
    bool authenticated_{false};
    bool ownerAuthenticated_{false};
    std::array<std::uint8_t, 16> fileId_{};

    // Revisions 2-4 use the first 32 bytes. Revision 6 uses all 48 bytes:
    // 32-byte validation hash + 8-byte validation salt + 8-byte key salt.
    std::array<std::uint8_t, 48> ownerEntry_{};
    std::array<std::uint8_t, 48> userEntry_{};
    std::array<std::uint8_t, 32> ownerEncryptedKey_{}; // /OE
    std::array<std::uint8_t, 32> userEncryptedKey_{};  // /UE
    std::array<std::uint8_t, 16> encryptedPermissions_{}; // /Perms
    std::array<std::uint8_t, 32> fileKey_{};
};

[[nodiscard]] std::array<std::uint8_t, 16> GeneratePdfFileId();
[[nodiscard]] std::string PdfHex(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> ParsePdfHex(std::string_view hex);
[[nodiscard]] std::int32_t BuildPdfPermissionBits(const PdfPermissions& permissions);

} // namespace CPPPdf::Internal

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace CPPPdf::Internal {

[[nodiscard]] std::array<std::uint8_t, 32> Sha256(std::span<const std::uint8_t> input);
[[nodiscard]] std::array<std::uint8_t, 16> Aes256EncryptBlock(
    std::span<const std::uint8_t, 32> key,
    std::span<const std::uint8_t, 16> block);
[[nodiscard]] std::array<std::uint8_t, 16> Aes256DecryptBlock(
    std::span<const std::uint8_t, 32> key,
    std::span<const std::uint8_t, 16> block);

} // namespace CPPPdf::Internal

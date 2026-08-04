#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace CPPPdf::Internal {

// Baseline JPEG (DCT) encoder over RGB data. Produces a complete JPEG file
// with 4:4:4 chroma subsampling, standard quantization tables scaled by
// quality, and Huffman coding. Dependency-free.
std::vector<std::byte> EncodeJpeg(std::uint32_t width, std::uint32_t height,
                                  std::span<const std::byte> rgbBytes, int quality);

} // namespace CPPPdf::Internal

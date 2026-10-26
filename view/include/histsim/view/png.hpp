// Minimal PNG writer (RGB, 8 bits per channel), no dependencies.
//
// Compression is a simple LZ77 against the previous pixel and the pixel above,
// coded with deflate's fixed Huffman table: maps have large flat areas, so
// files come out around 5-10% of raw size.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace histsim::view {

// Encode an image given as RGBA bytes (alpha is dropped).
std::vector<std::uint8_t> encode_png(int width, int height, const std::vector<std::uint8_t>& rgba);

void write_png(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgba);

}  // namespace histsim::view

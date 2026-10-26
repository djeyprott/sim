#include "histsim/view/png.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace histsim::view {

namespace {

// ---- Checksums --------------------------------------------------------------

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t v : data) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

// ---- Deflate with the fixed Huffman code ------------------------------------

class BitWriter {
public:
    void bits(std::uint32_t value, int count) {  // LSB first
        for (int i = 0; i < count; ++i) {
            acc_ |= ((value >> i) & 1u) << n_;
            if (++n_ == 8) flush_byte();
        }
    }
    void huffman(std::uint32_t code, int length) {  // codes are written MSB first
        for (int i = length - 1; i >= 0; --i) bits((code >> i) & 1u, 1);
    }
    std::vector<std::uint8_t> finish() {
        if (n_ > 0) flush_byte();
        return std::move(out_);
    }

private:
    void flush_byte() {
        out_.push_back(static_cast<std::uint8_t>(acc_));
        acc_ = 0;
        n_ = 0;
    }
    std::vector<std::uint8_t> out_;
    std::uint32_t acc_ = 0;
    int n_ = 0;
};

void literal_or_length(BitWriter& w, int sym) {
    if (sym < 144) w.huffman(0x30 + sym, 8);
    else if (sym < 256) w.huffman(0x190 + (sym - 144), 9);
    else if (sym < 280) w.huffman(sym - 256, 7);
    else w.huffman(0xC0 + (sym - 280), 8);
}

constexpr int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                              31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void emit_match(BitWriter& w, int length, int distance) {
    int li = 28;
    while (kLenBase[li] > length) --li;
    literal_or_length(w, 257 + li);
    w.bits(static_cast<std::uint32_t>(length - kLenBase[li]), kLenExtra[li]);
    int di = 29;
    while (kDistBase[di] > distance) --di;
    w.huffman(static_cast<std::uint32_t>(di), 5);
    w.bits(static_cast<std::uint32_t>(distance - kDistBase[di]), kDistExtra[di]);
}

// One final fixed-Huffman block. Tries two match distances at each position:
// one pixel back and one row back.
std::vector<std::uint8_t> deflate_fixed(const std::vector<std::uint8_t>& in, int pixel, int row) {
    BitWriter w;
    w.bits(1, 1);  // BFINAL
    w.bits(1, 2);  // BTYPE = fixed Huffman
    const std::size_t n = in.size();
    std::size_t i = 0;
    while (i < n) {
        int best_len = 0, best_dist = 0;
        for (int d : {pixel, row}) {
            if (d <= 0 || d > 32768 || static_cast<std::size_t>(d) > i) continue;
            int len = 0;
            while (len < 258 && i + len < n && in[i + len] == in[i + len - d]) ++len;
            if (len > best_len) {
                best_len = len;
                best_dist = d;
            }
        }
        if (best_len >= 3) {
            emit_match(w, best_len, best_dist);
            i += static_cast<std::size_t>(best_len);
        } else {
            literal_or_length(w, in[i]);
            ++i;
        }
    }
    literal_or_length(w, 256);  // end of block
    return w.finish();
}

void put_u32be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data) {
    put_u32be(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put_u32be(out, crc32(out.data() + start, out.size() - start));
}

}  // namespace

std::vector<std::uint8_t> encode_png(int width, int height, const std::vector<std::uint8_t>& rgba) {
    if (width <= 0 || height <= 0 || rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        throw std::invalid_argument("image size does not match pixel data");
    }
    // Scanlines: filter byte 0 (none), then RGB.
    const int stride = 1 + width * 3;
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(stride) * height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::uint8_t* row = &rgba[static_cast<std::size_t>(y) * width * 4];
        for (int x = 0; x < width; ++x) {
            raw.push_back(row[x * 4]);
            raw.push_back(row[x * 4 + 1]);
            raw.push_back(row[x * 4 + 2]);
        }
    }

    std::vector<std::uint8_t> z = {0x78, 0x01};  // zlib header, no preset dictionary
    const auto body = deflate_fixed(raw, 3, stride);
    z.insert(z.end(), body.begin(), body.end());
    put_u32be(z, adler32(raw));

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    put_u32be(ihdr, static_cast<std::uint32_t>(width));
    put_u32be(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit RGB, deflate, no filter, no interlace
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    return png;
}

void write_png(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgba) {
    const auto png = encode_png(width, height, rgba);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot open " + path + " for writing");
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!out) {
        throw std::runtime_error("failed writing " + path);
    }
}

}  // namespace histsim::view

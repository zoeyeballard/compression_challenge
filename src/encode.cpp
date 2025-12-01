#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>

#include "miniz.h"

// Container format v2:
// magic: 4 bytes "NLC2"
// original_size: 8 bytes LE  (entire original file size in bytes)
// header_size:   4 bytes LE  (number of bytes of WAV header we store verbatim)
// compressed_size: 8 bytes LE
// header_bytes: header_size bytes (copied verbatim from input)
// compressed_payload: compressed_size bytes
//
// If the input is not recognized as 16-bit mono PCM WAV, we fall back to
// treating the entire file as an opaque byte blob: header_size = 0 and
// we DEFLATE the whole thing. This keeps us lossless on any input.

static void die(const char *msg) {
    std::fprintf(stderr, "encode: %s\n", msg);
    std::exit(1);
}

static bool read_all_stdin(std::vector<unsigned char> &buf) {
    constexpr size_t CHUNK = 1 << 16;
    unsigned char tmp[CHUNK];
    while (true) {
        size_t n = std::fread(tmp, 1, CHUNK, stdin);
        if (n) buf.insert(buf.end(), tmp, tmp + n);
        if (n < CHUNK) {
            if (std::ferror(stdin)) return false;
            break;
        }
    }
    return true;
}

static bool read_file(const std::string &path, std::vector<unsigned char> &buf) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    constexpr size_t CHUNK = 1 << 16;
    unsigned char tmp[CHUNK];
    while (true) {
        size_t n = std::fread(tmp, 1, CHUNK, f);
        if (n) buf.insert(buf.end(), tmp, tmp + n);
        if (n < CHUNK) {
            if (std::ferror(f)) {
                std::fclose(f);
                return false;
            }
            break;
        }
    }
    std::fclose(f);
    return true;
}

static bool write_file(const std::string &path, const std::vector<unsigned char> &buf) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    if (!buf.empty()) {
        size_t n = std::fwrite(buf.data(), 1, buf.size(), f);
        if (n != buf.size()) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

static void write_le64(uint64_t v, std::vector<unsigned char> &out) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<unsigned char>(v & 0xFFu));
        v >>= 8;
    }
}

static void write_le32(uint32_t v, std::vector<unsigned char> &out) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<unsigned char>(v & 0xFFu));
        v >>= 8;
    }
}

static uint32_t read_le32(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

struct WavLayout {
    bool ok = false;
    size_t header_size = 0;   // bytes before first sample
    size_t data_offset = 0;   // same as header_size
    size_t data_size = 0;     // bytes of sample data
};

// Parse minimal RIFF/WAVE header and locate the "data" chunk.
// We require PCM, mono, 16-bit to enable the predictive transform.
static WavLayout parse_wav(const std::vector<unsigned char> &buf) {
    WavLayout wl;
    if (buf.size() < 44) return wl;

    const unsigned char *p = buf.data();
    if (p[0] != 'R' || p[1] != 'I' || p[2] != 'F' || p[3] != 'F') return wl;
    if (p[8] != 'W' || p[9] != 'A' || p[10] != 'V' || p[11] != 'E') return wl;

    bool have_fmt = false;
    bool have_data = false;

    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint16_t bits_per_sample = 0;

    size_t offset = 12;
    while (offset + 8 <= buf.size()) {
        const unsigned char *ch = p + offset;
        uint32_t chunk_size = read_le32(ch + 4);
        size_t data_start = offset + 8;
        if (data_start > buf.size()) break;

        if (ch[0] == 'f' && ch[1] == 'm' && ch[2] == 't' && ch[3] == ' ') {
            if (chunk_size >= 16 && data_start + 16 <= buf.size()) {
                const unsigned char *q = ch + 8;
                audio_format    = static_cast<uint16_t>(q[0] | (q[1] << 8));
                num_channels    = static_cast<uint16_t>(q[2] | (q[3] << 8));
                bits_per_sample = static_cast<uint16_t>(q[14] | (q[15] << 8));
                have_fmt = true;
            }
        } else if (ch[0] == 'd' && ch[1] == 'a' && ch[2] == 't' && ch[3] == 'a') {
            if (data_start + chunk_size <= buf.size()) {
                wl.data_offset = data_start;
                wl.data_size   = chunk_size;
                have_data = true;
            } else {
                return wl;
            }
        }

        size_t advance = static_cast<size_t>(8) + chunk_size;
        if (chunk_size & 1) ++advance; // chunks are word aligned
        if (offset + advance > buf.size()) break;
        offset += advance;
    }

    if (!have_fmt || !have_data) return wl;
    if (audio_format != 1) return wl;      // PCM
    if (num_channels != 1) return wl;     // mono only
    if (bits_per_sample != 16) return wl; // 16-bit only

    wl.header_size = wl.data_offset;
    if (wl.header_size + wl.data_size > buf.size()) return wl;
    wl.ok = true;
    return wl;
}

static inline int16_t load_le16(const unsigned char *p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                                (static_cast<uint16_t>(p[1]) << 8));
}

static inline void store_le16(unsigned char *p, int16_t v) {
    uint16_t u = static_cast<uint16_t>(v);
    p[0] = static_cast<unsigned char>(u & 0xFFu);
    p[1] = static_cast<unsigned char>((u >> 8) & 0xFFu);
}

int main(int argc, char **argv) {
    std::vector<unsigned char> input;

    if (argc == 1) {
        if (!read_all_stdin(input)) die("failed to read stdin");
    } else if (argc == 3) {
        const std::string in_path = argv[1];
        if (!read_file(in_path, input)) die("failed to read input file");
    } else {
        std::fprintf(stderr, "Usage: %s [input_file output_file]\n", argv[0]);
        return 1;
    }

    uint64_t orig_size = input.size();

    std::vector<unsigned char> header_bytes;
    std::vector<unsigned char> to_compress;

    WavLayout wl = parse_wav(input);
    if (wl.ok) {
        // WAV path: split header and data, apply delta coding on 16-bit samples.
        header_bytes.assign(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(wl.header_size));

        size_t num_samples = wl.data_size / 2;
        const unsigned char *data = input.data() + wl.data_offset;

        std::vector<unsigned char> residual_bytes(wl.data_size);
        int16_t prev = 0;
        for (size_t i = 0; i < num_samples; ++i) {
            int16_t s = load_le16(data + 2 * i);
            int16_t d = static_cast<int16_t>(s - prev);
            prev = s;
            store_le16(residual_bytes.data() + 2 * i, d);
        }
        to_compress.swap(residual_bytes);
    } else {
        // Fallback: treat the whole file as opaque bytes.
        header_bytes.clear();
        to_compress = input;
    }

    size_t comp_cap = to_compress.empty() ? 1 : to_compress.size() * 2 + 64;
    std::vector<unsigned char> comp(comp_cap);
    size_t comp_size = comp_cap;

    int level = 7;
    int rc = mz_compress2(comp.data(), &comp_size,
                          to_compress.data(), to_compress.size(),
                          level);
    if (rc != 0) die("compression failed");
    comp.resize(comp_size);

    // Print compression statistics to stderr
    uint64_t total_output_size = 4 + 8 + 4 + 8 + header_bytes.size() + comp_size;
    double ratio = orig_size > 0 ? static_cast<double>(orig_size) / static_cast<double>(total_output_size) : 0.0;
    std::fprintf(stderr, "encode: original_size=%llu bytes\n", static_cast<unsigned long long>(orig_size));
    std::fprintf(stderr, "encode: compressed_payload=%zu bytes\n", comp_size);
    std::fprintf(stderr, "encode: header_size=%zu bytes\n", header_bytes.size());
    std::fprintf(stderr, "encode: total_output_size=%llu bytes\n", static_cast<unsigned long long>(total_output_size));
    std::fprintf(stderr, "encode: compression_ratio=%.2fx\n", ratio);
    if (wl.ok) {
        std::fprintf(stderr, "encode: mode=WAV (delta-coded 16-bit mono PCM)\n");
    } else {
        std::fprintf(stderr, "encode: mode=opaque (full file DEFLATE)\n");
    }

    std::vector<unsigned char> output;
    // magic
    output.push_back('N'); output.push_back('L'); output.push_back('C'); output.push_back('2');
    // original file size
    write_le64(orig_size, output);
    // header size (only for WAV path, 0 otherwise)
    write_le32(static_cast<uint32_t>(header_bytes.size()), output);
    // compressed size
    write_le64(static_cast<uint64_t>(comp_size), output);
    // header bytes
    output.insert(output.end(), header_bytes.begin(), header_bytes.end());
    // payload
    output.insert(output.end(), comp.begin(), comp.end());

    if (argc == 1) {
        if (!output.empty()) {
            size_t n = std::fwrite(output.data(), 1, output.size(), stdout);
            if (n != output.size()) die("failed to write stdout");
        }
    } else {
        const std::string out_path = argv[2];
        if (!write_file(out_path, output)) die("failed to write output file");
    }

    return 0;
}



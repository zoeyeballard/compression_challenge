#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "miniz.h"

static void die(const char *msg) {
    std::fprintf(stderr, "decode: %s\n", msg);
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

static uint64_t read_le64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v <<= 8;
        v |= static_cast<uint64_t>(p[i]);
    }
    return v;
}

static uint32_t read_le32(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
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

    if (input.size() < 4 + 8 + 4 + 8) die("input too small");
    if (input[0] != 'N' || input[1] != 'L' || input[2] != 'C' || input[3] != '2')
        die("bad magic");

    const unsigned char *p = input.data() + 4;
    uint64_t orig_size   = read_le64(p);
    uint32_t header_sz   = read_le32(p + 8);
    uint64_t comp_size   = read_le64(p + 12);

    size_t meta_size = 4 + 8 + 4 + 8;
    size_t header_offset = meta_size;
    size_t comp_offset   = header_offset + static_cast<size_t>(header_sz);

    if (comp_offset + comp_size > input.size()) die("truncated input");
    if (orig_size < header_sz) die("corrupt sizes");

    const unsigned char *header_bytes = input.data() + header_offset;
    const unsigned char *comp         = input.data() + comp_offset;

    size_t tail_size = static_cast<size_t>(orig_size - header_sz);

    std::vector<unsigned char> tail;
    tail.resize(tail_size);
    size_t out_len = tail.size();

    int rc = mz_uncompress(tail.data(), &out_len, comp, static_cast<size_t>(comp_size));
    if (rc != 0 || out_len != tail_size) die("decompression failed");

    std::vector<unsigned char> output;
    output.reserve(static_cast<size_t>(orig_size));

    // If header_sz > 0, we assume WAV path with delta-coded 16-bit samples.
    if (header_sz > 0) {
        output.insert(output.end(), header_bytes, header_bytes + header_sz);

        if (tail_size % 2 != 0) die("odd tail size for 16-bit data");
        size_t num_samples = tail_size / 2;
        std::vector<unsigned char> pcm_bytes(tail_size);

        int16_t prev = 0;
        for (size_t i = 0; i < num_samples; ++i) {
            int16_t d = load_le16(tail.data() + 2 * i);
            int16_t s = static_cast<int16_t>(prev + d);
            prev = s;
            store_le16(pcm_bytes.data() + 2 * i, s);
        }

        output.insert(output.end(), pcm_bytes.begin(), pcm_bytes.end());
    } else {
        // Fallback opaque mode: tail already is the original bytes.
        output.insert(output.end(), tail.begin(), tail.end());
    }

    if (output.size() != static_cast<size_t>(orig_size)) die("size mismatch");

    // Print decompression statistics to stderr
    std::fprintf(stderr, "decode: original_size=%llu bytes\n", static_cast<unsigned long long>(orig_size));
    std::fprintf(stderr, "decode: compressed_payload=%llu bytes\n", static_cast<unsigned long long>(comp_size));
    std::fprintf(stderr, "decode: header_size=%u bytes\n", header_sz);
    std::fprintf(stderr, "decode: total_input_size=%zu bytes\n", input.size());
    if (header_sz > 0) {
        std::fprintf(stderr, "decode: mode=WAV (delta-coded 16-bit mono PCM)\n");
    } else {
        std::fprintf(stderr, "decode: mode=opaque (full file DEFLATE)\n");
    }

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



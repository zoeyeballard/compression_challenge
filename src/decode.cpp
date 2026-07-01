#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "codec.h"

// Neuralink Compression Challenge decoder. Inverse of encode.cpp.

static void die(const char *msg) {
    std::fprintf(stderr, "decode: %s\n", msg);
    std::exit(1);
}

static bool read_stream(FILE *f, std::vector<unsigned char> &buf) {
    constexpr size_t CHUNK = 1 << 16;
    unsigned char tmp[CHUNK];
    while (true) {
        size_t n = std::fread(tmp, 1, CHUNK, f);
        if (n) buf.insert(buf.end(), tmp, tmp + n);
        if (n < CHUNK) return !std::ferror(f);
    }
}

static bool read_file(const std::string &path, std::vector<unsigned char> &buf) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = read_stream(f, buf);
    std::fclose(f);
    return ok;
}

static bool write_file(const std::string &path, const std::vector<unsigned char> &buf) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = buf.empty() || std::fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    std::fclose(f);
    return ok;
}

static uint32_t read_le32(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
static uint64_t read_le64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
static inline void store_le16(unsigned char *p, int16_t v) {
    uint16_t u = static_cast<uint16_t>(v);
    p[0] = static_cast<unsigned char>(u & 0xFFu);
    p[1] = static_cast<unsigned char>((u >> 8) & 0xFFu);
}

int main(int argc, char **argv) {
    std::vector<unsigned char> input;
    if (argc == 1) {
        if (!read_stream(stdin, input)) die("failed to read stdin");
    } else if (argc == 3) {
        if (!read_file(argv[1], input)) die("failed to read input file");
    } else {
        std::fprintf(stderr, "Usage: %s [input_file output_file]\n", argv[0]);
        return 1;
    }

    if (input.size() < 4 + 1 + 8) die("input too small");
    if (input[0] != codec::MAGIC[0] || input[1] != codec::MAGIC[1] ||
        input[2] != codec::MAGIC[2] || input[3] != codec::MAGIC[3])
        die("bad magic");

    unsigned char mode = input[4];
    uint64_t orig_size = read_le64(input.data() + 5);

    std::vector<unsigned char> output;
    output.reserve(static_cast<size_t>(orig_size));

    if (mode == codec::MODE_OPAQUE) {
        size_t off = 4 + 1 + 8;
        if (off + orig_size > input.size()) die("truncated opaque input");
        output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(off),
                      input.begin() + static_cast<std::ptrdiff_t>(off + orig_size));
    } else if (mode == codec::MODE_WAV) {
        size_t off = 4 + 1 + 8;
        if (off + 4 > input.size()) die("truncated header");
        uint32_t header_sz = read_le32(input.data() + off);
        off += 4;
        if (off + header_sz > input.size() || orig_size < header_sz) die("corrupt sizes");

        // Header verbatim.
        output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(off),
                      input.begin() + static_cast<std::ptrdiff_t>(off + header_sz));
        off += header_sz;

        size_t data_bytes = static_cast<size_t>(orig_size - header_sz);
        if (data_bytes % 2 != 0) die("odd sample byte count");
        size_t num_samples = data_bytes / 2;

        std::vector<unsigned char> pcm(data_bytes);
        rc::Decoder dec(input.data() + off, input.size() - off);
        codec::Models models;
        int16_t prev = 0;
        for (size_t i = 0; i < num_samples; ++i) {
            int c = codec::ctx_id(prev);
            int32_t d = codec::decode_symbol(dec, models, c);
            int16_t s = static_cast<int16_t>(static_cast<int32_t>(prev) + d);
            store_le16(pcm.data() + 2 * i, s);
            prev = s;
        }
        output.insert(output.end(), pcm.begin(), pcm.end());
    } else {
        die("unknown mode");
    }

    if (output.size() != static_cast<size_t>(orig_size)) die("size mismatch");

    std::fprintf(stderr, "decode: mode=%s original=%llu input=%zu\n",
                 mode == codec::MODE_WAV ? "WAV" : "opaque",
                 static_cast<unsigned long long>(orig_size), input.size());

    if (argc == 1) {
        if (!output.empty() && std::fwrite(output.data(), 1, output.size(), stdout) != output.size())
            die("failed to write stdout");
    } else if (!write_file(argv[2], output)) {
        die("failed to write output file");
    }
    return 0;
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "codec.h"

// Neuralink Compression Challenge encoder.
//
// Reads a 16-bit mono PCM WAV file, forms per-sample deltas, and entropy-codes
// them with a two-level order-1 PPM-C model over a range coder (see codec.h).
// Non-WAV inputs are stored verbatim so the round-trip stays lossless on any
// input.

static void die(const char *msg) {
    std::fprintf(stderr, "encode: %s\n", msg);
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

static void write_le32(uint32_t v, std::vector<unsigned char> &out) {
    for (int i = 0; i < 4; ++i) { out.push_back(v & 0xFFu); v >>= 8; }
}
static void write_le64(uint64_t v, std::vector<unsigned char> &out) {
    for (int i = 0; i < 8; ++i) { out.push_back(v & 0xFFu); v >>= 8; }
}
static uint32_t read_le32(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
static inline int16_t load_le16(const unsigned char *p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

struct WavLayout {
    bool ok = false;
    size_t data_offset = 0; // bytes before first sample (== header size)
    size_t data_size = 0;   // bytes of sample data
};

// Parse a minimal RIFF/WAVE header and locate the "data" chunk. We require
// PCM, mono, 16-bit and that the data chunk runs to the end of the file (true
// for the challenge data) so the delta transform can be applied cleanly.
static WavLayout parse_wav(const std::vector<unsigned char> &buf) {
    WavLayout wl;
    if (buf.size() < 44) return wl;
    const unsigned char *p = buf.data();
    if (p[0] != 'R' || p[1] != 'I' || p[2] != 'F' || p[3] != 'F') return wl;
    if (p[8] != 'W' || p[9] != 'A' || p[10] != 'V' || p[11] != 'E') return wl;

    bool have_fmt = false, have_data = false;
    uint16_t audio_format = 0, num_channels = 0, bits_per_sample = 0;
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
                wl.data_size = chunk_size;
                have_data = true;
            } else {
                return wl;
            }
        }
        size_t advance = static_cast<size_t>(8) + chunk_size + (chunk_size & 1);
        if (offset + advance > buf.size()) break;
        offset += advance;
    }
    if (!have_fmt || !have_data) return wl;
    if (audio_format != 1 || num_channels != 1 || bits_per_sample != 16) return wl;
    // Require the data chunk to be the tail of the file and an even byte count.
    if (wl.data_offset + wl.data_size != buf.size()) return wl;
    if (wl.data_size % 2 != 0) return wl;
    wl.ok = true;
    return wl;
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

    const uint64_t orig_size = input.size();
    std::vector<unsigned char> output;
    output.insert(output.end(), codec::MAGIC, codec::MAGIC + 4);

    WavLayout wl = parse_wav(input);
    if (wl.ok) {
        output.push_back(codec::MODE_WAV);
        write_le64(orig_size, output);
        write_le32(static_cast<uint32_t>(wl.data_offset), output);
        output.insert(output.end(), input.begin(),
                      input.begin() + static_cast<std::ptrdiff_t>(wl.data_offset));

        const unsigned char *data = input.data() + wl.data_offset;
        size_t num_samples = wl.data_size / 2;

        rc::Encoder enc(output);
        codec::Models models;
        int16_t prev = 0;
        for (size_t i = 0; i < num_samples; ++i) {
            int16_t s = load_le16(data + 2 * i);
            int c = codec::ctx_id(prev);
            int32_t d = static_cast<int32_t>(s) - static_cast<int32_t>(prev);
            codec::encode_symbol(enc, models, c, d);
            prev = s;
        }
        enc.flush();
    } else {
        output.push_back(codec::MODE_OPAQUE);
        write_le64(orig_size, output);
        output.insert(output.end(), input.begin(), input.end());
    }

    double ratio = orig_size ? static_cast<double>(orig_size) / output.size() : 0.0;
    std::fprintf(stderr, "encode: mode=%s original=%llu compressed=%zu ratio=%.4fx\n",
                 wl.ok ? "WAV" : "opaque",
                 static_cast<unsigned long long>(orig_size), output.size(), ratio);

    if (argc == 1) {
        if (!output.empty() && std::fwrite(output.data(), 1, output.size(), stdout) != output.size())
            die("failed to write stdout");
    } else if (!write_file(argv[2], output)) {
        die("failed to write output file");
    }
    return 0;
}

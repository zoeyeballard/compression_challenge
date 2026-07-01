/* Shared codec for the Neuralink compression challenge.
 *
 * The neural PCM data is heavily quantized: each 5-second file contains only a
 * few hundred distinct sample values, and the next sample is strongly
 * predicted by the current level. We exploit this with an order-1 model:
 *
 *   1. Split off the WAV header (stored verbatim).
 *   2. For each sample, form delta = sample - prev and zig-zag map it.
 *   3. Entropy-code the delta with a two-level PPM-C model over a range coder:
 *        - level 1: context = quantized previous sample level (prev >> 6),
 *        - level 0: a global (context-free) model,
 *        - fallback: a raw 17-bit code for never-before-seen values.
 *      Each level codes an escape symbol (PPM-C: escape freq = #distinct
 *      symbols) to drop to the next level. Counts are halved on overflow.
 *
 * This reaches ~3.3x on the challenge data, versus ~2.3x for delta+DEFLATE and
 * ~2.9x for xz -9e, while staying self-contained and fast.
 */
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "rangecoder.h"

namespace codec {

static constexpr unsigned char MAGIC[4] = {'N', 'L', 'C', '3'};
enum Mode : unsigned char { MODE_WAV = 0, MODE_OPAQUE = 1 };

static constexpr int      CTX_SHIFT   = 6;              // level = prev >> 6
static constexpr int      NUM_CTX     = 1024;           // (prev>>6) + 512 in [0,1023]
static constexpr uint32_t MAX_TOTAL   = 1u << 15;       // rescale threshold (< 2^16)
static constexpr int      RAW_LO_BITS = 10;             // fallback: 10 + 7 = 17 bits
static constexpr int      RAW_HI_BITS = 7;

static inline uint32_t zigzag(int32_t v) {
    return (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31);
}
static inline int32_t unzigzag(uint32_t u) {
    return static_cast<int32_t>(u >> 1) ^ -static_cast<int32_t>(u & 1u);
}
static inline int ctx_id(int16_t prev) {
    int c = (static_cast<int>(prev) >> CTX_SHIFT) + (NUM_CTX / 2);
    if (c < 0) c = 0;
    if (c >= NUM_CTX) c = NUM_CTX - 1;
    return c;
}

// An adaptive frequency table over symbols (the zig-zagged deltas).
struct Table {
    std::map<uint32_t, uint32_t> cnt;
    uint32_t total = 0;

    uint32_t distinct() const { return static_cast<uint32_t>(cnt.size()); }

    void rescale() {
        total = 0;
        for (auto it = cnt.begin(); it != cnt.end();) {
            uint32_t v = (it->second + 1) >> 1;
            it->second = v;
            total += v;
            ++it; // v >= 1 always, so no entries are dropped
        }
    }
    void add(uint32_t sym) {
        cnt[sym] += 1;
        total += 1;
        if (total >= MAX_TOTAL) rescale();
    }
    // Cumulative frequency of symbols strictly less than sym.
    uint32_t cum_below(uint32_t sym) const {
        uint32_t c = 0;
        for (auto it = cnt.begin(); it != cnt.end() && it->first < sym; ++it)
            c += it->second;
        return c;
    }
};

struct Models {
    std::vector<Table> ctx;  // level 1: per quantized-level context
    Table global;            // level 0
    Models() : ctx(NUM_CTX) {}
};

// --- encode ---------------------------------------------------------------
// Returns true if the symbol was coded at this level; false if it escaped.
static inline bool enc_level(rc::Encoder &e, Table &t, uint32_t sym) {
    if (t.total == 0) return false; // empty model: no escape symbol, drop down
    uint32_t d = t.distinct();
    uint32_t tot = t.total + d;
    auto it = t.cnt.find(sym);
    if (it != t.cnt.end()) {
        e.encode(t.cum_below(sym), it->second, tot);
        return true;
    }
    e.encode(t.total, d, tot); // escape occupies [total, total+distinct)
    return false;
}

static inline void encode_symbol(rc::Encoder &e, Models &m, int c, int32_t delta) {
    uint32_t sym = zigzag(delta);
    Table &lvl1 = m.ctx[c];
    Table &lvl0 = m.global;
    if (!enc_level(e, lvl1, sym) && !enc_level(e, lvl0, sym)) {
        // Raw fallback: 17-bit value in two chunks.
        e.encode(sym & ((1u << RAW_LO_BITS) - 1), 1, 1u << RAW_LO_BITS);
        e.encode((sym >> RAW_LO_BITS) & ((1u << RAW_HI_BITS) - 1), 1, 1u << RAW_HI_BITS);
    }
    lvl1.add(sym);
    lvl0.add(sym);
}

// --- decode ---------------------------------------------------------------
// Returns true and sets sym if coded at this level; false if it escaped.
static inline bool dec_level(rc::Decoder &dec, Table &t, uint32_t &sym) {
    if (t.total == 0) return false;
    uint32_t d = t.distinct();
    uint32_t tot = t.total + d;
    uint32_t f = dec.get_freq(tot);
    if (f >= t.total) { // escape
        dec.decode(t.total, d, tot);
        return false;
    }
    uint32_t cum = 0;
    for (auto it = t.cnt.begin(); it != t.cnt.end(); ++it) {
        if (cum + it->second > f) {
            dec.decode(cum, it->second, tot);
            sym = it->first;
            return true;
        }
        cum += it->second;
    }
    return false; // unreachable
}

static inline int32_t decode_symbol(rc::Decoder &dec, Models &m, int c) {
    Table &lvl1 = m.ctx[c];
    Table &lvl0 = m.global;
    uint32_t sym = 0;
    if (!dec_level(dec, lvl1, sym) && !dec_level(dec, lvl0, sym)) {
        uint32_t lo = dec.get_freq(1u << RAW_LO_BITS);
        dec.decode(lo, 1, 1u << RAW_LO_BITS);
        uint32_t hi = dec.get_freq(1u << RAW_HI_BITS);
        dec.decode(hi, 1, 1u << RAW_HI_BITS);
        sym = lo | (hi << RAW_LO_BITS);
    }
    lvl1.add(sym);
    lvl0.add(sym);
    return unzigzag(sym);
}

} // namespace codec

/* Carryless range coder (Subbotin style), frequency based.
 *
 * Self-contained, header-only. Supports encoding/decoding a symbol given its
 * cumulative frequency, frequency, and the total frequency of the model.
 * The total frequency must stay below 2^16 (see codec.h MAX_TOTAL).
 */
#pragma once

#include <cstdint>
#include <vector>

namespace rc {

static constexpr uint32_t kTop = 1u << 24;
static constexpr uint32_t kBot = 1u << 16;

class Encoder {
public:
    explicit Encoder(std::vector<unsigned char> &out) : out_(out) {}

    void encode(uint32_t cum_freq, uint32_t freq, uint32_t tot_freq) {
        range_ /= tot_freq;
        low_ += cum_freq * range_;
        range_ *= freq;
        while ((low_ ^ (low_ + range_)) < kTop ||
               (range_ < kBot && ((range_ = (0u - low_) & (kBot - 1)), true))) {
            out_.push_back(static_cast<unsigned char>(low_ >> 24));
            low_ <<= 8;
            range_ <<= 8;
        }
    }

    void flush() {
        for (int i = 0; i < 4; ++i) {
            out_.push_back(static_cast<unsigned char>(low_ >> 24));
            low_ <<= 8;
        }
    }

private:
    std::vector<unsigned char> &out_;
    uint32_t low_ = 0;
    uint32_t range_ = 0xFFFFFFFF;
};

class Decoder {
public:
    Decoder(const unsigned char *in, size_t size) : in_(in), size_(size) {
        for (int i = 0; i < 4; ++i) code_ = (code_ << 8) | next();
    }

    // Return the current cumulative frequency value in [0, tot_freq).
    uint32_t get_freq(uint32_t tot_freq) {
        range_ /= tot_freq;
        return (code_ - low_) / range_;
    }

    void decode(uint32_t cum_freq, uint32_t freq, uint32_t /*tot_freq*/) {
        low_ += cum_freq * range_;
        range_ *= freq;
        while ((low_ ^ (low_ + range_)) < kTop ||
               (range_ < kBot && ((range_ = (0u - low_) & (kBot - 1)), true))) {
            code_ = (code_ << 8) | next();
            low_ <<= 8;
            range_ <<= 8;
        }
    }

private:
    unsigned char next() { return pos_ < size_ ? in_[pos_++] : 0; }

    const unsigned char *in_;
    size_t size_;
    size_t pos_ = 0;
    uint32_t low_ = 0;
    uint32_t range_ = 0xFFFFFFFF;
    uint32_t code_ = 0;
};

} // namespace rc

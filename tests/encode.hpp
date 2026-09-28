// Test-only AIS encoder: builds payloads with known field values so that the
// decoder can be checked field by field. Real-world coverage comes from the
// captured feed in testdata/ (see capture_test.cpp).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "maritime/nmea/sentence.hpp"

namespace maritime::test {

class BitWriter {
public:
    BitWriter& u(std::uint64_t value, int len) {
        for (int i = len - 1; i >= 0; --i) {
            bits_.push_back(i >= 64 ? std::uint8_t{0} : static_cast<std::uint8_t>((value >> i) & 1U));
        }
        return *this;
    }
    BitWriter& s(std::int64_t value, int len) { return u(static_cast<std::uint64_t>(value) & ((1ULL << len) - 1), len); }
    BitWriter& text(std::string_view str, int chars) {
        constexpr std::string_view table =
            "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_ !\"#$%&'()*+,-./0123456789:;<=>?";
        for (int i = 0; i < chars; ++i) {
            const char c = i < static_cast<int>(str.size()) ? str[static_cast<std::size_t>(i)] : '@';
            u(table.find(c), 6);
        }
        return *this;
    }
    [[nodiscard]] std::size_t size() const { return bits_.size(); }

    // Returns the armored payload; `fill_bits` receives the padding count.
    [[nodiscard]] std::string armor(int& fill_bits) const {
        auto bits = bits_;
        fill_bits = static_cast<int>((6 - bits.size() % 6) % 6);
        bits.resize(bits.size() + static_cast<std::size_t>(fill_bits), 0);
        std::string out;
        for (std::size_t i = 0; i < bits.size(); i += 6) {
            int v = 0;
            for (std::size_t j = 0; j < 6; ++j) v = (v << 1) | bits[i + j];
            v += 48;
            if (v > 87) v += 8;
            out.push_back(static_cast<char>(v));
        }
        return out;
    }

private:
    std::vector<std::uint8_t> bits_;
};

inline std::string sentence(std::string_view payload, int fill, int count = 1, int number = 1,
                            std::string_view seq = "", char channel = 'A', std::string_view talker = "AI") {
    std::string body = std::string("!") + std::string(talker) + "VDM," + std::to_string(count) + ',' +
                       std::to_string(number) + ',' + std::string(seq) + ',' + std::string(1, channel) + ',' +
                       std::string(payload) + ',' + std::to_string(fill);
    char cs[4];
    std::snprintf(cs, sizeof cs, "%02X", nmea::checksum(std::string_view(body).substr(1)));
    return body + '*' + cs;
}

}  // namespace maritime::test

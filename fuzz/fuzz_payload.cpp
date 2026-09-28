// libFuzzer target for the message decoders. Random bytes almost never pass
// the NMEA checksum, so this target skips the sentence layer: it maps every
// byte onto a valid 6-bit armor character and decodes the result directly.
#include <cstddef>
#include <cstdint>
#include <string>

#include "maritime/ais/bits.hpp"
#include "maritime/ais/messages.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const int fill_bits = data[0] % 6;
    std::string payload;
    payload.reserve(size - 1);
    for (std::size_t i = 1; i < size; ++i) {
        const int v = data[i] % 64;
        payload.push_back(static_cast<char>(v < 40 ? v + 48 : v + 56));
    }
    if (const auto bits = maritime::ais::BitBuffer::from_payload(payload, fill_bits)) {
        (void)maritime::ais::decode(*bits);
    }
    return 0;
}

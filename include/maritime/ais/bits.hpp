// De-armoring of AIS 6-bit payloads and bounds-checked bit-field reads.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace maritime::ais {

// Payload bits, most significant bit first, one bool-like byte per bit.
class BitBuffer {
public:
    // Decodes armored payload characters. Returns nullopt on an invalid
    // character or if fill_bits exceeds the available bits.
    [[nodiscard]] static std::optional<BitBuffer> from_payload(std::string_view payload, int fill_bits);

    [[nodiscard]] std::size_t size() const noexcept { return bits_.size(); }

    // All readers return nullopt when [start, start+len) runs past the end.
    [[nodiscard]] std::optional<std::uint32_t> u(std::size_t start, std::size_t len) const;
    [[nodiscard]] std::optional<std::int32_t> s(std::size_t start, std::size_t len) const;
    // AIS 6-bit text, cut at the first '@' (padding) and trimmed of spaces.
    [[nodiscard]] std::optional<std::string> text(std::size_t start, std::size_t len) const;

    // Zero-extending variants for decoders that have already checked the
    // message length: bits past the end read as 0 (text as '@' padding), so
    // a transmitter that drops the last few spare bits still decodes.
    [[nodiscard]] std::uint32_t get(std::size_t start, std::size_t len) const noexcept;
    [[nodiscard]] std::int32_t get_signed(std::size_t start, std::size_t len) const noexcept;
    [[nodiscard]] std::string get_text(std::size_t start, std::size_t len) const;

private:
    std::vector<std::uint8_t> bits_;
};

}  // namespace maritime::ais

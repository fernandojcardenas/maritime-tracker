#include "maritime/ais/bits.hpp"

namespace maritime::ais {

namespace {

constexpr std::string_view kSixBitAscii =
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_ !\"#$%&'()*+,-./0123456789:;<=>?";

std::optional<std::uint8_t> dearmor(char c) {
    int v = static_cast<unsigned char>(c) - 48;
    if (v > 40) v -= 8;
    if (v < 0 || v > 63) return std::nullopt;
    // Reject the gap between 'W' and '`', which maps into 40..47 twice.
    if (c > 'W' && c < '`') return std::nullopt;
    return static_cast<std::uint8_t>(v);
}

}  // namespace

std::optional<BitBuffer> BitBuffer::from_payload(std::string_view payload, int fill_bits) {
    BitBuffer buf;
    buf.bits_.reserve(payload.size() * 6);
    for (const char c : payload) {
        const auto v = dearmor(c);
        if (!v) return std::nullopt;
        for (unsigned i = 6; i-- > 0;) buf.bits_.push_back(static_cast<std::uint8_t>((static_cast<unsigned>(*v) >> i) & 1U));
    }
    if (fill_bits < 0 || static_cast<std::size_t>(fill_bits) > buf.bits_.size()) return std::nullopt;
    buf.bits_.resize(buf.bits_.size() - static_cast<std::size_t>(fill_bits));
    return buf;
}

std::uint32_t BitBuffer::get(std::size_t start, std::size_t len) const noexcept {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < len && i < 32; ++i) {
        const std::size_t pos = start + i;
        v = (v << 1U) | (pos < bits_.size() ? bits_[pos] : 0U);
    }
    return v;
}

std::int32_t BitBuffer::get_signed(std::size_t start, std::size_t len) const noexcept {
    if (len == 0 || len > 32) return 0;
    const std::uint32_t raw = get(start, len);
    if (len == 32) return static_cast<std::int32_t>(raw);
    if ((raw & (1U << (len - 1))) != 0U) {
        return static_cast<std::int32_t>(static_cast<std::int64_t>(raw) - (std::int64_t{1} << len));
    }
    return static_cast<std::int32_t>(raw);
}

std::string BitBuffer::get_text(std::size_t start, std::size_t len) const {
    std::string out;
    out.reserve(len / 6);
    for (std::size_t i = 0; i + 6 <= len; i += 6) out.push_back(kSixBitAscii[get(start + i, 6)]);
    if (const auto at = out.find('@'); at != std::string::npos) out.resize(at);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    const auto first = out.find_first_not_of(' ');
    out.erase(0, first == std::string::npos ? out.size() : first);
    return out;
}

std::optional<std::uint32_t> BitBuffer::u(std::size_t start, std::size_t len) const {
    if (len == 0 || len > 32 || start > bits_.size() || len > bits_.size() - start) return std::nullopt;
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < len; ++i) v = (v << 1U) | bits_[start + i];
    return v;
}

std::optional<std::int32_t> BitBuffer::s(std::size_t start, std::size_t len) const {
    if (!u(start, len)) return std::nullopt;
    return get_signed(start, len);
}

std::optional<std::string> BitBuffer::text(std::size_t start, std::size_t len) const {
    if (len % 6 != 0 || start > bits_.size() || len > bits_.size() - start) return std::nullopt;
    // '@' is padding; some transmitters pad with '@' then garbage, so cut at the first one.
    return get_text(start, len);
}

}  // namespace maritime::ais

// Stream decoder: feeds raw lines in, emits decoded messages, reassembling
// multi-sentence messages and keeping counters for every failure mode.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "maritime/ais/messages.hpp"
#include "maritime/nmea/sentence.hpp"

namespace maritime::ais {

struct DecodedMessage {
    Message message;
    std::optional<std::int64_t> unix_time;  // from the tag block of the last fragment
    std::optional<std::string> source;      // receiving station, if tagged
    char channel = '\0';
};

struct DecoderStats {
    std::uint64_t lines = 0;
    std::uint64_t sentences_ok = 0;
    std::array<std::uint64_t, 10> parse_errors{};  // indexed by nmea::ParseError
    std::uint64_t fragments_buffered = 0;
    std::uint64_t fragments_dropped = 0;  // out of order, duplicate or evicted
    std::uint64_t decode_too_short = 0;
    std::uint64_t decode_invalid = 0;
    std::uint64_t decode_unknown_type = 0;
    std::uint64_t messages = 0;
    std::array<std::uint64_t, 64> by_type{};
};

class Decoder {
public:
    // Maximum incomplete multi-fragment messages held at once; the oldest is
    // evicted beyond this, which bounds memory on hostile or lossy input.
    explicit Decoder(std::size_t max_pending = 256) : max_pending_(max_pending) {}

    // Returns a message when `line` completes one, otherwise nullopt.
    std::optional<DecodedMessage> feed(std::string_view line);

    [[nodiscard]] const DecoderStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t pending() const noexcept { return pending_.size(); }

private:
    // (source, sequence id, channel, fragment count)
    using Key = std::tuple<std::string, int, char, int>;
    struct Pending {
        std::vector<std::string> payloads;
    };

    std::optional<DecodedMessage> finish(const std::string& payload, int fill_bits, const nmea::Sentence& last);
    void evict_if_full();

    std::size_t max_pending_;
    std::map<Key, Pending> pending_;
    std::deque<Key> order_;  // insertion order for eviction
    DecoderStats stats_;
};

}  // namespace maritime::ais

#include "maritime/ais/decoder.hpp"

#include <algorithm>

namespace maritime::ais {

std::optional<DecodedMessage> Decoder::feed(std::string_view line) {
    ++stats_.lines;
    auto parsed = nmea::parse_sentence(line);
    if (!parsed) {
        ++stats_.parse_errors.at(static_cast<std::size_t>(parsed.error()));
        return std::nullopt;
    }
    ++stats_.sentences_ok;
    const nmea::Sentence& s = parsed.value();

    if (s.fragment_count == 1) return finish(s.payload, s.fill_bits, s);

    const Key key{s.tag.source.value_or(""), s.sequence_id.value_or(-1), s.channel, s.fragment_count};
    auto it = pending_.find(key);

    if (s.fragment_number == 1) {
        if (it != pending_.end()) {
            // A new message reuses the key before the old one finished: drop the old one.
            stats_.fragments_dropped += it->second.payloads.size();
            pending_.erase(it);
            order_.erase(std::find(order_.begin(), order_.end(), key));
        }
        evict_if_full();
        pending_.emplace(key, Pending{{s.payload}});
        order_.push_back(key);
        ++stats_.fragments_buffered;
        return std::nullopt;
    }

    if (it == pending_.end() || static_cast<int>(it->second.payloads.size()) + 1 != s.fragment_number) {
        ++stats_.fragments_dropped;  // missing predecessor or out of order
        return std::nullopt;
    }

    it->second.payloads.push_back(s.payload);
    ++stats_.fragments_buffered;
    if (s.fragment_number < s.fragment_count) return std::nullopt;

    std::string payload;
    for (const auto& p : it->second.payloads) payload += p;
    pending_.erase(it);
    order_.erase(std::find(order_.begin(), order_.end(), key));
    return finish(payload, s.fill_bits, s);
}

std::optional<DecodedMessage> Decoder::finish(const std::string& payload, int fill_bits, const nmea::Sentence& last) {
    const auto bits = BitBuffer::from_payload(payload, fill_bits);
    if (!bits) {
        ++stats_.decode_invalid;
        return std::nullopt;
    }
    auto msg = decode(*bits);
    if (!msg) {
        if (msg.error() == DecodeError::TooShort) {
            ++stats_.decode_too_short;
        } else if (msg.error() == DecodeError::UnknownType) {
            ++stats_.decode_unknown_type;
        } else {
            ++stats_.decode_invalid;
        }
        return std::nullopt;
    }
    ++stats_.messages;
    ++stats_.by_type.at(type_of(*msg) & 63U);
    return DecodedMessage{std::move(msg).value(), last.tag.unix_time, last.tag.source, last.channel};
}

void Decoder::evict_if_full() {
    while (pending_.size() >= max_pending_ && !order_.empty()) {
        const auto oldest = order_.front();
        order_.pop_front();
        if (auto it = pending_.find(oldest); it != pending_.end()) {
            stats_.fragments_dropped += it->second.payloads.size();
            pending_.erase(it);
        }
    }
}

}  // namespace maritime::ais

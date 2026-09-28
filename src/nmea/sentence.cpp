#include "maritime/nmea/sentence.hpp"

#include <array>
#include <charconv>
#include <vector>

namespace maritime::nmea {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    return s;
}

std::optional<int> hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return std::nullopt;
}

// Validates "<body>*hh" and returns body. `s` must not include the start char.
std::optional<std::string_view> verify_checksum(std::string_view s, bool& had_star) {
    const auto star = s.rfind('*');
    had_star = star != std::string_view::npos && star + 3 == s.size();
    if (!had_star) return std::nullopt;
    const auto hi = hex_digit(s[star + 1]);
    const auto lo = hex_digit(s[star + 2]);
    if (!hi || !lo) {
        had_star = false;
        return std::nullopt;
    }
    const auto body = s.substr(0, star);
    if (checksum(body) != static_cast<std::uint8_t>((*hi << 4) | *lo)) return std::nullopt;
    return body;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const auto pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
}

template <class Int>
std::optional<Int> to_int(std::string_view s) {
    Int v{};
    const auto* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc{} || ptr != end || s.empty()) return std::nullopt;
    return v;
}

bool valid_armor_char(char c) {
    return (c >= '0' && c <= 'W') || (c >= '`' && c <= 'w');
}

Expected<TagBlock, ParseError> parse_tag_block(std::string_view content) {
    bool had_star = false;
    const auto body = verify_checksum(content, had_star);
    if (!body) return Unexpected{ParseError::BadTagBlock};
    TagBlock tag;
    for (auto field : split(*body, ',')) {
        if (field.size() < 2 || field[1] != ':') continue;  // unknown or empty field: ignore
        const auto value = field.substr(2);
        switch (field[0]) {
            case 's':
                tag.source = std::string(value);
                break;
            case 'c':
                // Some sources send milliseconds; normalise to seconds.
                if (auto t = to_int<std::int64_t>(value)) {
                    tag.unix_time = *t > 100'000'000'000 ? *t / 1000 : *t;
                }
                break;
            default:
                break;
        }
    }
    return tag;
}

}  // namespace

std::string_view to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::Empty: return "empty";
        case ParseError::MissingStart: return "missing_start";
        case ParseError::MissingChecksum: return "missing_checksum";
        case ParseError::BadChecksum: return "bad_checksum";
        case ParseError::BadTagBlock: return "bad_tag_block";
        case ParseError::WrongFieldCount: return "wrong_field_count";
        case ParseError::UnsupportedSentence: return "unsupported_sentence";
        case ParseError::BadFragmentFields: return "bad_fragment_fields";
        case ParseError::BadFillBits: return "bad_fill_bits";
        case ParseError::BadPayloadChar: return "bad_payload_char";
    }
    return "unknown";
}

std::uint8_t checksum(std::string_view body) noexcept {
    std::uint8_t sum = 0;
    for (const char c : body) sum ^= static_cast<std::uint8_t>(c);
    return sum;
}

Expected<Sentence, ParseError> parse_sentence(std::string_view line) {
    line = trim(line);
    if (line.empty()) return Unexpected{ParseError::Empty};

    TagBlock tag;
    if (line.front() == '\\') {
        const auto close = line.find('\\', 1);
        if (close == std::string_view::npos) return Unexpected{ParseError::BadTagBlock};
        auto parsed = parse_tag_block(line.substr(1, close - 1));
        if (!parsed) return Unexpected{parsed.error()};
        tag = std::move(parsed).value();
        line.remove_prefix(close + 1);
    }

    if (line.empty() || line.front() != '!') return Unexpected{ParseError::MissingStart};
    line.remove_prefix(1);

    bool had_star = false;
    const auto body = verify_checksum(line, had_star);
    if (!had_star) return Unexpected{ParseError::MissingChecksum};
    if (!body) return Unexpected{ParseError::BadChecksum};

    const auto fields = split(*body, ',');
    if (fields.size() != 7) return Unexpected{ParseError::WrongFieldCount};

    const auto address = fields[0];
    if (address.size() != 5) return Unexpected{ParseError::UnsupportedSentence};
    const auto type = address.substr(2);
    if (type != "VDM" && type != "VDO") return Unexpected{ParseError::UnsupportedSentence};

    Sentence s;
    s.talker = std::string(address.substr(0, 2));
    s.own_ship = type == "VDO";
    s.tag = std::move(tag);

    const auto count = to_int<int>(fields[1]);
    const auto number = to_int<int>(fields[2]);
    if (!count || !number || *count < 1 || *count > 9 || *number < 1 || *number > *count) {
        return Unexpected{ParseError::BadFragmentFields};
    }
    s.fragment_count = *count;
    s.fragment_number = *number;

    if (!fields[3].empty()) {
        const auto seq = to_int<int>(fields[3]);
        if (!seq || *seq < 0 || *seq > 9) return Unexpected{ParseError::BadFragmentFields};
        s.sequence_id = seq;
    }

    if (fields[4].size() > 1) return Unexpected{ParseError::BadFragmentFields};
    s.channel = fields[4].empty() ? '\0' : fields[4].front();

    for (const char c : fields[5]) {
        if (!valid_armor_char(c)) return Unexpected{ParseError::BadPayloadChar};
    }
    s.payload = std::string(fields[5]);

    const auto fill = to_int<int>(fields[6]);
    if (!fill || *fill < 0 || *fill > 5) return Unexpected{ParseError::BadFillBits};
    s.fill_bits = *fill;

    return s;
}

}  // namespace maritime::nmea

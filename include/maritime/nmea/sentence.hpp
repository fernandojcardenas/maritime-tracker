// Parsing of NMEA 0183 AIS sentences (!AIVDM / !AIVDO and other talkers),
// including an optional IEC 62320-1 tag block prefix such as
//   \s:2573485,c:1727366000*0B\!BSVDM,1,1,,A,13m7...,0*5C
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "maritime/util/expected.hpp"

namespace maritime::nmea {

enum class ParseError : std::uint8_t {
    Empty,
    MissingStart,         // no '!' sentence start
    MissingChecksum,      // no '*hh' suffix
    BadChecksum,          // checksum digits do not match content
    BadTagBlock,          // malformed or failing-checksum tag block
    WrongFieldCount,      // VDM/VDO sentences have exactly 7 fields
    UnsupportedSentence,  // not a VDM or VDO sentence
    BadFragmentFields,    // fragment count/number missing or out of range
    BadFillBits,          // fill bits not 0..5
    BadPayloadChar,       // payload contains a char outside the AIS 6-bit armor set
};

[[nodiscard]] std::string_view to_string(ParseError e) noexcept;

// Fields from an IEC 62320-1 tag block that we use.
struct TagBlock {
    std::optional<std::string> source;       // s: receiving station
    std::optional<std::int64_t> unix_time;   // c: receive time, seconds since epoch
};

// One VDM/VDO sentence. A message may span several sentences (fragments).
struct Sentence {
    std::string talker;           // e.g. "AI", "BS"
    bool own_ship = false;        // VDO (own vessel) vs VDM
    int fragment_count = 1;       // 1..9
    int fragment_number = 1;      // 1..fragment_count
    std::optional<int> sequence_id;  // 0..9, present on multi-fragment messages
    char channel = '\0';          // 'A', 'B', '1', '2' or '\0' when empty
    std::string payload;          // 6-bit armored payload
    int fill_bits = 0;            // 0..5
    TagBlock tag;
};

// XOR of all bytes in `body` (the text between the start char and '*').
[[nodiscard]] std::uint8_t checksum(std::string_view body) noexcept;

// Parse the content of a tag block: the text between the two backslashes,
// including its "*hh" checksum, e.g. "s:2573485,c:1727366000*0B".
[[nodiscard]] Expected<TagBlock, ParseError> parse_tag_block(std::string_view content);

// Parse one line. Trailing "\r\n" and whitespace are ignored.
[[nodiscard]] Expected<Sentence, ParseError> parse_sentence(std::string_view line);

}  // namespace maritime::nmea

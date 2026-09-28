// JSON output for decoded messages (one object per line) and decoder stats.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>

#include "maritime/ais/decoder.hpp"

namespace maritime::ais {

// Writes one JSON object and a newline. `line_no`, when given, is the input
// line that completed the message.
void write_json(std::ostream& os, const DecodedMessage& dm, std::optional<std::uint64_t> line_no = std::nullopt);

// Human-readable decoder statistics.
void write_stats(std::ostream& os, const DecoderStats& s, std::size_t unique_mmsi);

}  // namespace maritime::ais

// Records from the BarentsWatch Live AIS API (Norwegian open AIS data,
// NLOD licence), which streams one JSON object per line over HTTPS, e.g.
//   {"courseOverGround":289,"latitude":62.796623,"longitude":6.904042,
//    "speedOverGround":0,"trueHeading":95,"mmsi":257918900,
//    "msgtime":"2023-04-21T01:38:11+00:00", ...}
// Records without a position (static data) are not fixes.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "maritime/track/tracker.hpp"

namespace maritime::track {

// Parses an ISO 8601 timestamp with a UTC offset or 'Z', optional fractional
// seconds ("2023-04-21T01:38:11+00:00", "2023-04-21T01:38:11.25Z") into Unix
// seconds.
[[nodiscard]] std::optional<double> parse_iso8601(std::string_view s);

enum class BwParse : std::uint8_t {
    Fix,        // a usable position report
    NoPosition, // valid record without latitude/longitude (e.g. static data)
    Invalid,    // not JSON, not an object, or missing/invalid mmsi or msgtime
};

struct BwResult {
    BwParse kind = BwParse::Invalid;
    std::optional<Fix> fix;
};

// Accepts one line of the stream. A Server-Sent Events "data:" prefix is
// tolerated. Never throws.
[[nodiscard]] BwResult parse_barentswatch(std::string_view line);

}  // namespace maritime::track

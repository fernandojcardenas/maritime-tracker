// Decoded AIS message types (ITU-R M.1371-5). "Not available" sentinel values
// are mapped to std::nullopt; out-of-range values are also rejected so that
// downstream code never sees e.g. latitude 91.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "maritime/ais/bits.hpp"
#include "maritime/util/expected.hpp"

namespace maritime::ais {

struct Position {
    double lat_deg = 0.0;
    double lon_deg = 0.0;
};

struct Dimensions {
    std::uint16_t to_bow_m = 0;
    std::uint16_t to_stern_m = 0;
    std::uint8_t to_port_m = 0;
    std::uint8_t to_starboard_m = 0;
};

// Types 1, 2, 3: Class A position report.
struct PositionReportA {
    std::uint8_t type = 1;
    std::uint32_t mmsi = 0;
    std::uint8_t nav_status = 15;              // 15 = not defined
    std::optional<double> rot_deg_per_min;     // rate of turn, decoded from ROT_AIS
    std::optional<double> sog_knots;
    bool position_accuracy = false;
    std::optional<Position> position;
    std::optional<double> cog_deg;
    std::optional<std::uint16_t> heading_deg;
    std::uint8_t utc_second = 60;              // 60 = not available
};

// Type 5: Class A static and voyage data.
struct StaticVoyageData {
    std::uint32_t mmsi = 0;
    std::optional<std::uint32_t> imo;
    std::string callsign;
    std::string name;
    std::uint8_t ship_type = 0;
    Dimensions dims;
    std::optional<double> draught_m;
    std::string destination;
};

// Type 18: Class B position report. Type 19: extended Class B report.
struct PositionReportB {
    std::uint8_t type = 18;
    std::uint32_t mmsi = 0;
    std::optional<double> sog_knots;
    bool position_accuracy = false;
    std::optional<Position> position;
    std::optional<double> cog_deg;
    std::optional<std::uint16_t> heading_deg;
    std::uint8_t utc_second = 60;
    // Type 19 only:
    std::string name;
    std::uint8_t ship_type = 0;
    std::optional<Dimensions> dims;
};

// Type 24: Class B static data, sent as part A (name) or part B (type, callsign, size).
struct StaticDataReport {
    std::uint32_t mmsi = 0;
    std::uint8_t part = 0;  // 0 = A, 1 = B
    std::string name;       // part A
    std::uint8_t ship_type = 0;  // part B
    std::string callsign;        // part B
    std::optional<Dimensions> dims;  // part B
};

// Any other message type: decoded header only.
struct OtherMessage {
    std::uint8_t type = 0;
    std::uint32_t mmsi = 0;
};

using Message = std::variant<PositionReportA, StaticVoyageData, PositionReportB, StaticDataReport, OtherMessage>;

enum class DecodeError : std::uint8_t {
    TooShort,        // fewer bits than the message type requires
    InvalidPayload,  // bad armor characters or fill bits
    UnknownType,     // message id 0 or above 27
};

[[nodiscard]] Expected<Message, DecodeError> decode(const BitBuffer& bits);

[[nodiscard]] std::uint32_t mmsi_of(const Message& m);
[[nodiscard]] std::uint8_t type_of(const Message& m);

}  // namespace maritime::ais

#include "maritime/ais/messages.hpp"

#include <cmath>

namespace maritime::ais {

namespace {

// Each decoder checks the message length first, then reads fields with the
// zero-extending BitBuffer::get* accessors.

std::optional<double> speed(std::uint32_t raw) {
    if (raw == 1023) return std::nullopt;  // not available
    return raw / 10.0;                     // 1022 means ">= 102.2 kn"
}

std::optional<double> course(std::uint32_t raw) {
    if (raw >= 3600) return std::nullopt;  // 3600 = not available
    return raw / 10.0;
}

std::optional<std::uint16_t> heading(std::uint32_t raw) {
    if (raw > 359) return std::nullopt;  // 511 = not available
    return static_cast<std::uint16_t>(raw);
}

std::optional<Position> position(std::int32_t lon_raw, std::int32_t lat_raw) {
    const double lon = lon_raw / 600000.0;
    const double lat = lat_raw / 600000.0;
    // 181 / 91 are the "not available" defaults; anything beyond range is invalid.
    if (std::fabs(lon) > 180.0 || std::fabs(lat) > 90.0) return std::nullopt;
    return Position{lat, lon};
}

std::optional<double> rate_of_turn(std::int32_t raw) {
    if (raw == -128) return std::nullopt;  // not available
    if (raw == 127 || raw == -127) return std::nullopt;  // turning >5 deg/30s, no TI; rate unknown
    const double r = raw / 4.733;
    return std::copysign(r * r, static_cast<double>(raw));
}

Dimensions dimensions(const BitBuffer& b, std::size_t start) {
    return Dimensions{
        static_cast<std::uint16_t>(b.get(start, 9)),
        static_cast<std::uint16_t>(b.get(start + 9, 9)),
        static_cast<std::uint8_t>(b.get(start + 18, 6)),
        static_cast<std::uint8_t>(b.get(start + 24, 6)),
    };
}

Expected<Message, DecodeError> decode_position_a(const BitBuffer& b, std::uint8_t type) {
    // Nominal length is 168 bits. Only the fields decoded here are required
    // (through the UTC second, bit 142): some transmitters declare too many
    // fill bits and cut the trailing radio-status field.
    if (b.size() < 143) return Unexpected{DecodeError::TooShort};
    PositionReportA m;
    m.type = type;
    m.mmsi = b.get(8, 30);
    m.nav_status = static_cast<std::uint8_t>(b.get(38, 4));
    m.rot_deg_per_min = rate_of_turn(b.get_signed(42, 8));
    m.sog_knots = speed(b.get(50, 10));
    m.position_accuracy = b.get(60, 1) != 0;
    m.position = position(b.get_signed(61, 28), b.get_signed(89, 27));
    m.cog_deg = course(b.get(116, 12));
    m.heading_deg = heading(b.get(128, 9));
    m.utc_second = static_cast<std::uint8_t>(b.get(137, 6));
    return Message{m};
}

Expected<Message, DecodeError> decode_static_voyage(const BitBuffer& b) {
    // Spec length is 424 bits; many transmitters send 420 or 422. The
    // destination is the last text field, so decode it from whatever is there.
    if (b.size() < 302) return Unexpected{DecodeError::TooShort};
    StaticVoyageData m;
    m.mmsi = b.get(8, 30);
    const auto imo = b.get(40, 30);
    if (imo != 0) m.imo = imo;
    m.callsign = b.get_text(70, 42);
    m.name = b.get_text(112, 120);
    m.ship_type = static_cast<std::uint8_t>(b.get(232, 8));
    m.dims = dimensions(b, 240);
    const auto draught = b.get(294, 8);
    if (draught != 0) m.draught_m = draught / 10.0;
    const std::size_t dest_bits = std::min<std::size_t>(120, ((b.size() - 302) / 6) * 6);
    if (dest_bits > 0) m.destination = b.get_text(302, dest_bits);
    return Message{m};
}

Expected<Message, DecodeError> decode_position_b(const BitBuffer& b, std::uint8_t type) {
    // Same rule as Class A: require the decoded fields, not the nominal length.
    if (b.size() < (type == 19 ? 301U : 139U)) return Unexpected{DecodeError::TooShort};
    PositionReportB m;
    m.type = type;
    m.mmsi = b.get(8, 30);
    m.sog_knots = speed(b.get(46, 10));
    m.position_accuracy = b.get(56, 1) != 0;
    m.position = position(b.get_signed(57, 28), b.get_signed(85, 27));
    m.cog_deg = course(b.get(112, 12));
    m.heading_deg = heading(b.get(124, 9));
    m.utc_second = static_cast<std::uint8_t>(b.get(133, 6));
    if (type == 19) {
        m.name = b.get_text(143, 120);
        m.ship_type = static_cast<std::uint8_t>(b.get(263, 8));
        m.dims = dimensions(b, 271);
    }
    return Message{m};
}

Expected<Message, DecodeError> decode_static_data(const BitBuffer& b) {
    if (b.size() < 40) return Unexpected{DecodeError::TooShort};
    StaticDataReport m;
    m.mmsi = b.get(8, 30);
    m.part = static_cast<std::uint8_t>(b.get(38, 2));
    if (m.part == 0) {
        if (b.size() < 160) return Unexpected{DecodeError::TooShort};
        m.name = b.get_text(40, 120);
    } else if (m.part == 1) {
        if (b.size() < 162) return Unexpected{DecodeError::TooShort};
        m.ship_type = static_cast<std::uint8_t>(b.get(40, 8));
        m.callsign = b.get_text(90, 42);
        m.dims = dimensions(b, 132);
    }
    return Message{m};
}

}  // namespace

Expected<Message, DecodeError> decode(const BitBuffer& bits) {
    if (bits.size() < 38) return Unexpected{DecodeError::TooShort};
    const auto type = static_cast<std::uint8_t>(bits.get(0, 6));
    switch (type) {
        case 1:
        case 2:
        case 3:
            return decode_position_a(bits, type);
        case 5:
            return decode_static_voyage(bits);
        case 18:
        case 19:
            return decode_position_b(bits, type);
        case 24:
            return decode_static_data(bits);
        default:
            // ITU-R M.1371-5 defines types 1-27; 0 and 28-63 are not valid.
            if (type == 0 || type > 27) return Unexpected{DecodeError::UnknownType};
            return Message{OtherMessage{type, bits.get(8, 30)}};
    }
}

std::uint32_t mmsi_of(const Message& m) {
    return std::visit([](const auto& v) { return v.mmsi; }, m);
}

std::uint8_t type_of(const Message& m) {
    struct Visitor {
        std::uint8_t operator()(const PositionReportA& v) const { return v.type; }
        std::uint8_t operator()(const StaticVoyageData& /*unused*/) const { return 5; }
        std::uint8_t operator()(const PositionReportB& v) const { return v.type; }
        std::uint8_t operator()(const StaticDataReport& /*unused*/) const { return 24; }
        std::uint8_t operator()(const OtherMessage& v) const { return v.type; }
    };
    return std::visit(Visitor{}, m);
}

}  // namespace maritime::ais

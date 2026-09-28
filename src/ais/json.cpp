#include "maritime/ais/json.hpp"

#include <array>
#include <cstdio>
#include <type_traits>

#include "maritime/nmea/sentence.hpp"

namespace maritime::ais {

namespace {

void json_string(std::ostream& os, std::string_view s) {
    os << '"';
    for (const char c : s) {
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::array<char, 8> buf{};
                    if (std::snprintf(buf.data(), buf.size(), R"(\u%04x)", c) > 0) os << buf.data();
                } else {
                    os << c;
                }
        }
    }
    os << '"';
}

template <class T>
void opt(std::ostream& os, const char* key, const std::optional<T>& v) {
    os << ",\"" << key << "\":";
    if (v) {
        os << *v;
    } else {
        os << "null";
    }
}

void pos(std::ostream& os, const std::optional<Position>& p) {
    os << ",\"lat\":";
    if (p) os << p->lat_deg; else os << "null";
    os << ",\"lon\":";
    if (p) os << p->lon_deg; else os << "null";
}

void dims(std::ostream& os, const std::optional<Dimensions>& d) {
    if (!d) return;
    os << ",\"length_m\":" << (d->to_bow_m + d->to_stern_m) << ",\"beam_m\":" << (d->to_port_m + d->to_starboard_m);
}

}  // namespace

void write_json(std::ostream& os, const DecodedMessage& dm, std::optional<std::uint64_t> line_no) {
    const auto old_precision = os.precision(10);
    os << "{\"type\":" << static_cast<int>(type_of(dm.message)) << ",\"mmsi\":" << mmsi_of(dm.message);
    if (line_no) os << ",\"line\":" << *line_no;
    opt(os, "t", dm.unix_time);
    std::visit(
        [&](const auto& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, PositionReportA>) {
                os << ",\"nav_status\":" << static_cast<int>(m.nav_status);
                pos(os, m.position);
                opt(os, "sog", m.sog_knots);
                opt(os, "cog", m.cog_deg);
                opt(os, "heading", m.heading_deg);
                opt(os, "rot", m.rot_deg_per_min);
            } else if constexpr (std::is_same_v<T, PositionReportB>) {
                pos(os, m.position);
                opt(os, "sog", m.sog_knots);
                opt(os, "cog", m.cog_deg);
                opt(os, "heading", m.heading_deg);
                if (m.type == 19) {
                    os << ",\"name\":";
                    json_string(os, m.name);
                    os << ",\"ship_type\":" << static_cast<int>(m.ship_type);
                    dims(os, m.dims);
                }
            } else if constexpr (std::is_same_v<T, StaticVoyageData>) {
                opt(os, "imo", m.imo);
                os << ",\"callsign\":";
                json_string(os, m.callsign);
                os << ",\"name\":";
                json_string(os, m.name);
                os << ",\"ship_type\":" << static_cast<int>(m.ship_type);
                dims(os, std::optional<Dimensions>{m.dims});
                opt(os, "draught_m", m.draught_m);
                os << ",\"destination\":";
                json_string(os, m.destination);
            } else if constexpr (std::is_same_v<T, StaticDataReport>) {
                os << R"(,"part":")" << (m.part == 0 ? 'A' : 'B') << '"';
                if (m.part == 0) {
                    os << ",\"name\":";
                    json_string(os, m.name);
                } else {
                    os << ",\"ship_type\":" << static_cast<int>(m.ship_type) << ",\"callsign\":";
                    json_string(os, m.callsign);
                    dims(os, m.dims);
                }
            }
        },
        dm.message);
    os << "}\n";
    os.precision(old_precision);
}

void write_stats(std::ostream& os, const DecoderStats& s, std::size_t unique_mmsi) {
    os << "lines            " << s.lines << '\n'
       << "sentences ok     " << s.sentences_ok << '\n'
       << "messages         " << s.messages << '\n'
       << "unique MMSIs     " << unique_mmsi << '\n'
       << "fragments dropped " << s.fragments_dropped << '\n'
       << "decode too short " << s.decode_too_short << '\n'
       << "decode invalid   " << s.decode_invalid << '\n'
       << "unknown type     " << s.decode_unknown_type << '\n';
    for (std::size_t i = 0; i < s.parse_errors.size(); ++i) {
        if (s.parse_errors[i] != 0) {
            os << "parse error " << nmea::to_string(static_cast<nmea::ParseError>(i)) << ": " << s.parse_errors[i] << '\n';
        }
    }
    os << "by type:";
    for (std::size_t t = 0; t < s.by_type.size(); ++t) {
        if (s.by_type[t] != 0) os << ' ' << t << '=' << s.by_type[t];
    }
    os << '\n';
}

}  // namespace maritime::ais

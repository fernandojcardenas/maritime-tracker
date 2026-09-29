#include "maritime/track/barentswatch.hpp"

#include <charconv>
#include <cmath>

#include <nlohmann/json.hpp>

namespace maritime::track {

namespace {

std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::optional<unsigned> digits(std::string_view s) {
    unsigned v = 0;
    const auto* end = s.data() + s.size();
    const auto [p, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc{} || p != end || s.empty()) return std::nullopt;
    return v;
}

std::optional<double> number(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return std::nullopt;
    const double v = it->get<double>();
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

}  // namespace

std::optional<double> parse_iso8601(std::string_view s) {
    // YYYY-MM-DDTHH:MM:SS[.fff](Z|+HH:MM|-HH:MM)
    if (s.size() < 20 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') || s[13] != ':' ||
        s[16] != ':') {
        return std::nullopt;
    }
    const auto y = digits(s.substr(0, 4));
    const auto mo = digits(s.substr(5, 2));
    const auto d = digits(s.substr(8, 2));
    const auto h = digits(s.substr(11, 2));
    const auto mi = digits(s.substr(14, 2));
    const auto sec = digits(s.substr(17, 2));
    if (!y || !mo || !d || !h || !mi || !sec || *mo < 1 || *mo > 12 || *d < 1 || *d > 31 || *h > 23 || *mi > 59 ||
        *sec > 60) {
        return std::nullopt;
    }
    std::size_t pos = 19;
    double frac = 0.0;
    if (pos < s.size() && s[pos] == '.') {
        double scale = 0.1;
        ++pos;
        const std::size_t start = pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            if (pos - start < 9) frac += scale * (s[pos] - '0');
            scale /= 10.0;
            ++pos;
        }
        if (pos == start) return std::nullopt;
    }
    std::int64_t offset_s = 0;
    const auto tz = s.substr(pos);
    if (tz == "Z") {
        offset_s = 0;
    } else if (tz.size() == 6 && (tz[0] == '+' || tz[0] == '-') && tz[3] == ':') {
        const auto oh = digits(tz.substr(1, 2));
        const auto om = digits(tz.substr(4, 2));
        if (!oh || !om || *oh > 23 || *om > 59) return std::nullopt;
        offset_s = (std::int64_t{*oh} * 3600 + std::int64_t{*om} * 60) * (tz[0] == '+' ? 1 : -1);
    } else {
        return std::nullopt;
    }
    const std::int64_t days = days_from_civil(*y, *mo, *d);
    const std::int64_t secs = days * 86400 + std::int64_t{*h} * 3600 + std::int64_t{*mi} * 60 + *sec - offset_s;
    return static_cast<double>(secs) + frac;
}

BwResult parse_barentswatch(std::string_view line) {
    if (line.substr(0, 5) == "data:") line.remove_prefix(5);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);

    const auto j = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return {};

    const auto mmsi = number(j, "mmsi");
    const auto time = j.find("msgtime");
    if (!mmsi || *mmsi < 1 || *mmsi > 999999999 || std::floor(*mmsi) != *mmsi || time == j.end() ||
        !time->is_string()) {
        return {};
    }
    const auto t = parse_iso8601(time->get_ref<const std::string&>());
    if (!t) return {};

    const auto lat = number(j, "latitude");
    const auto lon = number(j, "longitude");
    if (!lat || !lon) return {BwParse::NoPosition, std::nullopt};
    if (std::fabs(*lat) > 90.0 || std::fabs(*lon) > 180.0) return {};

    Fix f;
    f.mmsi = static_cast<std::uint32_t>(*mmsi);
    f.t = *t;
    f.lat_deg = *lat;
    f.lon_deg = *lon;
    const auto sog = number(j, "speedOverGround");
    const auto cog = number(j, "courseOverGround");
    if (sog && cog && *sog >= 0.0 && *sog < 102.3 && *cog >= 0.0 && *cog < 360.0) {
        f.sog_knots = sog;
        f.cog_deg = cog;
    }
    return {BwParse::Fix, f};
}

}  // namespace maritime::track

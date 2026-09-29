#include "maritime/track/tracker.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <variant>

namespace maritime::track {

namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

template <class T>
std::optional<T> num(std::string_view s) {
    T v{};
    const auto* end = s.data() + s.size();
    const auto [p, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc{} || p != end || s.empty()) return std::nullopt;
    return v;
}

// "DD/MM/YYYY HH:MM:SS" -> Unix seconds.
std::optional<double> parse_dk_time(std::string_view s) {
    if (s.size() != 19 || s[2] != '/' || s[5] != '/' || s[10] != ' ' || s[13] != ':' || s[16] != ':') {
        return std::nullopt;
    }
    const auto dd = num<unsigned>(s.substr(0, 2));
    const auto mo = num<unsigned>(s.substr(3, 2));
    const auto yy = num<int>(s.substr(6, 4));
    const auto hh = num<unsigned>(s.substr(11, 2));
    const auto mi = num<unsigned>(s.substr(14, 2));
    const auto ss = num<unsigned>(s.substr(17, 2));
    if (!dd || !mo || !yy || !hh || !mi || !ss || *mo < 1 || *mo > 12 || *dd < 1 || *dd > 31 || *hh > 23 ||
        *mi > 59 || *ss > 60) {
        return std::nullopt;
    }
    const auto days = days_from_civil(*yy, *mo, *dd);
    const std::int64_t secs = std::int64_t{*hh} * 3600 + std::int64_t{*mi} * 60 + std::int64_t{*ss};
    return static_cast<double>(days * 86400 + secs);
}

}  // namespace

std::optional<Fix> fix_from(const ais::DecodedMessage& dm) {
    if (!dm.unix_time) return std::nullopt;
    Fix f;
    f.t = static_cast<double>(*dm.unix_time);
    return std::visit(
        [&](const auto& m) -> std::optional<Fix> {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, ais::PositionReportA> || std::is_same_v<T, ais::PositionReportB>) {
                if (!m.position) return std::nullopt;
                f.mmsi = m.mmsi;
                f.lat_deg = m.position->lat_deg;
                f.lon_deg = m.position->lon_deg;
                f.sog_knots = m.sog_knots;
                f.cog_deg = m.cog_deg;
                f.pos_sigma_m = m.position_accuracy ? 10.0 : 25.0;
                return f;
            } else {
                return std::nullopt;
            }
        },
        dm.message);
}

std::optional<Fix> fix_from_dk_csv(std::string_view row) {
    std::array<std::string_view, 9> col{};
    std::size_t n = 0;
    while (n < col.size()) {
        const auto comma = row.find(',');
        col.at(n++) = row.substr(0, comma);
        if (comma == std::string_view::npos) break;
        row.remove_prefix(comma + 1);
    }
    if (n < 7) return std::nullopt;
    const auto t = parse_dk_time(col[0]);
    const auto mmsi = num<std::uint32_t>(col[2]);
    const auto lat = num<double>(col[3]);
    const auto lon = num<double>(col[4]);
    if (!t || !mmsi || !lat || !lon || std::fabs(*lat) > 90.0 || std::fabs(*lon) > 180.0) return std::nullopt;
    Fix f;
    f.mmsi = *mmsi;
    f.t = *t;
    f.lat_deg = *lat;
    f.lon_deg = *lon;
    const auto sog = num<double>(col[5]);
    const auto cog = num<double>(col[6]);
    if (sog && cog && *sog >= 0.0 && *sog < 102.3 && *cog >= 0.0 && *cog < 360.0) {
        f.sog_knots = sog;
        f.cog_deg = cog;
    }
    return f;
}

Measurement Tracker::measurement(const Track& tr, const Fix& f) {
    Measurement m;
    const auto p = tr.frame.to_local(f.lat_deg, f.lon_deg);
    m.east_m = p.east_m;
    m.north_m = p.north_m;
    m.pos_sigma_m = f.pos_sigma_m;
    if (f.sog_knots && f.cog_deg) {
        const auto v = velocity_from_sog_cog(*f.sog_knots, *f.cog_deg);
        m.v_east = v.east_m;
        m.v_north = v.north_m;
    }
    return m;
}

Tracker::Track Tracker::start(const Fix& f) const {
    const LocalFrame frame(f.lat_deg, f.lon_deg);
    Track tr{frame, KalmanCV(Measurement{}, params_.kalman), f.t};
    tr.kf = KalmanCV(measurement(tr, f), params_.kalman);
    tr.updates = 1;
    return tr;
}

std::optional<Prediction> Tracker::predict(std::uint32_t mmsi, double t) const {
    const auto it = tracks_.find(mmsi);
    if (it == tracks_.end()) return std::nullopt;
    KalmanCV kf = it->second.kf;
    kf.predict(t - it->second.t);
    Prediction p;
    it->second.frame.to_geo({kf.state()[0], kf.state()[1]}, p.lat_deg, p.lon_deg);
    p.sigma_m = std::sqrt(0.5 * (kf.covariance()[0][0] + kf.covariance()[1][1]));
    return p;
}

FixOutcome Tracker::add(const Fix& f) {
    ++stats_.fixes;
    auto it = tracks_.find(f.mmsi);
    if (it == tracks_.end()) {
        tracks_.emplace(f.mmsi, start(f));
        ++stats_.started;
        return FixOutcome::Started;
    }
    Track& tr = it->second;
    const double dt = f.t - tr.t;
    if (dt < -params_.min_dt_s) {
        ++stats_.out_of_order;
        return FixOutcome::OutOfOrder;
    }
    if (dt < params_.min_dt_s) {
        ++stats_.duplicates;
        return FixOutcome::Duplicate;
    }

    try {
        KalmanCV predicted = tr.kf;
        predicted.predict(dt);
        const Measurement m = measurement(tr, f);
        if (predicted.position_nis(m) > params_.gate_nis) {
            ++stats_.rejected;
            if (++tr.consecutive_rejects >= params_.restart_after_rejects) {
                tr = start(f);
                ++stats_.restarted;
                return FixOutcome::Restarted;
            }
            return FixOutcome::Rejected;
        }
        predicted.update(m);
        tr.kf = predicted;
        tr.t = f.t;
        tr.consecutive_rejects = 0;
        ++tr.updates;
        ++stats_.updated;

        // Keep the local frame near the vessel so the flat-earth error stays small.
        const auto& x = tr.kf.state();
        if (std::hypot(x[0], x[1]) > params_.reanchor_m) {
            double lat = 0.0;
            double lon = 0.0;
            tr.frame.to_geo({x[0], x[1]}, lat, lon);
            tr.frame = LocalFrame(lat, lon);
            tr.kf.shift(x[0], x[1]);
            ++stats_.reanchored;
        }
        return FixOutcome::Updated;
    } catch (const std::runtime_error&) {
        // Covariance lost positive-definiteness (should not happen with the
        // Joseph form, but never let one track take the process down).
        tr = start(f);
        ++stats_.numeric_resets;
        return FixOutcome::Restarted;
    }
}

std::size_t Tracker::expire(double now) {
    std::size_t n = 0;
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        if (now - it->second.t > params_.stale_after_s) {
            it = tracks_.erase(it);
            ++n;
        } else {
            ++it;
        }
    }
    stats_.expired += n;
    return n;
}

TrackView Tracker::view(std::uint32_t mmsi, const Track& tr) {
    TrackView v;
    v.mmsi = mmsi;
    v.t = tr.t;
    tr.frame.to_geo({tr.kf.state()[0], tr.kf.state()[1]}, v.lat_deg, v.lon_deg);
    v.v_east = tr.kf.state()[2];
    v.v_north = tr.kf.state()[3];
    v.updates = tr.updates;
    return v;
}

std::optional<TrackView> Tracker::track(std::uint32_t mmsi) const {
    const auto it = tracks_.find(mmsi);
    if (it == tracks_.end()) return std::nullopt;
    return view(mmsi, it->second);
}

std::vector<TrackView> Tracker::tracks() const {
    std::vector<TrackView> out;
    out.reserve(tracks_.size());
    for (const auto& [mmsi, tr] : tracks_) out.push_back(view(mmsi, tr));
    return out;
}

}  // namespace maritime::track

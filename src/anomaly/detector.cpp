#include "maritime/anomaly/detector.hpp"

#include <algorithm>
#include <cmath>

#include "maritime/track/geo.hpp"

namespace maritime::anomaly {

std::string_view name(Kind k) noexcept {
    switch (k) {
        case Kind::ImpossibleSpeed:
            return "impossible_speed";
        case Kind::PositionJump:
            return "position_jump";
        case Kind::Gap:
            return "gap";
        case Kind::IdentityConflict:
            return "identity_conflict";
    }
    return "unknown";
}

void Detector::count(Kind k) {
    switch (k) {
        case Kind::ImpossibleSpeed:
            ++stats_.impossible_speed;
            break;
        case Kind::PositionJump:
            ++stats_.position_jump;
            break;
        case Kind::Gap:
            ++stats_.gap;
            break;
        case Kind::IdentityConflict:
            ++stats_.identity_conflict;
            break;
    }
}

bool Detector::reachable(const Position& from, const track::Fix& f, double& dist_m) const {
    dist_m = track::haversine_m(from.lat, from.lon, f.lat_deg, f.lon_deg);
    const double dt = std::abs(f.t - from.t);  // live feeds can be slightly out of order
    double speed_kn = p_.max_speed_kn;
    if (from.sog && f.sog_knots) {
        // Both ends report a speed: the vessel cannot have averaged much more
        // than the higher of the two in between.
        speed_kn = std::min(speed_kn, std::max(*from.sog, *f.sog_knots) + p_.speed_margin_kn);
    }
    return dist_m <= speed_kn * track::kKnotToMs * dt + p_.slack_m;
}

void Detector::add(const track::Fix& f, std::vector<Anomaly>& out) {
    ++stats_.fixes;
    ++per_minute_[static_cast<std::int64_t>(std::floor(f.t / 60.0))];
    release_gaps(f.t, out);
    if (is_sar_aircraft(f.mmsi)) {
        ++stats_.aircraft;
        return;
    }
    auto raise_at = [&](Kind k, double t, double lat, double lon, double value, double distance_m) {
        out.push_back(Anomaly{k, f.mmsi, t, lat, lon, value, distance_m});
        count(k);
    };
    auto raise = [&](Kind k, double value, double distance_m) {
        raise_at(k, f.t, f.lat_deg, f.lon_deg, value, distance_m);
    };
    auto implied_kn = [](double dist_m, double t1, double t2) {
        // Timestamps have 1 s resolution, so the implied speed uses at least 1 s.
        return dist_m / std::max(std::abs(t2 - t1), 1.0) / track::kKnotToMs;
    };

    if (f.sog_knots && *f.sog_knots > p_.max_speed_kn) raise(Kind::ImpossibleSpeed, *f.sog_knots, 0.0);

    const Position here{f.t, f.lat_deg, f.lon_deg, f.sog_knots, f.t, 1};
    auto [it, inserted] = vessels_.try_emplace(f.mmsi);
    Vessel& v = it->second;
    if (inserted) {
        ++stats_.vessels;
        v.primary = here;
        v.last_t = f.t;
        v.last_sog = f.sog_knots.value_or(0.0);
        v.recent[0] = f.t;
        v.n_recent = 1;
        return;
    }

    // Silence: judged against the last report from either position.
    const double silent = f.t - v.last_t;
    const auto k = static_cast<std::size_t>(p_.gap_min_reports);
    const bool regular = k <= v.recent.size() && v.n_recent >= k && v.recent[0] - v.recent[k - 1] <= p_.gap_s;
    if (silent >= p_.gap_s && v.last_t < p_.listening_since) {
        ++stats_.gaps_before_listening;
    } else if (silent >= p_.gap_s && v.last_sog >= p_.gap_min_sog_kn && !regular) {
        ++stats_.gaps_not_regular;
    } else if (silent >= p_.gap_s && v.last_sog >= p_.gap_min_sog_kn) {
        silence_ends_.push_back(f.t);  // evidence for other vessels' coverage check
        // Only the time the network was up counts as the vessel's silence.
        if (silent - 60.0 * minutes_network_down(v.last_t, f.t) < p_.gap_s) {
            ++stats_.gaps_during_outage;
        } else {
            held_gaps_.push_back(Anomaly{Kind::Gap, f.mmsi, f.t, f.lat_deg, f.lon_deg, silent,
                                         track::haversine_m(v.primary.lat, v.primary.lon, f.lat_deg, f.lon_deg)});
        }
    }

    if (v.second) {
        if (f.t - v.second->t > p_.second_position_s) {
            // The second position went quiet: forget it.
            v.second.reset();
            v.conflict_raised = false;
        } else if (f.t - v.primary.t > p_.second_position_s) {
            // The first position went quiet and the second kept reporting:
            // the vessel really is there (the first position was wrong).
            v.primary = *v.second;
            v.before.reset();
            v.second.reset();
            v.conflict_raised = false;
            ++stats_.relocated;
        }
    }

    auto accept = [&](Position& pos, bool keep_before) {
        if (f.t > pos.t) {  // repeats of one transmission count once
            if (keep_before) v.before = pos;
            ++pos.fixes;
        }
        pos.t = std::max(pos.t, f.t);
        pos.lat = f.lat_deg;
        pos.lon = f.lon_deg;
        if (f.sog_knots) pos.sog = f.sog_knots;
    };

    double d_primary = 0.0;
    double d_second = 0.0;
    double d_before = 0.0;
    if (reachable(v.primary, f, d_primary)) {
        accept(v.primary, true);
    } else if (v.second && reachable(*v.second, f, d_second)) {
        accept(*v.second, false);
        const Position& s = *v.second;
        // Both positions kept reporting: two transmitters, one MMSI.
        if (s.fixes >= static_cast<std::uint32_t>(p_.conflict_min_fixes) && v.primary.t > s.first_t &&
            !v.conflict_raised) {
            raise(Kind::IdentityConflict, d_primary, 0.0);
            v.conflict_raised = true;
        }
    } else if (v.before && reachable(*v.before, f, d_before) &&
               d_before < 0.5 * track::haversine_m(v.before->lat, v.before->lon, v.primary.lat, v.primary.lon)) {
        // Out and back: this report agrees with the one before the latest,
        // so the latest report was the odd one out. Flag that report.
        const Position bad = v.primary;
        const double d_bad = track::haversine_m(v.before->lat, v.before->lon, bad.lat, bad.lon);
        raise_at(Kind::PositionJump, bad.t, bad.lat, bad.lon, implied_kn(d_bad, v.before->t, bad.t), d_bad);
        v.primary = *v.before;
        v.before.reset();
        accept(v.primary, true);
    } else {
        raise(Kind::PositionJump, implied_kn(d_primary, v.primary.t, f.t), d_primary);
        v.second = here;
        v.conflict_raised = false;
    }

    if (f.t > v.last_t) {
        for (std::size_t i = v.recent.size() - 1; i > 0; --i) v.recent[i] = v.recent[i - 1];
        v.recent[0] = f.t;
        v.n_recent = std::min(v.n_recent + 1, v.recent.size());
    }
    if (f.t >= v.last_t) {
        v.last_t = f.t;
        v.last_sog = f.sog_knots.value_or(v.last_sog);
    }
}

void Detector::release_gaps(double now, std::vector<Anomaly>& out) {
    while (!held_gaps_.empty() && held_gaps_.front().t + p_.coverage_hold_s <= now) {
        const Anomaly a = held_gaps_.front();
        held_gaps_.pop_front();
        const auto others = std::count_if(silence_ends_.begin(), silence_ends_.end(),
                                          [&](double t) { return std::abs(t - a.t) <= p_.coverage_hold_s; }) -
                            1;  // this vessel's own
        if (others >= p_.coverage_min_others) {
            ++stats_.gaps_coverage_returned;
        } else {
            out.push_back(a);
            count(Kind::Gap);
        }
    }
    // Silence ends are needed only as far back as the oldest held flag.
    const double keep_from = (held_gaps_.empty() ? now : held_gaps_.front().t) - 2.0 * p_.coverage_hold_s;
    while (!silence_ends_.empty() && silence_ends_.front() < keep_from) silence_ends_.pop_front();
}

void Detector::flush(std::vector<Anomaly>& out) {
    release_gaps(INFINITY, out);
}

int Detector::minutes_network_down(double from, double to) const {
    const auto first = static_cast<std::int64_t>(std::floor(from / 60.0));
    const auto last = static_cast<std::int64_t>(std::floor(to / 60.0));
    if (per_minute_.empty()) return 0;
    const std::int64_t earliest = per_minute_.begin()->first;
    auto count_at = [&](std::int64_t m) -> std::uint32_t {
        const auto it = per_minute_.find(m);
        return it == per_minute_.end() ? 0U : it->second;
    };
    // Usual rate: median over the hour before the silence began (complete minutes only).
    std::vector<std::uint32_t> before;
    for (std::int64_t m = std::max(first - 60, earliest + 1); m < first; ++m) before.push_back(count_at(m));
    if (before.size() < 5) return 0;  // too little history to judge
    std::nth_element(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(before.size() / 2), before.end());
    const double usual = before[before.size() / 2];
    // Minutes wholly inside the silence.
    int down = 0;
    for (std::int64_t m = first + 1; m < last; ++m) {
        if (static_cast<double>(count_at(m)) < p_.outage_fraction * usual) ++down;
    }
    return down;
}

std::size_t Detector::expire(double now, double max_age_s) {
    // Per-minute counts are only needed for an hour before the oldest silence
    // that could still be flagged.
    const auto keep_from = static_cast<std::int64_t>(std::floor((now - max_age_s) / 60.0)) - 60;
    while (!per_minute_.empty() && per_minute_.begin()->first < keep_from) per_minute_.erase(per_minute_.begin());
    const std::size_t n = std::erase_if(vessels_, [&](const auto& kv) { return now - kv.second.last_t > max_age_s; });
    stats_.expired += n;
    return n;
}

}  // namespace maritime::anomaly

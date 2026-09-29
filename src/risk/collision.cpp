#include "maritime/risk/collision.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "maritime/track/geo.hpp"

namespace maritime::risk {

namespace {

constexpr double kDeg = 180.0 / std::numbers::pi;

double wrap360(double deg) {
    const double w = std::fmod(deg, 360.0);
    return w < 0.0 ? w + 360.0 : w;
}

// Bearing of `to` relative to `from`'s course, 0-360 clockwise.
double relative_bearing(const Motion& from, const Motion& to) {
    const track::LocalFrame frame(from.lat_deg, from.lon_deg);
    const auto p = frame.to_local(to.lat_deg, to.lon_deg);
    const double true_bearing = std::atan2(p.east_m, p.north_m) * kDeg;
    return wrap360(true_bearing - course_deg(from));
}

}  // namespace

double speed_kn(const Motion& m) noexcept {
    return std::hypot(m.v_east, m.v_north) / track::kKnotToMs;
}

double course_deg(const Motion& m) noexcept {
    return wrap360(std::atan2(m.v_east, m.v_north) * kDeg);
}

std::string_view name(Encounter e) noexcept {
    switch (e) {
        case Encounter::HeadOn:
            return "head_on";
        case Encounter::Crossing:
            return "crossing";
        case Encounter::Overtaking:
            return "overtaking";
        case Encounter::Unclear:
            return "unclear";
    }
    return "unknown";
}

std::string_view name(Role r) noexcept {
    return r == Role::GiveWay ? "give_way" : "stand_on";
}

Cpa closest_approach(const Motion& a, const Motion& b) {
    // A frame centred between the two, so the result does not depend on which
    // vessel is `a` (and the flat-earth error is halved).
    const track::LocalFrame frame(0.5 * (a.lat_deg + b.lat_deg), 0.5 * (a.lon_deg + b.lon_deg));
    const auto pa = frame.to_local(a.lat_deg, a.lon_deg);
    const auto pb = frame.to_local(b.lat_deg, b.lon_deg);
    const track::EastNorth p{pb.east_m - pa.east_m, pb.north_m - pa.north_m};  // b relative to a
    const double vx = b.v_east - a.v_east;
    const double vy = b.v_north - a.v_north;
    Cpa c;
    c.range_m = std::hypot(p.east_m, p.north_m);
    const double vv = vx * vx + vy * vy;
    if (vv < 1e-9) {  // same velocity: the range never changes
        c.tcpa_s = 0.0;
        c.dcpa_m = c.range_m;
        return c;
    }
    c.tcpa_s = -(p.east_m * vx + p.north_m * vy) / vv;
    if (c.tcpa_s <= 0.0) {
        c.dcpa_m = c.range_m;  // already opening
    } else {
        c.dcpa_m = std::hypot(p.east_m + vx * c.tcpa_s, p.north_m + vy * c.tcpa_s);
    }
    return c;
}

Assessment classify(const Motion& a, const Motion& b, const Params& p) {
    Assessment r;
    r.cpa = closest_approach(a, b);
    r.bearing_of_b_from_a = relative_bearing(a, b);
    r.bearing_of_a_from_b = relative_bearing(b, a);
    const double ra = r.bearing_of_b_from_a;
    const double rb = r.bearing_of_a_from_b;

    const double stern_lo = 90.0 + p.abaft_beam_deg;   // 112.5
    const double stern_hi = 270.0 - p.abaft_beam_deg;  // 247.5
    const auto astern = [&](double rel) { return rel > stern_lo && rel < stern_hi; };
    const auto ahead = [&](double rel) { return rel <= p.head_on_bearing_deg || rel >= 360.0 - p.head_on_bearing_deg; };
    const auto starboard = [&](double rel) { return rel > 0.0 && rel <= stern_lo; };

    // Rule 13 first: it applies "notwithstanding" Rules 14 and 15.
    if (astern(ra) != astern(rb)) {
        r.type = Encounter::Overtaking;
        const bool b_overtakes = astern(ra);  // b is coming up from a's stern sector
        r.role_a = b_overtakes ? Role::StandOn : Role::GiveWay;
        r.role_b = b_overtakes ? Role::GiveWay : Role::StandOn;
        return r;
    }
    if (astern(ra) && astern(rb)) {  // each is astern of the other: moving apart
        r.type = Encounter::Unclear;
        r.role_a = r.role_b = Role::GiveWay;
        return r;
    }
    // Rule 14: nearly reciprocal courses, each nearly ahead of the other.
    const double between = std::abs(wrap360(course_deg(a) - course_deg(b) + 180.0) - 180.0);  // 0-180
    if (between >= 180.0 - p.head_on_course_deg && ahead(ra) && ahead(rb)) {
        r.type = Encounter::HeadOn;
        r.role_a = r.role_b = Role::GiveWay;
        return r;
    }
    // Rule 15: the vessel with the other on her starboard side gives way.
    if (starboard(ra) != starboard(rb)) {
        r.type = Encounter::Crossing;
        r.role_a = starboard(ra) ? Role::GiveWay : Role::StandOn;
        r.role_b = starboard(rb) ? Role::GiveWay : Role::StandOn;
        return r;
    }
    // Both see the other on the same side: no rule assigns the roles, and in
    // doubt both are expected to act (Rules 7 and 8).
    r.type = Encounter::Unclear;
    r.role_a = r.role_b = Role::GiveWay;
    return r;
}

std::optional<Assessment> assess(const Motion& a, const Motion& b, const Params& p) {
    if (speed_kn(a) < p.min_speed_kn || speed_kn(b) < p.min_speed_kn) return std::nullopt;
    const Cpa c = closest_approach(a, b);
    if (c.range_m > p.max_range_m || c.tcpa_s <= 0.0 || c.tcpa_s > p.max_tcpa_s || c.dcpa_m > p.max_dcpa_m) {
        return std::nullopt;
    }
    return classify(a, b, p);
}

std::vector<PairAssessment> find_encounters(std::vector<Motion> vessels, const Params& p) {
    // Only vessels under way can be at risk; then only pairs within range.
    std::erase_if(vessels, [&](const Motion& m) { return speed_kn(m) < p.min_speed_kn; });
    std::vector<spatial::Point> points(vessels.size());
    std::transform(vessels.begin(), vessels.end(), points.begin(),
                   [](const Motion& m) { return spatial::Point{m.lat_deg, m.lon_deg}; });
    // assess() measures range in a flat local frame, which can differ from the
    // great-circle distance by a few centimetres at 6 nm: ask the index for a
    // slightly wider radius and let assess() decide.
    const auto candidates = spatial::pairs_within(points, p.max_range_m * 1.001 + 1.0, p.index);
    std::vector<PairAssessment> out;
    for (const auto& [i, j] : candidates) {
        const Motion& x = vessels[i];
        const Motion& y = vessels[j];
        if (x.mmsi == y.mmsi) continue;
        const bool x_first = x.mmsi < y.mmsi;
        const Motion& a = x_first ? x : y;
        const Motion& b = x_first ? y : x;
        if (auto r = assess(a, b, p)) out.push_back({a.mmsi, b.mmsi, *r});
    }
    std::sort(out.begin(), out.end(), [](const PairAssessment& l, const PairAssessment& r) {
        return l.mmsi_a != r.mmsi_a ? l.mmsi_a < r.mmsi_a : l.mmsi_b < r.mmsi_b;
    });
    return out;
}

}  // namespace maritime::risk

// Collision risk between pairs of vessels: closest point of approach and the
// encounter type under the COLREGs steering rules for vessels in sight of one
// another (Rule 13 overtaking, Rule 14 head-on, Rule 15 crossing), with which
// vessel has to keep out of the way.
//
// Motion is straight-line (constant course and speed), the standard basis for
// CPA/TCPA on a radar or ECDIS. Course is course over ground; COLREGs judge
// sectors by heading, which AIS gives separately and can differ by a few
// degrees in wind or current.
//
// Out of scope: Rule 18 (a power-driven vessel keeps out of the way of vessels
// fishing, sailing, not under command or restricted in their ability to
// manoeuvre), Rule 9 (narrow channels) and Rule 10 (traffic separation
// schemes). Those change who gives way; this module reports the geometry.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace maritime::risk {

inline constexpr double kNauticalMileM = 1852.0;

struct Motion {
    std::uint32_t mmsi = 0;
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    double v_east = 0.0;  // m/s
    double v_north = 0.0;
};

[[nodiscard]] double speed_kn(const Motion& m) noexcept;
[[nodiscard]] double course_deg(const Motion& m) noexcept;  // 0-360, clockwise from north

struct Cpa {
    double range_m = 0.0;  // distance now
    double tcpa_s = 0.0;   // time to closest approach; negative if it is already past
    double dcpa_m = 0.0;   // distance at closest approach (the current range if past)
};

[[nodiscard]] Cpa closest_approach(const Motion& a, const Motion& b);

enum class Encounter : std::uint8_t {
    HeadOn,      // Rule 14: both alter course to starboard
    Crossing,    // Rule 15: the vessel with the other on her starboard side keeps out of the way
    Overtaking,  // Rule 13: the overtaking vessel keeps out of the way
    Unclear,     // the geometry fits none of the rules cleanly (see classify)
};

enum class Role : std::uint8_t { GiveWay, StandOn };

[[nodiscard]] std::string_view name(Encounter e) noexcept;
[[nodiscard]] std::string_view name(Role r) noexcept;

struct Params {
    double max_range_m = 6.0 * kNauticalMileM;  // only pairs this close are considered
    double max_tcpa_s = 20.0 * 60.0;            // ... closing within this time
    double max_dcpa_m = 0.5 * kNauticalMileM;   // ... to within this distance
    double min_speed_kn = 2.0;                  // both vessels under way
    // Rule 13: "more than 22.5 degrees abaft her beam", i.e. relative bearing
    // between 112.5 and 247.5 degrees.
    double abaft_beam_deg = 22.5;
    // Rule 14: "reciprocal or nearly reciprocal courses". Courses within this
    // many degrees of reciprocal, and each vessel within this many degrees of
    // right ahead of the other.
    double head_on_course_deg = 10.0;
    double head_on_bearing_deg = 22.5;
};

struct Assessment {
    Encounter type = Encounter::Unclear;
    Role role_a = Role::StandOn;
    Role role_b = Role::StandOn;
    Cpa cpa;
    double bearing_of_b_from_a = 0.0;  // relative to a's course, 0-360, clockwise
    double bearing_of_a_from_b = 0.0;
};

// Encounter type and roles from geometry alone, whether or not there is risk.
[[nodiscard]] Assessment classify(const Motion& a, const Motion& b, const Params& p = {});

// classify(), but only if the pair is at risk under `p` (both under way, in
// range, closing to within max_dcpa_m within max_tcpa_s).
[[nodiscard]] std::optional<Assessment> assess(const Motion& a, const Motion& b, const Params& p = {});

struct PairAssessment {
    std::uint32_t mmsi_a = 0;  // mmsi_a < mmsi_b
    std::uint32_t mmsi_b = 0;
    Assessment assessment;
};

// Every pair at risk among `vessels` (one entry per MMSI, all at the same
// moment). A sweep over latitude limits the pairs examined to those within
// max_range_m north-south of each other.
[[nodiscard]] std::vector<PairAssessment> find_encounters(std::vector<Motion> vessels, const Params& p = {});

}  // namespace maritime::risk

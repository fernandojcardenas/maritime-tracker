// Anomaly detection on a stream of position reports. Every rule is a
// physical limit that an operator can check by hand, not a statistical
// score: a flag always says which limit was broken and by how much.
//
//   ImpossibleSpeed   the vessel reported a speed over ground above max_speed_kn
//   PositionJump      the report is farther from the vessel's last position than
//                     it could have travelled at max_speed_kn (plus slack_m)
//   Gap               a moving vessel was silent for gap_s or longer while the
//                     receiver network was working (minutes in which the whole
//                     network went quiet do not count)
//   IdentityConflict  one MMSI keeps reporting from two places that are
//                     mutually unreachable: two transmitters share an identity
//
// Search-and-rescue aircraft (MMSI 111MIDxxx, ITU-R M.585) fly at 100+ kn and
// are not checked.
//
// Reports should arrive roughly in time order (replay sorts them; live feeds
// are nearly ordered). Memory is one small record per vessel; call expire()
// periodically to drop vessels not heard for a long time.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "maritime/track/tracker.hpp"

namespace maritime::anomaly {

enum class Kind : std::uint8_t { ImpossibleSpeed, PositionJump, Gap, IdentityConflict };

[[nodiscard]] std::string_view name(Kind k) noexcept;

// True for search-and-rescue aircraft identities (111MIDxxx).
[[nodiscard]] constexpr bool is_sar_aircraft(std::uint32_t mmsi) noexcept {
    return mmsi / 1000000U == 111U;
}

struct Anomaly {
    Kind kind = Kind::PositionJump;
    std::uint32_t mmsi = 0;
    double t = 0.0;        // time of the report that raised the flag
    double lat_deg = 0.0;  // position of that report
    double lon_deg = 0.0;
    // ImpossibleSpeed: reported speed (kn). PositionJump: implied speed (kn).
    // Gap: silence (s). IdentityConflict: distance between the two positions (m).
    double value = 0.0;
    // PositionJump and Gap: distance from the previous position (m).
    double distance_m = 0.0;
};

struct Params {
    double max_speed_kn = 50.0;     // faster than any cargo ship, tanker or ferry, and most high-speed craft
    double speed_margin_kn = 15.0;  // when both reports give a speed, allow this much above the higher one
    double slack_m = 500.0;         // GPS error and timestamp rounding
    double gap_s = 600.0;           // a moving Class A vessel reports every 2-10 s, Class B every 30 s
    double gap_min_sog_kn = 2.0;    // silence only counts if the vessel was moving when it went quiet
    int gap_min_reports = 3;        // ... and had been heard at least this often in the gap_s before it:
                                    // vessels seen only by satellite or at the edge of coverage are
                                    // silent for long stretches as a matter of course
    // A minute in which the whole network delivered less than this fraction of
    // its usual reports counts as an outage: the receivers were down, not the
    // vessel's transponder. Outage minutes do not count towards gap_s.
    double outage_fraction = 0.25;
    // When at least coverage_min_others other vessels that were moving and
    // heard regularly also end a silence of gap_s or more within
    // coverage_hold_s of this one, coverage came back
    // (a satellite pass, a receiver restart): not flagged. Gap flags are held
    // back for coverage_hold_s so the others can arrive.
    double coverage_hold_s = 120.0;
    // Silences that began before this time are not judged: a live stream
    // opens with each vessel's last known position, which may be hours old.
    double listening_since = -1e300;
    int coverage_min_others = 3;
    int conflict_min_fixes = 3;        // reports from the second position before an identity conflict is raised
    double second_position_s = 600.0;  // a second position not heard from for this long is forgotten
};

struct Stats {
    std::uint64_t fixes = 0;
    std::uint64_t vessels = 0;
    std::uint64_t impossible_speed = 0;
    std::uint64_t position_jump = 0;
    std::uint64_t gap = 0;
    std::uint64_t identity_conflict = 0;
    std::uint64_t relocated = 0;               // a second position took over because the first went silent
    std::uint64_t gaps_not_regular = 0;        // silences not flagged: the vessel was not being heard regularly
    std::uint64_t aircraft = 0;                // reports from SAR aircraft (MMSI 111MIDxxx), not checked
    std::uint64_t gaps_before_listening = 0;   // silences that began before listening_since
    std::uint64_t gaps_coverage_returned = 0;  // silences not flagged: many vessels reappeared together
    std::uint64_t gaps_during_outage = 0;      // silences not flagged: too short once network outages are taken out
    std::uint64_t expired = 0;
};

class Detector {
public:
    explicit Detector(Params p = {}) : p_(p) {}

    // Processes one report; appends any anomalies it raises to `out`. Gap
    // flags are appended coverage_hold_s after the report that ends the gap.
    void add(const track::Fix& f, std::vector<Anomaly>& out);

    // End of input: decides and appends any gap flags still held back.
    void flush(std::vector<Anomaly>& out);

    // Forgets vessels not heard from since now - max_age_s. Returns how many.
    std::size_t expire(double now, double max_age_s);

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    [[nodiscard]] const Params& params() const noexcept { return p_; }

private:
    // Where a transmitter using this MMSI was last heard.
    struct Position {
        double t = 0.0;
        double lat = 0.0;
        double lon = 0.0;
        std::optional<double> sog;  // last reported speed over ground (kn)
        double first_t = 0.0;
        std::uint32_t fixes = 0;
    };
    struct Vessel {
        Position primary;
        std::optional<Position> before;  // the primary as it was one report earlier
        std::optional<Position> second;
        double last_t = 0.0;
        double last_sog = 0.0;
        std::array<double, 4> recent{};  // times of the last distinct reports, newest first
        std::size_t n_recent = 0;
        bool conflict_raised = false;
    };

    void release_gaps(double now, std::vector<Anomaly>& out);
    [[nodiscard]] bool reachable(const Position& from, const track::Fix& f, double& dist_m) const;
    void count(Kind k);
    // Whole minutes in (from, to) in which the network was down.
    [[nodiscard]] int minutes_network_down(double from, double to) const;

    Params p_;
    std::unordered_map<std::uint32_t, Vessel> vessels_;
    std::map<std::int64_t, std::uint32_t> per_minute_;  // reports per minute, whole network
    std::deque<Anomaly> held_gaps_;                     // gap flags waiting for coverage_hold_s
    std::deque<double> silence_ends_;                   // times at which a gap candidate ended its silence
    Stats stats_;
};

}  // namespace maritime::anomaly

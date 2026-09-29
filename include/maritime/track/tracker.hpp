// Multi-vessel tracker. AIS reports carry the vessel's identity (MMSI), so
// association is by MMSI; the hard part is deciding which reports to trust.
// Each track runs a KalmanCV filter. A report whose position is statistically
// implausible given the track (gating on the normalised innovation squared)
// is rejected and counted; several rejections in a row mean the track itself
// is wrong (or the vessel really jumped), so the track is restarted. Tracks
// with no report for `stale_after` are dropped.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "maritime/ais/decoder.hpp"
#include "maritime/track/geo.hpp"
#include "maritime/track/kalman.hpp"

namespace maritime::track {

// One position report, from any source.
struct Fix {
    std::uint32_t mmsi = 0;
    double t = 0.0;  // seconds since the Unix epoch
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    std::optional<double> sog_knots;
    std::optional<double> cog_deg;
    double pos_sigma_m = 15.0;
};

// Builds a Fix from a decoded position report (types 1-3, 18, 19). Needs a
// receive time from the tag block; returns nullopt otherwise or when the
// position is not available. `high_accuracy` reports get a 10 m sigma, others 25 m.
[[nodiscard]] std::optional<Fix> fix_from(const ais::DecodedMessage& dm);

// Parses one data row of the Danish Maritime Authority historical AIS CSV
// ("22/04/2026 12:00:00,Class A,219001259,56.09,12.47,...,SOG,COG,..."), as
// produced by tools/fetch_dk_slice.sh (columns: time, class, MMSI, lat, lon,
// SOG, COG, heading, ship type). Returns nullopt for malformed rows.
[[nodiscard]] std::optional<Fix> fix_from_dk_csv(std::string_view row);

struct TrackerParams {
    KalmanParams kalman;
    double gate_nis = 18.42;       // chi-square, 2 dof, p = 1e-4
    int restart_after_rejects = 3;
    double stale_after_s = 1800.0;
    double reanchor_m = 20000.0;   // move the local frame when the track drifts this far
    double min_dt_s = 0.5;         // reports closer than this to the last update are duplicates
};

enum class FixOutcome : std::uint8_t {
    Started,    // first report of a new track
    Updated,
    Duplicate,  // same time (within min_dt_s) as the last update
    OutOfOrder, // older than the last update
    Rejected,   // failed the gate
    Restarted,  // gate failed restart_after_rejects times in a row
};

struct Prediction {
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    double sigma_m = 0.0;  // sqrt of the mean of the position variances
};

struct TrackerStats {
    std::uint64_t fixes = 0;
    std::uint64_t started = 0;
    std::uint64_t updated = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t out_of_order = 0;
    std::uint64_t rejected = 0;
    std::uint64_t restarted = 0;
    std::uint64_t expired = 0;
    std::uint64_t reanchored = 0;
    std::uint64_t numeric_resets = 0;
};

struct TrackView {
    std::uint32_t mmsi = 0;
    double t = 0.0;  // time of the last update
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    double v_east = 0.0;
    double v_north = 0.0;
    std::uint64_t updates = 0;
};

class Tracker {
public:
    explicit Tracker(TrackerParams params = {}) : params_(params) {}

    // Where the track for `mmsi` expects the vessel at time t, before any
    // report at t is applied. nullopt if there is no track.
    [[nodiscard]] std::optional<Prediction> predict(std::uint32_t mmsi, double t) const;

    FixOutcome add(const Fix& fix);

    // Drops tracks not updated since now - stale_after_s. Returns how many.
    std::size_t expire(double now);

    [[nodiscard]] std::optional<TrackView> track(std::uint32_t mmsi) const;
    [[nodiscard]] std::vector<TrackView> tracks() const;
    [[nodiscard]] std::size_t size() const noexcept { return tracks_.size(); }
    [[nodiscard]] const TrackerStats& stats() const noexcept { return stats_; }

private:
    struct Track {
        LocalFrame frame;
        KalmanCV kf;
        double t;
        int consecutive_rejects = 0;
        std::uint64_t updates = 0;
    };

    [[nodiscard]] static Measurement measurement(const Track& tr, const Fix& f);
    Track start(const Fix& f) const;
    [[nodiscard]] static TrackView view(std::uint32_t mmsi, const Track& tr);

    TrackerParams params_;
    std::unordered_map<std::uint32_t, Track> tracks_;
    TrackerStats stats_;
};

}  // namespace maritime::track

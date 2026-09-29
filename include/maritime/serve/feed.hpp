// The live map's data: turns tracker state, anomaly flags and collision-risk
// encounters into two JSON messages each second.
//
//   snapshot  everything a newly connected browser needs: every track, the
//             most recent anomalies, the encounters at risk now, and totals
//   update    what changed since the previous tick: tracks updated or
//             expired, new anomalies, the encounters at risk now, and totals
//
// Track rows are arrays, [mmsi, lat, lon, sog_kn, cog_deg, t], to keep the
// messages small; see docs/live-map.md for the full format.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "maritime/anomaly/detector.hpp"
#include "maritime/risk/collision.hpp"
#include "maritime/track/tracker.hpp"

namespace maritime::serve {

struct FeedTotals {
    std::uint64_t messages = 0;
    std::uint64_t vessels = 0;
    std::uint64_t anomalies = 0;
    std::uint64_t encounters = 0;
    std::uint64_t dropped = 0;  // lines the ingest queue dropped
};

class LiveFeed {
public:
    static constexpr std::size_t kRecentAnomalies = 200;

    void add_anomaly(const anomaly::Anomaly& a);
    void set_encounters(std::vector<risk::PairAssessment> now);

    struct Messages {
        std::string update;
        std::string snapshot;
    };
    // Builds this tick's messages. `data_time` is the newest report time seen.
    [[nodiscard]] Messages tick(const track::Tracker& tracker, double data_time, const FeedTotals& totals);

private:
    std::unordered_map<std::uint32_t, double> sent_;  // mmsi -> track time last sent
    std::deque<anomaly::Anomaly> recent_;
    std::vector<anomaly::Anomaly> new_anomalies_;
    std::vector<risk::PairAssessment> encounters_;
};

}  // namespace maritime::serve

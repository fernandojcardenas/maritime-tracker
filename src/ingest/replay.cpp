#include "maritime/ingest/replay.hpp"

#include <string>
#include <thread>

#include "maritime/nmea/sentence.hpp"

namespace maritime::ingest {

void SteadyClock::sleep_until(time_point t) { std::this_thread::sleep_until(t); }

std::optional<std::int64_t> tag_block_time(std::string_view line) {
    if (line.empty() || line.front() != '\\') return std::nullopt;
    const auto close = line.find('\\', 1);
    if (close == std::string_view::npos) return std::nullopt;
    const auto tag = nmea::parse_tag_block(line.substr(1, close - 1));
    if (!tag) return std::nullopt;
    return tag->unix_time;
}

ReplayStats replay(std::istream& in, const ReplayOptions& options, Clock& clock,
                   const std::function<void(std::string_view)>& on_line, const std::atomic<bool>& stop) {
    using namespace std::chrono;
    ReplayStats stats;
    const bool paced = options.speed > 0.0;

    // Recorded time is mapped onto wall time as:
    //   wall = start_wall + (recorded - first - skipped) / speed
    bool have_first = false;
    std::int64_t first_ts = 0;
    std::int64_t last_ts = 0;
    std::int64_t skipped_s = 0;  // recorded seconds removed by gap shortening
    Clock::time_point start_wall{};

    std::string line;
    while (!stop.load(std::memory_order_relaxed) && std::getline(in, line)) {
        ++stats.lines;
        if (paced) {
            if (const auto ts = tag_block_time(line)) {
                const std::int64_t t = ts.value();
                ++stats.timestamped;
                if (!have_first) {
                    have_first = true;
                    first_ts = last_ts = t;
                    start_wall = clock.now();
                } else if (t < last_ts) {
                    ++stats.backwards_timestamps;  // emit now, don't go back in time
                } else {
                    if (t - last_ts > options.max_gap.count()) {
                        skipped_s += (t - last_ts) - options.max_gap.count();
                        ++stats.gaps_shortened;
                    }
                    last_ts = t;
                }
                const double offset_s = static_cast<double>(last_ts - first_ts - skipped_s) / options.speed;
                clock.sleep_until(start_wall + duration_cast<Clock::time_point::duration>(duration<double>(offset_s)));
            }
        }
        on_line(line);
    }
    return stats;
}

}  // namespace maritime::ingest

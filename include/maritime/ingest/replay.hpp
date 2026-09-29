// Replays a recorded file, pacing lines by their timestamps so recorded
// traffic arrives as it did live, optionally sped up. NMEA lines are timed by
// their tag block (the "c:" field); other formats pass ReplayOptions::time_of.
// Time is injected through Clock so tests run instantly.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <istream>
#include <optional>
#include <string_view>

namespace maritime::ingest {

class Clock {
public:
    using time_point = std::chrono::steady_clock::time_point;
    virtual ~Clock() = default;
    [[nodiscard]] virtual time_point now() const = 0;
    virtual void sleep_until(time_point t) = 0;
};

class SteadyClock final : public Clock {
public:
    [[nodiscard]] time_point now() const override { return std::chrono::steady_clock::now(); }
    void sleep_until(time_point t) override;
};

struct ReplayOptions {
    // 1.0 = real time, 60.0 = one hour per minute. 0 = no pacing at all.
    double speed = 1.0;
    // Gaps in the recording longer than this (in recorded time) are
    // shortened to it, so a receiver outage doesn't stall the replay.
    std::chrono::seconds max_gap{60};
    // Where a line's time comes from. Empty: the NMEA tag block (tag_block_time).
    std::function<std::optional<std::int64_t>(std::string_view)> time_of;
};

struct ReplayStats {
    std::uint64_t lines = 0;
    std::uint64_t timestamped = 0;
    std::uint64_t gaps_shortened = 0;
    std::uint64_t backwards_timestamps = 0;  // earlier than the previous line
};

// Reads the unix time from a line's tag block without parsing the sentence.
// Returns nullopt when there is no tag block, no "c:" field or a bad checksum.
[[nodiscard]] std::optional<std::int64_t> tag_block_time(std::string_view line);

// Calls on_line for every line of `in` until EOF or until `stop` is set.
ReplayStats replay(std::istream& in, const ReplayOptions& options, Clock& clock,
                   const std::function<void(std::string_view)>& on_line, const std::atomic<bool>& stop);

}  // namespace maritime::ingest

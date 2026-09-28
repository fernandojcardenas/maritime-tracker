// Records raw lines to hourly files named ais-YYYYMMDD-HH.nmea (UTC), so a
// live session can be replayed later. Lines that arrive without a tag-block
// timestamp get one ("\c:<unix>*hh\"), which is what replay() paces on.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace maritime::ingest {

struct RecorderStats {
    std::uint64_t lines = 0;
    std::uint64_t timestamps_added = 0;
    std::uint64_t files_opened = 0;
    std::uint64_t write_errors = 0;
};

class Recorder {
public:
    explicit Recorder(std::filesystem::path dir);

    // Appends `line` received at `unix_time` (seconds, UTC).
    void write(std::string_view line, std::int64_t unix_time);
    void flush();

    [[nodiscard]] const RecorderStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const std::filesystem::path& current_file() const noexcept { return current_; }

    // "ais-YYYYMMDD-HH.nmea" for the UTC hour containing unix_time.
    [[nodiscard]] static std::string file_name_for(std::int64_t unix_time);
    // Returns `line` with a "\c:<unix>*hh\" tag block prepended, unless it
    // already has a tag block.
    [[nodiscard]] static std::string with_timestamp(std::string_view line, std::int64_t unix_time);

private:
    std::filesystem::path dir_;
    std::filesystem::path current_;
    std::ofstream out_;
    RecorderStats stats_;
};

}  // namespace maritime::ingest

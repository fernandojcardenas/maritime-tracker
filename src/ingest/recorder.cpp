#include "maritime/ingest/recorder.hpp"

#include <array>
#include <cstdio>
#include <ctime>

#include "maritime/nmea/sentence.hpp"

namespace maritime::ingest {

Recorder::Recorder(std::filesystem::path dir) : dir_(std::move(dir)) { std::filesystem::create_directories(dir_); }

std::string Recorder::file_name_for(std::int64_t unix_time) {
    const auto t = static_cast<std::time_t>(unix_time);
    std::tm utc{};
    gmtime_r(&t, &utc);
    std::array<char, 32> buf{};
    const auto n = std::strftime(buf.data(), buf.size(), "ais-%Y%m%d-%H.nmea", &utc);
    return {buf.data(), n};
}

std::string Recorder::with_timestamp(std::string_view line, std::int64_t unix_time) {
    if (!line.empty() && line.front() == '\\') return std::string(line);
    const std::string body = "c:" + std::to_string(unix_time);
    std::array<char, 4> hex{};
    if (std::snprintf(hex.data(), hex.size(), "%02X", static_cast<unsigned>(nmea::checksum(body))) != 2) {
        return std::string(line);  // cannot happen for a one-byte checksum
    }
    std::string out;
    out.reserve(body.size() + line.size() + 6);
    out += '\\';
    out += body;
    out += '*';
    out += hex.data();
    out += '\\';
    out += line;
    return out;
}

void Recorder::write(std::string_view line, std::int64_t unix_time) {
    const auto path = dir_ / file_name_for(unix_time);
    if (path != current_ || !out_.is_open()) {
        if (out_.is_open()) out_.close();
        out_.open(path, std::ios::app);
        current_ = path;
        ++stats_.files_opened;
    }
    const bool add_ts = line.empty() || line.front() != '\\';
    if (add_ts) ++stats_.timestamps_added;
    out_ << with_timestamp(line, unix_time) << '\n';
    if (!out_) {
        ++stats_.write_errors;
        out_.clear();
        return;
    }
    ++stats_.lines;
}

void Recorder::flush() {
    if (out_.is_open()) out_.flush();
}

}  // namespace maritime::ingest

// Splits a byte stream (TCP reads arrive in arbitrary chunks) into lines.
// Lines longer than max_line_length are discarded, so a peer that never
// sends a newline cannot grow memory without bound.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace maritime::ingest {

struct FramerStats {
    std::uint64_t lines = 0;
    std::uint64_t oversize_dropped = 0;
    std::uint64_t bytes = 0;
};

class LineFramer {
public:
    // NMEA 0183 limits a sentence to 82 characters; a tag block adds more.
    // 1024 leaves generous room while still bounding memory.
    explicit LineFramer(std::size_t max_line_length = 1024) : max_(max_line_length) {}

    // Feeds a chunk and calls on_line(std::string_view) for each complete,
    // non-empty line. Trailing '\r' is removed.
    template <class OnLine>
    void feed(std::string_view chunk, OnLine&& on_line) {
        stats_.bytes += chunk.size();
        for (const char c : chunk) {
            if (c == '\n') {
                if (!discarding_) {
                    if (!buf_.empty() && buf_.back() == '\r') buf_.pop_back();
                    if (!buf_.empty()) {
                        ++stats_.lines;
                        on_line(std::string_view(buf_));
                    }
                }
                buf_.clear();
                discarding_ = false;
            } else if (!discarding_) {
                if (buf_.size() >= max_) {
                    ++stats_.oversize_dropped;
                    buf_.clear();
                    discarding_ = true;  // skip the rest of this line
                } else {
                    buf_.push_back(c);
                }
            }
        }
    }

    // Discards any partial line, e.g. after a reconnect.
    void reset() {
        buf_.clear();
        discarding_ = false;
    }

    [[nodiscard]] const FramerStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t buffered() const noexcept { return buf_.size(); }

private:
    std::size_t max_;
    std::string buf_;
    bool discarding_ = false;
    FramerStats stats_;
};

}  // namespace maritime::ingest

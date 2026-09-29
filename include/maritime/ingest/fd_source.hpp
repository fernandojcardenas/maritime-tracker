// Reads lines from a file descriptor (for example stdin fed by `curl`) with
// the same line framing and limits as the TCP source. Polls, so a stop
// request is honoured within 100 ms even if the writer goes silent.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string_view>

#include "maritime/ingest/line_framer.hpp"

namespace maritime::ingest {

// Returns at end of input, on a read error, or when `stop` becomes true.
FramerStats read_lines(int fd, const std::atomic<bool>& stop, const std::function<void(std::string_view)>& on_line,
                       std::size_t max_line_length = std::size_t{64} * 1024);

}  // namespace maritime::ingest

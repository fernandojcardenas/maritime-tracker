#include "maritime/ingest/fd_source.hpp"

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>

namespace maritime::ingest {

FramerStats read_lines(int fd, const std::atomic<bool>& stop, const std::function<void(std::string_view)>& on_line,
                       std::size_t max_line_length) {
    LineFramer framer(max_line_length);
    std::array<char, std::size_t{64} * 1024> buf{};
    while (!stop.load()) {
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, 100);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;
        const auto n = ::read(fd, buf.data(), buf.size());
        if (n == 0) break;  // end of input
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        framer.feed(std::string_view(buf.data(), static_cast<std::size_t>(n)), on_line);
    }
    // A final line without a trailing newline still counts.
    framer.feed("\n", on_line);
    return framer.stats();
}

}  // namespace maritime::ingest

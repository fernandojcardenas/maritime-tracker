#include "maritime/ingest/tcp_source.hpp"

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <random>
#include <thread>
#include <utility>

#include "maritime/ingest/line_framer.hpp"

namespace maritime::ingest {

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr milliseconds kStopCheck{100};

class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket() {
        if (fd_ >= 0) ::close(fd_);
    }
    [[nodiscard]] int get() const { return fd_; }
    int release() { return std::exchange(fd_, -1); }

private:
    int fd_;
};

class AddrInfo {
public:
    AddrInfo(const std::string& host, const std::string& port) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &list_) != 0) list_ = nullptr;
    }
    AddrInfo(const AddrInfo&) = delete;
    AddrInfo& operator=(const AddrInfo&) = delete;
    ~AddrInfo() {
        if (list_ != nullptr) ::freeaddrinfo(list_);
    }
    [[nodiscard]] const addrinfo* get() const { return list_; }

private:
    addrinfo* list_ = nullptr;
};

// Sleeps for `d` in short slices so a stop request is noticed quickly.
void sleep_unless_stopped(milliseconds d, const std::atomic<bool>& stop) {
    const auto until = Clock::now() + d;
    while (!stop.load() && Clock::now() < until) {
        std::this_thread::sleep_for(std::min(kStopCheck, std::chrono::duration_cast<milliseconds>(until - Clock::now())));
    }
}

int poll_ms(milliseconds d) { return static_cast<int>(std::max<milliseconds::rep>(0, d.count())); }

}  // namespace

TcpStats TcpSource::stats() const {
    return TcpStats{c_.connect_attempts.load(), c_.connects.load(),    c_.connect_failures.load(),
                    c_.peer_closes.load(),      c_.read_errors.load(), c_.idle_timeouts.load(),
                    c_.bytes.load(),            c_.lines.load(),       c_.oversize_dropped.load()};
}

int TcpSource::connect_once(const std::atomic<bool>& stop) {
    ++c_.connect_attempts;
    const AddrInfo addrs(options_.host, options_.port);
    for (const addrinfo* a = addrs.get(); a != nullptr && !stop.load(); a = a->ai_next) {
        Socket s(::socket(a->ai_family, a->ai_socktype, a->ai_protocol));
        if (s.get() < 0) continue;
        const int flags = ::fcntl(s.get(), F_GETFL, 0);
        if (flags < 0 || ::fcntl(s.get(), F_SETFL, flags | O_NONBLOCK) < 0) continue;

        if (::connect(s.get(), a->ai_addr, a->ai_addrlen) == 0) return s.release();
        if (errno != EINPROGRESS) continue;

        // Wait for the non-blocking connect, in slices so stop is honoured.
        const auto deadline = Clock::now() + options_.connect_timeout;
        bool connected = false;
        while (!stop.load() && Clock::now() < deadline) {
            pollfd p{s.get(), POLLOUT, 0};
            const auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now());
            const int r = ::poll(&p, 1, poll_ms(std::min(left, kStopCheck)));
            if (r < 0 && errno != EINTR) break;
            if (r > 0) {
                int err = 0;
                socklen_t len = sizeof err;
                connected = ::getsockopt(s.get(), SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
                break;
            }
        }
        if (connected) return s.release();
    }
    ++c_.connect_failures;
    return -1;
}

bool TcpSource::read_until_closed(int fd, const std::atomic<bool>& stop,
                                  const std::function<void(std::string_view)>& on_line) {
    LineFramer framer(options_.max_line_length);
    std::array<char, std::size_t{64} * 1024> buf{};
    bool got_data = false;
    auto last_data = Clock::now();
    std::uint64_t oversize_seen = 0;

    while (!stop.load()) {
        if (Clock::now() - last_data > options_.idle_timeout) {
            ++c_.idle_timeouts;
            break;
        }
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, poll_ms(kStopCheck));
        if (r < 0) {
            if (errno == EINTR) continue;
            ++c_.read_errors;
            break;
        }
        if (r == 0) continue;

        const auto n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n == 0) {
            ++c_.peer_closes;
            break;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            ++c_.read_errors;
            break;
        }
        got_data = true;
        last_data = Clock::now();
        c_.bytes += static_cast<std::uint64_t>(n);
        framer.feed(std::string_view(buf.data(), static_cast<std::size_t>(n)), [&](std::string_view line) {
            ++c_.lines;
            on_line(line);
        });
        c_.oversize_dropped += framer.stats().oversize_dropped - oversize_seen;
        oversize_seen = framer.stats().oversize_dropped;
    }
    return got_data;
}

void TcpSource::run(const std::atomic<bool>& stop, const std::function<void(std::string_view)>& on_line) {
    std::minstd_rand rng(std::random_device{}());
    auto backoff = options_.backoff_initial;

    while (!stop.load()) {
        const int fd = connect_once(stop);
        if (fd >= 0) {
            ++c_.connects;
            const Socket s(fd);
            // A connection that delivered data resets the backoff.
            if (read_until_closed(s.get(), stop, on_line)) backoff = options_.backoff_initial;
        }
        if (stop.load()) break;

        // Random wait between half and all of the current backoff, so many
        // clients restarting at once don't reconnect in lockstep.
        std::uniform_int_distribution<milliseconds::rep> dist(backoff.count() / 2, backoff.count());
        sleep_unless_stopped(milliseconds(dist(rng)), stop);
        backoff = std::min(backoff * 2, options_.backoff_max);
    }
}

}  // namespace maritime::ingest

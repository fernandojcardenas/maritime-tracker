// TCP client for a raw NMEA feed (for example the Norwegian Coastal
// Administration's open AIS stream). It never gives up: on connect failure,
// peer close, read error or a silent connection it backs off with jitter and
// reconnects, until asked to stop.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace maritime::ingest {

struct TcpOptions {
    std::string host;
    std::string port;
    std::chrono::milliseconds connect_timeout{5000};
    // No bytes for this long counts as a dead connection. The Norwegian feed
    // carries several messages a second, so 30 s of silence is not normal.
    std::chrono::milliseconds idle_timeout{30000};
    std::chrono::milliseconds backoff_initial{500};
    std::chrono::milliseconds backoff_max{30000};
    std::size_t max_line_length = 1024;
};

struct TcpStats {
    std::uint64_t connect_attempts = 0;
    std::uint64_t connects = 0;
    std::uint64_t connect_failures = 0;
    std::uint64_t peer_closes = 0;
    std::uint64_t read_errors = 0;
    std::uint64_t idle_timeouts = 0;
    std::uint64_t bytes = 0;
    std::uint64_t lines = 0;
    std::uint64_t oversize_dropped = 0;
};

class TcpSource {
public:
    explicit TcpSource(TcpOptions options) : options_(std::move(options)) {}

    // Runs until `stop` becomes true, calling on_line for each received line
    // on the calling thread. Checks `stop` at least every 100 ms.
    void run(const std::atomic<bool>& stop, const std::function<void(std::string_view)>& on_line);

    // Safe to call from another thread while run() is active.
    [[nodiscard]] TcpStats stats() const;

private:
    struct Counters {
        std::atomic<std::uint64_t> connect_attempts{0}, connects{0}, connect_failures{0}, peer_closes{0},
            read_errors{0}, idle_timeouts{0}, bytes{0}, lines{0}, oversize_dropped{0};
    };

    // Returns a connected socket, or -1 after counting the failure.
    int connect_once(const std::atomic<bool>& stop);
    // Reads until the connection ends. Returns true if any data arrived.
    bool read_until_closed(int fd, const std::atomic<bool>& stop,
                           const std::function<void(std::string_view)>& on_line);

    TcpOptions options_;
    Counters c_;
};

}  // namespace maritime::ingest

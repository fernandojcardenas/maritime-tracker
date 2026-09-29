// A small HTTP and WebSocket server for the live map: serves a fixed set of
// files built into the binary, and pushes JSON messages to every connected
// browser over a WebSocket.
//
// One thread runs the sockets (poll, non-blocking). publish() never blocks
// the caller: each client has its own queue, and a client whose queue grows
// past max_queued_bytes (a slow or stalled browser) is disconnected and
// counted, so one bad client cannot slow the tracker or the other clients.
// A client that connects receives the latest snapshot first, then every
// update published after it.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace maritime::serve {

struct Asset {
    std::string content_type;
    std::string_view body;  // must outlive the server (built-in files are static)
};

struct ServerOptions {
    std::string address = "127.0.0.1";  // listen on all interfaces with "0.0.0.0" (e.g. in a container)
    std::uint16_t port = 8080;          // 0: pick a free port (tests)
    std::size_t max_clients = 64;       // connections beyond this get 503
    std::size_t max_queued_bytes = 16U << 20U;
    std::chrono::milliseconds request_timeout{5000};  // to send a complete HTTP request
    std::string websocket_path = "/ws";
    std::map<std::string, Asset> assets;  // path -> file; "/" serves "/index.html"
};

struct ServerStats {
    std::uint64_t connections = 0;     // accepted
    std::uint64_t http_responses = 0;  // files, errors and 503s
    std::uint64_t websocket_opened = 0;
    std::uint64_t websocket_clients = 0;  // connected now
    std::uint64_t rejected_busy = 0;      // over max_clients
    std::uint64_t dropped_slow = 0;       // queue over max_queued_bytes
    std::uint64_t protocol_errors = 0;    // bad requests and bad frames
    std::uint64_t messages_queued = 0;    // WebSocket messages queued for clients
    std::uint64_t bytes_sent = 0;
};

class Server {
public:
    explicit Server(ServerOptions options);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    // Binds and starts serving. Returns an error message on failure.
    [[nodiscard]] std::optional<std::string> start();
    void stop();

    [[nodiscard]] std::uint16_t port() const;  // the bound port

    // Sends `update` to every WebSocket client, and makes `snapshot` the first
    // message new clients receive. Either may be empty to skip it.
    void publish(std::string_view update, std::string_view snapshot);

    [[nodiscard]] ServerStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace maritime::serve

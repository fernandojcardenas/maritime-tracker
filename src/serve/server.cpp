#include "maritime/serve/server.hpp"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include "maritime/serve/websocket.hpp"

namespace maritime::serve {

namespace {

using Clock = std::chrono::steady_clock;
using Buffer = std::shared_ptr<const std::string>;

bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

struct Connection {
    enum class State : std::uint8_t { Request, WebSocket, Closing };
    int fd = -1;
    State state = State::Request;
    Clock::time_point opened;
    std::string in;                // HTTP request bytes so far
    FrameParser frames;            // WebSocket frames from the client
    std::deque<Buffer> out;        // queued output
    std::size_t out_offset = 0;    // bytes of out.front() already sent
    std::size_t queued_bytes = 0;  // total unsent bytes
};

}  // namespace

struct Server::Impl {
    ServerOptions opt;
    int listen_fd = -1;
    std::array<int, 2> wake{-1, -1};  // self-pipe: publish() and stop() wake poll()
    std::uint16_t bound_port = 0;
    std::thread thread;
    std::atomic<bool> stopping{false};

    mutable std::mutex mu;  // guards everything below
    std::vector<std::unique_ptr<Connection>> conns;
    Buffer snapshot;  // framed
    ServerStats stats;

    explicit Impl(ServerOptions o) : opt(std::move(o)) {}

    static void queue(Connection& c, Buffer b) {
        c.queued_bytes += b->size();
        c.out.push_back(std::move(b));
    }

    void respond(Connection& c, int status, std::string_view type, std::string_view body) {
        queue(c, std::make_shared<const std::string>(http_response(status, type, body)));
        c.state = Connection::State::Closing;
        ++stats.http_responses;
    }

    void on_request(Connection& c) {
        const Request r = parse_request(c.in);
        switch (r.kind) {
            case Request::Kind::Incomplete:
                return;
            case Request::Kind::Bad:
                ++stats.protocol_errors;
                respond(c, 400, "text/plain", "bad request\n");
                return;
            case Request::Kind::WrongVersion:
                respond(c, 426, "text/plain", "WebSocket version 13 required\n");
                return;
            case Request::Kind::Get: {
                const std::string path = r.path == "/" ? "/index.html" : r.path;
                const auto it = opt.assets.find(path);
                if (it == opt.assets.end()) {
                    respond(c, 404, "text/plain", "not found\n");
                } else {
                    respond(c, 200, it->second.content_type, it->second.body);
                }
                return;
            }
            case Request::Kind::Upgrade:
                if (r.path != opt.websocket_path) {
                    respond(c, 404, "text/plain", "not found\n");
                    return;
                }
                queue(c, std::make_shared<const std::string>(upgrade_response(r.websocket_key)));
                c.state = Connection::State::WebSocket;
                c.in.clear();
                ++stats.websocket_opened;
                if (snapshot) {
                    queue(c, snapshot);
                    ++stats.messages_queued;
                }
                return;
        }
    }

    void on_frames(Connection& c, std::string_view bytes) {
        std::vector<Frame> frames;
        const auto error = c.frames.feed(bytes, frames);
        for (const auto& f : frames) {
            if (f.opcode == Opcode::Ping) queue(c, std::make_shared<const std::string>(pong_frame(f.payload)));
            if (f.opcode == Opcode::Close) {
                queue(c, std::make_shared<const std::string>(close_frame(1000)));
                c.state = Connection::State::Closing;
                return;
            }
            // Text, binary and pong frames are read and ignored: this server only sends.
        }
        if (error) {
            ++stats.protocol_errors;
            queue(c, std::make_shared<const std::string>(close_frame(*error)));
            c.state = Connection::State::Closing;
        }
    }

    // Reads what is available. Returns false if the connection is finished.
    bool read_from(Connection& c) {
        std::array<char, 4096> buf{};
        while (true) {
            const ssize_t n = ::recv(c.fd, buf.data(), buf.size(), 0);
            if (n == 0) return false;
            if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
            const std::string_view bytes(buf.data(), static_cast<std::size_t>(n));
            if (c.state == Connection::State::Request) {
                c.in.append(bytes);
                on_request(c);
            } else if (c.state == Connection::State::WebSocket) {
                on_frames(c, bytes);
            }
            // Closing: discard whatever else arrives.
        }
    }

    // Writes what the socket takes. Returns false if the connection is finished.
    bool write_to(Connection& c) {
        while (!c.out.empty()) {
            const std::string& b = *c.out.front();
            const ssize_t n = ::send(c.fd, b.data() + c.out_offset, b.size() - c.out_offset, MSG_NOSIGNAL);
            if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
            stats.bytes_sent += static_cast<std::uint64_t>(n);
            c.out_offset += static_cast<std::size_t>(n);
            c.queued_bytes -= static_cast<std::size_t>(n);
            if (c.out_offset == b.size()) {
                c.out.pop_front();
                c.out_offset = 0;
            }
        }
        return c.state != Connection::State::Closing;  // done sending a final response
    }

    void close_conn(std::size_t i) {
        ::close(conns[i]->fd);
        conns.erase(conns.begin() + static_cast<std::ptrdiff_t>(i));
    }

    void accept_all() {
        while (true) {
            const int fd = ::accept(listen_fd, nullptr, nullptr);
            if (fd < 0) return;
            if (!set_nonblocking(fd)) {
                ::close(fd);
                continue;
            }
            ++stats.connections;
            auto c = std::make_unique<Connection>();
            c->fd = fd;
            c->opened = Clock::now();
            if (conns.size() >= opt.max_clients) {
                ++stats.rejected_busy;
                respond(*c, 503, "text/plain", "too many clients\n");
            }
            conns.push_back(std::move(c));
        }
    }

    void run() {
        while (!stopping.load()) {
            std::vector<pollfd> fds;
            {
                const std::lock_guard lock(mu);
                fds.push_back({listen_fd, POLLIN, 0});
                fds.push_back({wake[0], POLLIN, 0});
                for (const auto& c : conns) {
                    fds.push_back({c->fd, static_cast<short>(POLLIN | (c->out.empty() ? 0 : POLLOUT)), 0});
                }
            }
            if (::poll(fds.data(), fds.size(), 500) < 0 && errno != EINTR) break;
            if ((fds[1].revents & POLLIN) != 0) {
                std::array<char, 256> drain{};
                while (::read(wake[0], drain.data(), drain.size()) > 0) {
                }
            }
            const std::lock_guard lock(mu);
            if ((fds[0].revents & POLLIN) != 0) accept_all();
            const auto now = Clock::now();
            for (std::size_t i = conns.size(); i-- > 0;) {
                Connection& c = *conns[i];
                bool alive = true;
                // Connections accepted in this pass are not in `fds` yet.
                const auto it = std::find_if(fds.begin() + 2, fds.end(), [&](const pollfd& p) { return p.fd == c.fd; });
                const short ev = it == fds.end() ? short{0} : it->revents;
                if ((ev & (POLLERR | POLLNVAL)) != 0) alive = false;
                if (alive && (ev & (POLLIN | POLLHUP)) != 0) alive = read_from(c);
                if (alive && c.state == Connection::State::Request && now - c.opened > opt.request_timeout) {
                    ++stats.protocol_errors;
                    respond(c, 400, "text/plain", "request timeout\n");
                }
                if (alive && !c.out.empty()) alive = write_to(c);
                if (alive && c.state == Connection::State::Closing && c.out.empty()) alive = false;
                if (!alive) close_conn(i);
            }
            stats.websocket_clients = static_cast<std::uint64_t>(std::count_if(
                conns.begin(), conns.end(), [](const auto& c) { return c->state == Connection::State::WebSocket; }));
        }
    }

    void wake_up() const {
        const char b = 1;
        (void)!::write(wake[1], &b, 1);
    }
};

Server::Server(ServerOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}

Server::~Server() {
    stop();
}

std::optional<std::string> Server::start() {
    Impl& s = *impl_;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo* res = nullptr;
    const std::string port = std::to_string(s.opt.port);
    if (const int rc = ::getaddrinfo(s.opt.address.c_str(), port.c_str(), &hints, &res); rc != 0) {
        return "bad listen address " + s.opt.address + ": " + ::gai_strerror(rc);
    }
    std::string error = "cannot listen on " + s.opt.address + ":" + port;
    for (addrinfo* a = res; a != nullptr && s.listen_fd < 0; a = a->ai_next) {
        const int fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        const int yes = 1;
        (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (::bind(fd, a->ai_addr, a->ai_addrlen) == 0 && ::listen(fd, 64) == 0 && set_nonblocking(fd)) {
            s.listen_fd = fd;
        } else {
            error += ": " + std::generic_category().message(errno);
            ::close(fd);
        }
    }
    ::freeaddrinfo(res);
    if (s.listen_fd < 0) return error;
    sockaddr_storage bound{};
    socklen_t len = sizeof bound;
    if (::getsockname(s.listen_fd, reinterpret_cast<sockaddr*>(&bound), &len) ==
        0) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        s.bound_port = bound.ss_family == AF_INET6
                           ? ntohs(reinterpret_cast<sockaddr_in6*>(&bound)
                                       ->sin6_port)  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                           : ntohs(reinterpret_cast<sockaddr_in*>(&bound)
                                       ->sin_port);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    }
    if (::pipe(s.wake.data()) != 0 || !set_nonblocking(s.wake[0]) || !set_nonblocking(s.wake[1])) {
        return "cannot create wake-up pipe: " + std::generic_category().message(errno);
    }
    s.thread = std::thread([&s] { s.run(); });
    return std::nullopt;
}

void Server::stop() {
    Impl& s = *impl_;
    if (!s.thread.joinable()) return;
    s.stopping = true;
    s.wake_up();
    s.thread.join();
    const std::lock_guard lock(s.mu);
    for (auto& c : s.conns) ::close(c->fd);
    s.conns.clear();
    ::close(s.listen_fd);
    ::close(s.wake[0]);
    ::close(s.wake[1]);
    s.listen_fd = -1;
}

std::uint16_t Server::port() const {
    return impl_->bound_port;
}

void Server::publish(std::string_view update, std::string_view snapshot) {
    Impl& s = *impl_;
    // Frame once, outside the lock; every client shares the same buffer.
    const Buffer u = update.empty() ? nullptr : std::make_shared<const std::string>(text_frame(update));
    const Buffer snap = snapshot.empty() ? nullptr : std::make_shared<const std::string>(text_frame(snapshot));
    {
        const std::lock_guard lock(s.mu);
        if (snap) s.snapshot = snap;
        if (u) {
            for (const auto& c : s.conns) {
                if (c->state != Connection::State::WebSocket) continue;
                if (c->queued_bytes + u->size() > s.opt.max_queued_bytes) {
                    // Too far behind: drop this client rather than grow without
                    // limit. A frame may be half sent, so no close frame: just
                    // close the connection.
                    ++s.stats.dropped_slow;
                    c->out.clear();
                    c->out_offset = 0;
                    c->queued_bytes = 0;
                    c->state = Connection::State::Closing;
                    continue;
                }
                Impl::queue(*c, u);
                ++s.stats.messages_queued;
            }
        }
    }
    s.wake_up();
}

ServerStats Server::stats() const {
    const std::lock_guard lock(impl_->mu);
    return impl_->stats;
}

}  // namespace maritime::serve

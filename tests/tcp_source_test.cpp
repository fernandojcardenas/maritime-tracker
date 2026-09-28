// Tests TcpSource against a fake feed server on the loopback interface:
// split lines, peer disconnects, silent connections and refused connects.
#include "maritime/ingest/tcp_source.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace maritime::ingest;
using namespace std::chrono_literals;

namespace {

// Listening socket on 127.0.0.1 with an OS-assigned port.
class FakeFeed {
public:
    FakeFeed() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        const int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        ::listen(fd_, 4);
        socklen_t len = sizeof addr;
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = std::to_string(ntohs(addr.sin_port));
    }
    FakeFeed(const FakeFeed&) = delete;
    FakeFeed& operator=(const FakeFeed&) = delete;
    ~FakeFeed() { ::close(fd_); }

    [[nodiscard]] const std::string& port() const { return port_; }
    [[nodiscard]] int accept_one() const { return ::accept(fd_, nullptr, nullptr); }

    static void send_all(int fd, std::string_view data) {
        while (!data.empty()) {
            const auto n = ::send(fd, data.data(), data.size(), 0);
            if (n <= 0) return;
            data.remove_prefix(static_cast<std::size_t>(n));
        }
    }

private:
    int fd_ = -1;
    std::string port_;
};

struct Collected {
    std::mutex mu;
    std::vector<std::string> lines;
    void add(std::string_view l) {
        const std::lock_guard lock(mu);
        lines.emplace_back(l);
    }
    std::size_t size() {
        const std::lock_guard lock(mu);
        return lines.size();
    }
};

bool wait_for(const std::function<bool()>& cond, std::chrono::milliseconds limit = 5s) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        if (cond()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return cond();
}

TcpOptions fast_options(const std::string& port) {
    TcpOptions o;
    o.host = "127.0.0.1";
    o.port = port;
    o.connect_timeout = 500ms;
    o.idle_timeout = 300ms;
    o.backoff_initial = 20ms;
    o.backoff_max = 100ms;
    return o;
}

}  // namespace

TEST(TcpSource, ReceivesSplitLinesAndReconnectsAfterPeerClose) {
    FakeFeed feed;
    std::thread server([&] {
        const int a = feed.accept_one();
        FakeFeed::send_all(a, "!AIVDM,1,1,,A,1");
        std::this_thread::sleep_for(20ms);
        FakeFeed::send_all(a, "3,0*00\r\n!AIVDM,2\n");
        ::close(a);  // peer drops the connection
        const int b = feed.accept_one();
        FakeFeed::send_all(b, "third\n");
        std::this_thread::sleep_for(200ms);
        ::close(b);
    });

    TcpSource src(fast_options(feed.port()));
    std::atomic<bool> stop{false};
    Collected got;
    std::thread client([&] { src.run(stop, [&](std::string_view l) { got.add(l); }); });

    EXPECT_TRUE(wait_for([&] { return got.size() >= 3; }));
    stop = true;
    client.join();
    server.join();

    EXPECT_EQ(got.lines, (std::vector<std::string>{"!AIVDM,1,1,,A,13,0*00", "!AIVDM,2", "third"}));
    const auto s = src.stats();
    EXPECT_GE(s.connects, 2U);
    EXPECT_GE(s.peer_closes, 1U);
    EXPECT_EQ(s.lines, 3U);
}

TEST(TcpSource, ReconnectsAfterSilentConnection) {
    FakeFeed feed;
    std::atomic<bool> done{false};
    std::thread server([&] {
        const int silent = feed.accept_one();  // accept, then say nothing
        const int b = feed.accept_one();       // the client gives up and comes back
        FakeFeed::send_all(b, "after-idle\n");
        while (!done) std::this_thread::sleep_for(10ms);
        ::close(b);
        ::close(silent);
    });

    TcpSource src(fast_options(feed.port()));
    std::atomic<bool> stop{false};
    Collected got;
    std::thread client([&] { src.run(stop, [&](std::string_view l) { got.add(l); }); });

    EXPECT_TRUE(wait_for([&] { return got.size() >= 1; }));
    stop = true;
    client.join();
    done = true;
    server.join();

    EXPECT_EQ(got.lines.at(0), "after-idle");
    EXPECT_GE(src.stats().idle_timeouts, 1U);
}

TEST(TcpSource, CountsRefusedConnectionsAndStopsPromptly) {
    std::string port;
    {
        const FakeFeed closed;  // bind a port, then free it: nothing listens there
        port = closed.port();
    }
    TcpSource src(fast_options(port));
    std::atomic<bool> stop{false};
    std::thread client([&] { src.run(stop, [](std::string_view) {}); });

    EXPECT_TRUE(wait_for([&] { return src.stats().connect_failures >= 3; }));
    const auto t0 = std::chrono::steady_clock::now();
    stop = true;
    client.join();
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 1s);
    EXPECT_EQ(src.stats().connects, 0U);
}

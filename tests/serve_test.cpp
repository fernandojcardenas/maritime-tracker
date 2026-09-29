#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "maritime/serve/feed.hpp"
#include "maritime/serve/server.hpp"
#include "maritime/serve/web_assets.hpp"
#include "maritime/serve/websocket.hpp"

using namespace maritime::serve;

namespace {

std::string hex(const std::array<std::uint8_t, 20>& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    for (const auto b : d) {
        s.push_back(kHex[b >> 4U]);
        s.push_back(kHex[b & 15U]);
    }
    return s;
}

std::string b64(std::string_view s) {
    return base64(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}  // NOLINT

// A client frame (always masked), as a browser would send it.
std::string client_frame(std::uint8_t first, std::string_view payload, std::array<std::uint8_t, 4> key = {1, 2, 3, 4}) {
    std::string f;
    f.push_back(static_cast<char>(first));
    if (payload.size() < 126) {
        f.push_back(static_cast<char>(0x80U | payload.size()));
    } else {
        f.push_back(static_cast<char>(0x80U | 126U));
        f.push_back(static_cast<char>(payload.size() >> 8U));
        f.push_back(static_cast<char>(payload.size() & 0xFFU));
    }
    for (const auto k : key) f.push_back(static_cast<char>(k));
    for (std::size_t i = 0; i < payload.size(); ++i) f.push_back(static_cast<char>(payload[i] ^ key.at(i % 4)));
    return f;
}

const char* kUpgrade =
    "GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";

// A blocking test client over loopback.
class Client {
public:
    explicit Client(std::uint16_t port, int rcvbuf = 0) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (rcvbuf > 0) (void)::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connected_ = ::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;  // NOLINT
    }
    ~Client() { ::close(fd_); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    [[nodiscard]] bool connected() const { return connected_; }
    void send(std::string_view s) const { (void)::send(fd_, s.data(), s.size(), MSG_NOSIGNAL); }

    // Reads until `pred(received)` or the timeout. Returns what was received.
    template <class Pred>
    std::string read_until(Pred pred, int timeout_ms = 3000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!pred(buf_) && std::chrono::steady_clock::now() < deadline) {
            pollfd p{fd_, POLLIN, 0};
            if (::poll(&p, 1, 50) <= 0) continue;
            std::array<char, 65536> tmp{};
            const ssize_t n = ::recv(fd_, tmp.data(), tmp.size(), 0);
            if (n <= 0) {
                closed_ = true;
                break;
            }
            buf_.append(tmp.data(), static_cast<std::size_t>(n));
        }
        return buf_;
    }
    std::string read_for(int ms) {
        return read_until([](const std::string&) { return false; }, ms);
    }
    [[nodiscard]] bool closed() const { return closed_; }
    void clear() { buf_.clear(); }

private:
    int fd_ = -1;
    bool connected_ = false;
    bool closed_ = false;
    std::string buf_;
};

bool contains(const std::string& s, std::string_view part) {
    return s.find(part) != std::string::npos;
}

}  // namespace

// ---- pure functions ---------------------------------------------------------

TEST(WebSocket, Sha1TestVectors) {  // FIPS 180 examples
    EXPECT_EQ(hex(sha1("")), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    EXPECT_EQ(hex(sha1("abc")), "a9993e364706816aba3e25717850c26c9cd0d89d");
    EXPECT_EQ(hex(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
              "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    EXPECT_EQ(hex(sha1(std::string(1000000, 'a'))), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(WebSocket, Base64TestVectors) {  // RFC 4648, section 10
    EXPECT_EQ(b64(""), "");
    EXPECT_EQ(b64("f"), "Zg==");
    EXPECT_EQ(b64("fo"), "Zm8=");
    EXPECT_EQ(b64("foo"), "Zm9v");
    EXPECT_EQ(b64("foob"), "Zm9vYg==");
    EXPECT_EQ(b64("fooba"), "Zm9vYmE=");
    EXPECT_EQ(b64("foobar"), "Zm9vYmFy");
}

TEST(WebSocket, AcceptKeyFromTheRfc) {  // RFC 6455, section 1.3
    EXPECT_EQ(accept_key("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WebSocket, ParsesRequests) {
    auto r = parse_request(kUpgrade);
    EXPECT_EQ(r.kind, Request::Kind::Upgrade);
    EXPECT_EQ(r.path, "/ws");
    EXPECT_EQ(r.websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");
    EXPECT_EQ(r.length, std::strlen(kUpgrade));

    // Header names in any case, Connection with several tokens (as Firefox sends).
    r = parse_request(
        "GET /ws?x=1 HTTP/1.1\r\nupgrade: WebSocket\r\nconnection: keep-alive, Upgrade\r\n"
        "sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\nsec-websocket-version: 13\r\n\r\n");
    EXPECT_EQ(r.kind, Request::Kind::Upgrade);
    EXPECT_EQ(r.path, "/ws");

    EXPECT_EQ(parse_request("GET /app.js HTTP/1.1\r\nHost: x\r\n\r\n").kind, Request::Kind::Get);
    EXPECT_EQ(parse_request("GET / HTTP/1.1\r\nHost: x\r\n").kind, Request::Kind::Incomplete);
    EXPECT_EQ(parse_request("POST / HTTP/1.1\r\n\r\n").kind, Request::Kind::Bad);
    EXPECT_EQ(parse_request("GET / HTTP/1.0\r\n\r\n").kind, Request::Kind::Bad);
    EXPECT_EQ(parse_request("GET relative HTTP/1.1\r\n\r\n").kind, Request::Kind::Bad);
    EXPECT_EQ(parse_request("GET / HTTP/1.1\r\nHost: x\r\n folded\r\n\r\n").kind, Request::Kind::Bad);
    EXPECT_EQ(parse_request("GET / HTTP/1.1\r\nno-colon\r\n\r\n").kind, Request::Kind::Bad);
    EXPECT_EQ(parse_request(std::string(kMaxRequest, 'a')).kind, Request::Kind::Bad);  // too long, no end
    // Upgrade requests that are not valid.
    EXPECT_EQ(parse_request("GET /ws HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Version: 13\r\n\r\n")
                  .kind,
              Request::Kind::Bad);  // no key
    EXPECT_EQ(parse_request("GET /ws HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Key: short==\r\nSec-WebSocket-Version: 13\r\n\r\n")
                  .kind,
              Request::Kind::Bad);
    EXPECT_EQ(parse_request("GET /ws HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 8\r\n\r\n")
                  .kind,
              Request::Kind::WrongVersion);
}

TEST(WebSocket, ServerFramesUseTheRightLengthEncoding) {
    EXPECT_EQ(text_frame("Hello"), std::string("\x81\x05Hello"));
    const std::string medium = text_frame(std::string(200, 'x'));
    EXPECT_EQ(medium.substr(0, 4), std::string("\x81\x7e\x00\xc8", 4));
    const std::string large = text_frame(std::string(70000, 'x'));
    EXPECT_EQ(large.substr(0, 10), std::string("\x81\x7f\x00\x00\x00\x00\x00\x01\x11\x70", 10));
    EXPECT_EQ(large.size(), 70010U);
    EXPECT_EQ(close_frame(1000), std::string("\x88\x02\x03\xe8", 4));
}

TEST(WebSocket, ParsesClientFrames) {
    // RFC 6455, section 5.7: a masked "Hello".
    const std::string hello("\x81\x85\x37\xfa\x21\x3d\x7f\x9f\x4d\x51\x58", 11);
    FrameParser p;
    std::vector<Frame> out;
    EXPECT_FALSE(p.feed(hello, out));
    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].opcode, Opcode::Text);
    EXPECT_EQ(out[0].payload, "Hello");

    // One byte at a time, and a fragmented message with a ping in between.
    FrameParser q;
    out.clear();
    const std::string stream = client_frame(0x01, "Hel") + client_frame(0x89, "p") + client_frame(0x80, "lo");
    for (const char c : stream) ASSERT_FALSE(q.feed(std::string_view(&c, 1), out));
    ASSERT_EQ(out.size(), 3U);
    EXPECT_EQ(out[1].opcode, Opcode::Ping);
    EXPECT_EQ(out[2].opcode, Opcode::Continuation);
    EXPECT_TRUE(out[2].fin);
}

TEST(WebSocket, RejectsBadClientFrames) {
    const auto code = [](const std::string& bytes) {
        FrameParser p;
        std::vector<Frame> out;
        return p.feed(bytes, out);
    };
    EXPECT_EQ(code(std::string("\x81\x05Hello")), 1002);                       // not masked
    EXPECT_EQ(code(client_frame(0xC1, "x")), 1002);                            // reserved bit
    EXPECT_EQ(code(client_frame(0x83, "x")), 1002);                            // unknown opcode
    EXPECT_EQ(code(client_frame(0x80, "x")), 1002);                            // continuation with nothing to continue
    EXPECT_EQ(code(client_frame(0x01, "a") + client_frame(0x81, "b")), 1002);  // new message mid-message
    EXPECT_EQ(code(client_frame(0x09, "x")), 1002);                            // fragmented control frame
    EXPECT_EQ(code(client_frame(0x89, std::string(126, 'x'))), 1002);          // control frame over 125 bytes
    EXPECT_EQ(code(client_frame(0x88, "x")), 1002);                            // close with a 1-byte body
    EXPECT_EQ(code(client_frame(0x81, std::string(kMaxClientMessage + 1, 'x'))), 1009);
    // A 64-bit length is refused before the payload arrives.
    EXPECT_EQ(code(std::string("\x81\xff\x00\x00\x01\x00\x00\x00\x00\x00", 10)), 1009);
}

// ---- the server over real sockets --------------------------------------------

namespace {
ServerOptions options() {
    static const std::string kIndex = "<!doctype html><title>map</title>";
    ServerOptions o;
    o.port = 0;
    o.assets["/index.html"] = {"text/html; charset=utf-8", kIndex};
    return o;
}
}  // namespace

TEST(Server, ServesFilesAndNotFound) {
    Server s(options());
    ASSERT_FALSE(s.start());
    Client a(s.port());
    ASSERT_TRUE(a.connected());
    a.send("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    const auto r = a.read_until([](const std::string& b) { return contains(b, "</title>"); });
    EXPECT_TRUE(contains(r, "HTTP/1.1 200 OK"));
    EXPECT_TRUE(contains(r, "Content-Security-Policy"));
    Client b(s.port());
    b.send("GET /../etc/passwd HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(contains(b.read_until([](const std::string& x) { return contains(x, "\r\n\r\n"); }), "404"));
    Client c(s.port());
    c.send("DELETE / HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(contains(c.read_until([](const std::string& x) { return contains(x, "\r\n\r\n"); }), "400"));
}

TEST(Server, UpgradesThenSendsSnapshotAndUpdates) {
    Server s(options());
    ASSERT_FALSE(s.start());
    s.publish("", R"({"type":"snapshot"})");
    Client a(s.port());
    a.send(kUpgrade);
    auto r = a.read_until([](const std::string& b) { return contains(b, "snapshot"); });
    EXPECT_TRUE(contains(r, "HTTP/1.1 101 Switching Protocols"));
    EXPECT_TRUE(contains(r, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    EXPECT_TRUE(contains(r, std::string("\x81\x13{\"type\":\"snapshot\"}")));
    s.publish(R"({"type":"update","n":1})", "");
    r = a.read_until([](const std::string& b) { return contains(b, "\"n\":1"); });
    EXPECT_TRUE(contains(r, "\"n\":1"));

    // Ping: pong with the same payload. Close: the server closes too.
    a.clear();
    a.send(client_frame(0x89, "hi"));
    EXPECT_TRUE(
        contains(a.read_until([](const std::string& b) { return contains(b, "hi"); }), std::string("\x8a\x02hi")));
    a.clear();
    a.send(client_frame(0x88, std::string("\x03\xe8", 2)));
    const auto closing = a.read_for(500);
    EXPECT_EQ(closing.substr(0, 2), std::string("\x88\x02"));
    EXPECT_TRUE(a.closed());
    EXPECT_EQ(s.stats().websocket_opened, 1U);
}

TEST(Server, BadFramesCloseTheConnection) {
    Server s(options());
    ASSERT_FALSE(s.start());
    Client a(s.port());
    a.send(kUpgrade);
    a.read_until([](const std::string& b) { return contains(b, "\r\n\r\n"); });
    a.clear();
    a.send(std::string("\x81\x05Hello"));  // unmasked
    const auto r = a.read_for(500);
    EXPECT_EQ(r, std::string("\x88\x02\x03\xea", 4));  // close 1002
    EXPECT_TRUE(a.closed());
    EXPECT_GE(s.stats().protocol_errors, 1U);
}

TEST(Server, DropsAClientThatFallsBehind) {
    ServerOptions o = options();
    o.max_queued_bytes = 256 * 1024;
    Server s(o);
    ASSERT_FALSE(s.start());
    Client slow(s.port(), 4096);  // small receive buffer, never reads
    slow.send(kUpgrade);
    Client fast(s.port());
    fast.send(kUpgrade);
    fast.read_until([](const std::string& b) { return contains(b, "\r\n\r\n"); });
    const std::string big(32 * 1024, 'x');
    std::size_t fast_bytes = 0;
    for (int i = 0; i < 200 && s.stats().dropped_slow == 0; ++i) {
        s.publish(big, "");
        fast_bytes = fast.read_for(5).size();
    }
    EXPECT_EQ(s.stats().dropped_slow, 1U);
    EXPECT_GT(fast_bytes, 256U * 1024U);  // the fast client kept receiving
}

TEST(Server, RefusesClientsOverTheLimit) {
    ServerOptions o = options();
    o.max_clients = 1;
    Server s(o);
    ASSERT_FALSE(s.start());
    Client a(s.port());
    a.send(kUpgrade);
    a.read_until([](const std::string& b) { return contains(b, "\r\n\r\n"); });
    Client b(s.port());
    EXPECT_TRUE(contains(b.read_until([](const std::string& x) { return contains(x, "\r\n\r\n"); }), "503"));
    EXPECT_EQ(s.stats().rejected_busy, 1U);
}

TEST(Server, TimesOutIncompleteRequests) {
    ServerOptions o = options();
    o.request_timeout = std::chrono::milliseconds(200);
    Server s(o);
    ASSERT_FALSE(s.start());
    Client a(s.port());
    a.send("GET / HTTP/1.1\r\n");  // and nothing more
    const auto r = a.read_until([](const std::string& b) { return contains(b, "\r\n\r\n"); }, 3000);
    EXPECT_TRUE(contains(r, "400"));
}

TEST(Server, StopsCleanlyWithClientsConnected) {
    auto s = std::make_unique<Server>(options());
    ASSERT_FALSE(s->start());
    Client a(s->port());
    a.send(kUpgrade);
    a.read_until([](const std::string& b) { return contains(b, "\r\n\r\n"); });
    std::thread publisher([&] {
        for (int i = 0; i < 100; ++i) s->publish(R"({"type":"update"})", R"({"type":"snapshot"})");
    });
    publisher.join();
    s->stop();
    s.reset();
    a.read_for(200);
    EXPECT_TRUE(a.closed());
}

// ---- the live feed's messages -----------------------------------------------

TEST(LiveFeed, SnapshotHasEverythingAndUpdatesOnlyChanges) {
    using nlohmann::json;
    maritime::track::Tracker tracker;
    const auto fix = [](std::uint32_t mmsi, double t, double lat) {
        maritime::track::Fix f;
        f.mmsi = mmsi;
        f.t = t;
        f.lat_deg = lat;
        f.lon_deg = 10.0;
        f.sog_knots = 10.0;
        f.cog_deg = 0.0;
        return f;
    };
    (void)tracker.add(fix(2, 100, 60.0));
    (void)tracker.add(fix(1, 100, 61.0));
    LiveFeed feed;
    FeedTotals totals;
    totals.messages = 2;

    auto m = feed.tick(tracker, 100, totals);
    auto snap = json::parse(m.snapshot);
    auto upd = json::parse(m.update);
    EXPECT_EQ(snap["type"], "snapshot");
    ASSERT_EQ(snap["tracks"].size(), 2U);
    EXPECT_EQ(snap["tracks"][0][0], 1);  // sorted by MMSI
    EXPECT_NEAR(snap["tracks"][0][1].get<double>(), 61.0, 1e-5);
    EXPECT_NEAR(snap["tracks"][0][3].get<double>(), 10.0, 0.1);  // speed from the track
    EXPECT_EQ(upd["tracks"].size(), 2U);                         // first tick: all new
    EXPECT_EQ(snap["totals"]["messages"], 2);

    // Nothing changed: an empty update, the same snapshot.
    upd = json::parse(feed.tick(tracker, 101, totals).update);
    EXPECT_TRUE(upd["tracks"].empty());
    EXPECT_TRUE(upd["removed"].empty());

    // One vessel reports, an anomaly is flagged, an encounter appears.
    (void)tracker.add(fix(2, 110, 60.001));
    maritime::anomaly::Anomaly a;
    a.kind = maritime::anomaly::Kind::Gap;
    a.mmsi = 2;
    a.value = 700;
    feed.add_anomaly(a);
    maritime::risk::PairAssessment pa;
    pa.mmsi_a = 1;
    pa.mmsi_b = 2;
    pa.assessment.type = maritime::risk::Encounter::Crossing;
    pa.assessment.role_a = maritime::risk::Role::GiveWay;
    feed.set_encounters({pa});
    m = feed.tick(tracker, 110, totals);
    upd = json::parse(m.update);
    ASSERT_EQ(upd["tracks"].size(), 1U);
    EXPECT_EQ(upd["tracks"][0][0], 2);
    ASSERT_EQ(upd["anomalies"].size(), 1U);
    EXPECT_EQ(upd["anomalies"][0]["kind"], "gap");
    EXPECT_EQ(upd["encounters"][0]["type"], "crossing");
    EXPECT_EQ(upd["encounters"][0]["role_a"], "give_way");
    EXPECT_EQ(json::parse(m.snapshot)["anomalies"].size(), 1U);
    // An anomaly is announced once, and stays in the snapshot's recent list.
    upd = json::parse(feed.tick(tracker, 111, totals).update);
    EXPECT_TRUE(upd["anomalies"].empty());

    // Tracks that expire are listed as removed, once.
    tracker.expire(100000);
    upd = json::parse(feed.tick(tracker, 100000, totals).update);
    EXPECT_EQ(upd["removed"], json::array({1, 2}));
    upd = json::parse(feed.tick(tracker, 100001, totals).update);
    EXPECT_TRUE(upd["removed"].empty());
}

TEST(WebAssets, BuiltInFilesMatchTheSourcesByteForByte) {
    const auto& assets = web_assets();
    for (const char* f : {"index.html", "app.js", "style.css", "land.json", "vendor/leaflet/leaflet.js",
                          "vendor/leaflet/leaflet.css", "vendor/leaflet/LICENSE"}) {
        std::ifstream in(std::string(MT_WEB_DIR) + "/" + f, std::ios::binary);
        ASSERT_TRUE(in) << f;
        const std::string disk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto it = assets.find(std::string("/") + f);
        ASSERT_NE(it, assets.end()) << f;
        EXPECT_EQ(it->second.body, disk) << f;
    }
    EXPECT_EQ(assets.at("/index.html").content_type, "text/html; charset=utf-8");
    EXPECT_EQ(assets.at("/app.js").content_type, "text/javascript; charset=utf-8");
    // The coastline is valid GeoJSON.
    const auto land = nlohmann::json::parse(assets.at("/land.json").body);
    EXPECT_EQ(land["type"], "FeatureCollection");
    EXPECT_GT(land["features"].size(), 1000U);
}

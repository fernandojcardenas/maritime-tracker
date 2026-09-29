// The parts of HTTP/1.1 and WebSocket (RFC 6455) the live map needs, as pure
// functions: parsing a browser's request, the WebSocket handshake, framing
// the server's messages, and parsing the (few) frames a browser sends back.
//
// Everything a client sends is untrusted and bounded: requests to
// kMaxRequest bytes, client messages to kMaxClientMessage bytes. The server
// only ever sends; clients may ping, pong and close, and anything else they
// send is read and discarded.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace maritime::serve {

inline constexpr std::size_t kMaxRequest = 8192;
inline constexpr std::size_t kMaxClientMessage = 4096;

[[nodiscard]] std::array<std::uint8_t, 20> sha1(std::string_view data);
[[nodiscard]] std::string base64(const std::uint8_t* data, std::size_t size);

// Sec-WebSocket-Accept for a client's Sec-WebSocket-Key.
[[nodiscard]] std::string accept_key(std::string_view client_key);

struct Request {
    enum class Kind : std::uint8_t {
        Incomplete,    // no blank line yet: read more (up to kMaxRequest)
        Bad,           // malformed or too large: answer 400 and close
        Get,           // a plain GET for `path`
        Upgrade,       // a valid WebSocket upgrade
        WrongVersion,  // an upgrade for a WebSocket version other than 13: answer 426
    };
    Kind kind = Kind::Incomplete;
    std::string path;  // without query string
    std::string websocket_key;
    std::size_t length = 0;  // bytes of the request, including the blank line
};

// Parses the start of `buffer` as an HTTP/1.1 request.
[[nodiscard]] Request parse_request(std::string_view buffer);

// Full HTTP responses.
[[nodiscard]] std::string upgrade_response(std::string_view client_key);
[[nodiscard]] std::string http_response(int status, std::string_view content_type, std::string_view body);

// Server-to-client frames (never masked).
[[nodiscard]] std::string text_frame(std::string_view payload);
[[nodiscard]] std::string pong_frame(std::string_view payload);
[[nodiscard]] std::string close_frame(std::uint16_t code);

enum class Opcode : std::uint8_t { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

struct Frame {
    Opcode opcode = Opcode::Text;
    bool fin = true;
    std::string payload;  // unmasked
};

// Incremental parser for client-to-server frames. Returns frames as they
// complete; on a protocol violation it stops and reports the close code to
// send (1002 protocol error, 1009 message too big).
class FrameParser {
public:
    // Appends completed frames to `out`. Returns a close code on error.
    std::optional<std::uint16_t> feed(std::string_view bytes, std::vector<Frame>& out);

private:
    std::string buffer_;
    std::size_t message_bytes_ = 0;  // data bytes in the current fragmented message
    bool continuing_ = false;        // a fragmented message is in progress
    bool failed_ = false;
};

}  // namespace maritime::serve

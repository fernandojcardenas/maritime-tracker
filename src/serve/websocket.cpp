#include "maritime/serve/websocket.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace maritime::serve {

namespace {

std::uint32_t rotl(std::uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// True if the comma-separated header value contains `token` (case-insensitive).
bool has_token(std::string_view value, std::string_view token) {
    const std::string v = lower(value);
    std::string_view rest(v);
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        if (trim(rest.substr(0, comma)) == token) return true;
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    return false;
}

bool is_base64_key(std::string_view key) {
    // 16 random bytes, base64-encoded: 22 characters and "==".
    if (key.size() != 24 || key.substr(22) != "==") return false;
    return std::all_of(key.begin(), key.begin() + 22,
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '+' || c == '/'; });
}

std::string frame(std::uint8_t first_byte, std::string_view payload) {
    std::string out;
    out.reserve(payload.size() + 10);
    out.push_back(static_cast<char>(first_byte));
    const std::size_t n = payload.size();
    if (n < 126) {
        out.push_back(static_cast<char>(n));
    } else if (n <= 0xFFFF) {
        out.push_back(static_cast<char>(126));
        out.push_back(static_cast<char>((n >> 8U) & 0xFFU));
        out.push_back(static_cast<char>(n & 0xFFU));
    } else {
        out.push_back(static_cast<char>(127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<char>((static_cast<std::uint64_t>(n) >> static_cast<unsigned>(shift)) & 0xFFU));
        }
    }
    out.append(payload);
    return out;
}

}  // namespace

std::array<std::uint8_t, 20> sha1(std::string_view data) {
    std::uint32_t h0 = 0x67452301;
    std::uint32_t h1 = 0xEFCDAB89;
    std::uint32_t h2 = 0x98BADCFE;
    std::uint32_t h3 = 0x10325476;
    std::uint32_t h4 = 0xC3D2E1F0;
    std::string msg(data);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8U;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int shift = 56; shift >= 0; shift -= 8)
        msg.push_back(static_cast<char>((bits >> static_cast<unsigned>(shift)) & 0xFFU));

    std::array<std::uint32_t, 80> w{};
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        for (std::size_t i = 0; i < 16; ++i) {
            w.at(i) = 0;
            for (std::size_t b = 0; b < 4; ++b) {
                w.at(i) = (w.at(i) << 8U) | static_cast<std::uint8_t>(msg[chunk + 4 * i + b]);
            }
        }
        for (std::size_t i = 16; i < 80; ++i)
            w.at(i) = rotl(w.at(i - 3) ^ w.at(i - 8) ^ w.at(i - 14) ^ w.at(i - 16), 1);
        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const std::uint32_t t = rotl(a, 5) + f + e + k + w.at(i);
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = t;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    std::array<std::uint8_t, 20> out{};
    const std::array<std::uint32_t, 5> h{h0, h1, h2, h3, h4};
    for (std::size_t i = 0; i < 5; ++i) {
        for (std::size_t b = 0; b < 4; ++b) out.at(4 * i + b) = static_cast<std::uint8_t>(h.at(i) >> (24U - 8U * b));
    }
    return out;
}

std::string base64(const std::uint8_t* data, std::size_t size) {
    static constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16U) |
                                (i + 1 < size ? static_cast<std::uint32_t>(data[i + 1]) << 8U : 0U) |
                                (i + 2 < size ? static_cast<std::uint32_t>(data[i + 2]) : 0U);
        out.push_back(kAlphabet[(n >> 18U) & 63U]);
        out.push_back(kAlphabet[(n >> 12U) & 63U]);
        out.push_back(i + 1 < size ? kAlphabet[(n >> 6U) & 63U] : '=');
        out.push_back(i + 2 < size ? kAlphabet[n & 63U] : '=');
    }
    return out;
}

std::string accept_key(std::string_view client_key) {
    std::string s(client_key);
    s += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";  // RFC 6455, section 1.3
    const auto digest = sha1(s);
    return base64(digest.data(), digest.size());
}

Request parse_request(std::string_view buffer) {
    Request r;
    const auto end = buffer.find("\r\n\r\n");
    if (end == std::string_view::npos || end + 4 > kMaxRequest) {
        r.kind = buffer.size() >= kMaxRequest ? Request::Kind::Bad : Request::Kind::Incomplete;
        return r;
    }
    r.length = end + 4;
    const std::string_view head = buffer.substr(0, end);
    r.kind = Request::Kind::Bad;

    // Request line: GET <target> HTTP/1.1
    const auto eol = head.find("\r\n");
    const std::string_view line = head.substr(0, eol);
    const auto sp1 = line.find(' ');
    const auto sp2 = line.find(' ', sp1 == std::string_view::npos ? sp1 : sp1 + 1);
    if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) return r;
    if (line.substr(0, sp1) != "GET" || line.substr(sp2 + 1) != "HTTP/1.1") return r;
    const std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    if (target.empty() || target.front() != '/') return r;
    r.path = std::string(target.substr(0, target.find('?')));

    std::string upgrade;
    std::string connection;
    std::string key;
    std::string version;
    std::string_view rest = eol == std::string_view::npos ? std::string_view{} : head.substr(eol + 2);
    while (!rest.empty()) {
        const auto next = rest.find("\r\n");
        const std::string_view h = rest.substr(0, next);
        rest = next == std::string_view::npos ? std::string_view{} : rest.substr(next + 2);
        const auto colon = h.find(':');
        if (colon == std::string_view::npos || colon == 0 || h.front() == ' ' || h.front() == '\t') return r;
        const std::string name = lower(h.substr(0, colon));
        const std::string_view value = trim(h.substr(colon + 1));
        if (name == "upgrade") upgrade = value;
        if (name == "connection") connection = value;
        if (name == "sec-websocket-key") key = value;
        if (name == "sec-websocket-version") version = value;
    }
    if (upgrade.empty()) {
        r.kind = Request::Kind::Get;
        return r;
    }
    if (!has_token(upgrade, "websocket") || !has_token(connection, "upgrade") || !is_base64_key(key)) return r;
    if (version != "13") {
        r.kind = Request::Kind::WrongVersion;
        return r;
    }
    r.kind = Request::Kind::Upgrade;
    r.websocket_key = key;
    return r;
}

std::string upgrade_response(std::string_view client_key) {
    return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
           accept_key(client_key) + "\r\n\r\n";
}

std::string http_response(int status, std::string_view content_type, std::string_view body) {
    std::string reason = "Error";
    switch (status) {
        case 200:
            reason = "OK";
            break;
        case 400:
            reason = "Bad Request";
            break;
        case 404:
            reason = "Not Found";
            break;
        case 426:
            reason = "Upgrade Required";
            break;
        case 503:
            reason = "Service Unavailable";
            break;
        default:
            break;
    }
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    out += "Content-Type: " + std::string(content_type) + "\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "Connection: close\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\n";
    out +=
        "Content-Security-Policy: default-src 'self'; connect-src 'self'; img-src 'self' data:; style-src 'self'\r\n";
    if (status == 426) out += "Sec-WebSocket-Version: 13\r\n";
    out += "\r\n";
    out.append(body);
    return out;
}

std::string text_frame(std::string_view payload) {
    return frame(0x81, payload);
}
std::string pong_frame(std::string_view payload) {
    return frame(0x8A, payload.substr(0, 125));
}
std::string close_frame(std::uint16_t code) {
    const std::string body{static_cast<char>(code >> 8U), static_cast<char>(code & 0xFFU)};
    return frame(0x88, body);
}

std::optional<std::uint16_t> FrameParser::feed(std::string_view bytes, std::vector<Frame>& out) {
    if (failed_) return 1002;
    buffer_.append(bytes);
    const auto fail = [&](std::uint16_t code) -> std::optional<std::uint16_t> {
        failed_ = true;
        buffer_.clear();
        return code;
    };
    while (buffer_.size() >= 2) {
        const auto b0 = static_cast<std::uint8_t>(buffer_[0]);
        const auto b1 = static_cast<std::uint8_t>(buffer_[1]);
        const bool fin = (b0 & 0x80U) != 0;
        const unsigned opcode = b0 & 0x0FU;
        const bool masked = (b1 & 0x80U) != 0;
        if ((b0 & 0x70U) != 0 || !masked) return fail(1002);  // no extensions; clients must mask
        const bool control = opcode >= 8;
        if (opcode != 0 && opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 && opcode != 10) return fail(1002);
        std::uint64_t len = b1 & 0x7FU;
        std::size_t header = 2;
        if (len == 126) {
            if (buffer_.size() < 4) return std::nullopt;
            len = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(buffer_[2])) << 8U) |
                  static_cast<std::uint8_t>(buffer_[3]);
            header = 4;
        } else if (len == 127) {
            if (buffer_.size() < 10) return std::nullopt;
            len = 0;
            for (std::size_t i = 2; i < 10; ++i) len = (len << 8U) | static_cast<std::uint8_t>(buffer_[i]);
            header = 10;
        }
        if (control && (!fin || len > 125)) return fail(1002);
        if (!control && len > kMaxClientMessage - std::min<std::uint64_t>(message_bytes_, kMaxClientMessage)) {
            return fail(1009);
        }
        const std::size_t need = header + 4 + static_cast<std::size_t>(len);
        if (buffer_.size() < need) return std::nullopt;

        Frame f;
        f.opcode = static_cast<Opcode>(opcode);
        f.fin = fin;
        f.payload = buffer_.substr(header + 4, static_cast<std::size_t>(len));
        for (std::size_t i = 0; i < f.payload.size(); ++i) {
            f.payload[i] = static_cast<char>(f.payload[i] ^ buffer_[header + (i % 4)]);
        }
        buffer_.erase(0, need);

        if (!control) {
            // Fragmentation: a continuation needs a message in progress; a new
            // message must not start while one is.
            const bool in_message = continuing_;
            if (opcode == 0 && !in_message) return fail(1002);
            if (opcode != 0 && in_message) return fail(1002);
            message_bytes_ += f.payload.size();
            continuing_ = !fin;
            if (fin) message_bytes_ = 0;
        } else if (f.opcode == Opcode::Close && f.payload.size() == 1) {
            return fail(1002);
        }
        out.push_back(std::move(f));
    }
    return std::nullopt;
}

}  // namespace maritime::serve

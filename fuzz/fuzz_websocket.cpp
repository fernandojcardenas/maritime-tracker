// libFuzzer target for everything a browser (or anyone) can send the live-map
// server: HTTP requests and WebSocket frames, split at arbitrary points.
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "maritime/serve/websocket.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace maritime::serve;
    const std::string_view in(reinterpret_cast<const char*>(data),
                              size);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    const Request r = parse_request(in);
    if (r.kind == Request::Kind::Upgrade && r.websocket_key.size() != 24) __builtin_trap();
    if (r.length > in.size() || r.length > kMaxRequest) __builtin_trap();

    // The same bytes as a frame stream, fed in chunks whose sizes come from the data.
    FrameParser p;
    std::vector<Frame> frames;
    std::size_t pos = 0;
    while (pos < size) {
        const std::size_t chunk = 1 + (data[pos] % 64);
        const auto err = p.feed(in.substr(pos, chunk), frames);
        pos += chunk;
        if (err && *err != 1002 && *err != 1009) __builtin_trap();
        if (err) break;
    }
    for (const auto& f : frames) {
        const auto op = static_cast<unsigned>(f.opcode);
        if (op >= 8 && f.payload.size() > 125) __builtin_trap();
        if (op < 8 && f.payload.size() > kMaxClientMessage) __builtin_trap();
    }
    return 0;
}

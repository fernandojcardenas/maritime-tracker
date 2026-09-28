// libFuzzer target for the line framer and tag-block timestamp reader: the
// input is split into chunks at positions chosen by the fuzzer, as TCP reads
// would split it, and every framed line goes through tag_block_time().
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "maritime/ingest/line_framer.hpp"
#include "maritime/ingest/replay.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2) return 0;
    const std::size_t chunk = 1 + data[0] % 64;
    const std::size_t max_len = 8 + data[1];
    maritime::ingest::LineFramer framer(max_len);
    std::string_view input(reinterpret_cast<const char*>(data + 2), size - 2);
    std::size_t lines = 0;
    while (!input.empty()) {
        const auto piece = input.substr(0, chunk);
        framer.feed(piece, [&](std::string_view line) {
            if (line.size() > max_len) __builtin_trap();  // framer must enforce the limit
            (void)maritime::ingest::tag_block_time(line);
            ++lines;
        });
        input.remove_prefix(piece.size());
    }
    if (framer.buffered() > max_len) __builtin_trap();
    (void)lines;
    return 0;
}

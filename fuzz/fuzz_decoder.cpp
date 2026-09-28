// libFuzzer target: arbitrary bytes, split on newlines, fed through the
// stream decoder. Built with ASan + UBSan, so any out-of-bounds read,
// overflow or crash fails the run.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "maritime/ais/decoder.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    maritime::ais::Decoder decoder(8);
    std::string_view input(reinterpret_cast<const char*>(data), size);
    while (!input.empty()) {
        const auto nl = input.find('\n');
        const auto line = input.substr(0, nl);
        (void)decoder.feed(line);
        if (nl == std::string_view::npos) break;
        input.remove_prefix(nl + 1);
    }
    return 0;
}

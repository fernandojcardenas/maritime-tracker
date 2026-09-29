// libFuzzer target for the Danish AIS CSV row parser, which reads data
// downloaded from the internet.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "maritime/track/tracker.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view row(reinterpret_cast<const char*>(data), size);
    if (const auto f = maritime::track::fix_from_dk_csv(row)) {
        // Anything accepted must be a usable position.
        if (!(f->lat_deg >= -90.0 && f->lat_deg <= 90.0 && f->lon_deg >= -180.0 && f->lon_deg <= 180.0)) {
            __builtin_trap();
        }
        maritime::track::Tracker t;
        (void)t.add(*f);
    }
    return 0;
}

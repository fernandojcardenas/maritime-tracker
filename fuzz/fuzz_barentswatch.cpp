// libFuzzer target for the BarentsWatch JSON record parser, which reads data
// streamed from the internet.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "maritime/track/barentswatch.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view line(reinterpret_cast<const char*>(data), size);
    const auto r = maritime::track::parse_barentswatch(line);
    if (r.kind == maritime::track::BwParse::Fix) {
        const auto& f = *r.fix;
        if (!(f.lat_deg >= -90.0 && f.lat_deg <= 90.0 && f.lon_deg >= -180.0 && f.lon_deg <= 180.0) || f.mmsi == 0) {
            __builtin_trap();
        }
        maritime::track::Tracker t;
        (void)t.add(f);
    }
    return 0;
}

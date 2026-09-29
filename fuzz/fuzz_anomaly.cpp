// libFuzzer target for the anomaly detector: arbitrary sequences of reports,
// including out-of-order times, repeated identities, positions at the poles
// and the date line, and missing speeds. Every flag must carry finite values.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "maritime/anomaly/detector.hpp"

namespace {

template <class T>
T take(const std::uint8_t*& p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    p += sizeof v;
    return v;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t kRecord = 16;  // mmsi, time step, lat, lon, speed
    maritime::anomaly::Detector det;
    std::vector<maritime::anomaly::Anomaly> out;
    double t = 1.7e9;
    const std::uint8_t* p = data;
    for (std::size_t i = 0; i + kRecord <= size; i += kRecord) {
        maritime::track::Fix f;
        f.mmsi = take<std::uint32_t>(p) % 8U;  // few identities, so reports collide
        t += take<std::int16_t>(p);            // steps of up to +-9 hours, backwards too
        f.t = t;
        f.lat_deg = static_cast<double>(take<std::int32_t>(p)) / 2147483647.0 * 90.0;
        f.lon_deg = static_cast<double>(take<std::int32_t>(p)) / 2147483647.0 * 180.0;
        const auto sog = take<std::uint16_t>(p);
        if (sog != 0xFFFF) f.sog_knots = sog / 100.0;
        det.add(f, out);
        if (i % (64 * kRecord) == 0) (void)det.expire(t, 3600.0);
    }
    det.flush(out);
    for (const auto& a : out) {
        if (!std::isfinite(a.t) || !std::isfinite(a.value) || !std::isfinite(a.distance_m) || a.value < 0.0 ||
            a.distance_m < 0.0) {
            __builtin_trap();
        }
    }
    return 0;
}

// libFuzzer target for collision risk: arbitrary positions (poles and the
// date line included) and velocities. Every assessment must be finite, the
// roles must not depend on argument order, and in crossing and overtaking
// exactly one vessel gives way.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "maritime/risk/collision.hpp"

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
    using namespace maritime::risk;
    constexpr std::size_t kRecord = 14;  // lat, lon, v_east, v_north
    std::vector<Motion> fleet;
    const std::uint8_t* p = data;
    for (std::size_t i = 0; i + kRecord <= size && fleet.size() < 256; i += kRecord) {
        Motion m;
        m.mmsi = static_cast<std::uint32_t>(fleet.size());
        m.lat_deg = static_cast<double>(take<std::int32_t>(p)) / 2147483647.0 * 85.0;
        m.lon_deg = static_cast<double>(take<std::int32_t>(p)) / 2147483647.0 * 180.0;
        m.v_east = take<std::int16_t>(p) / 1000.0;  // up to about 64 kn
        m.v_north = take<std::int16_t>(p) / 1000.0;
        fleet.push_back(m);
    }
    for (std::size_t i = 0; i + 1 < fleet.size(); ++i) {
        const auto ab = classify(fleet[i], fleet[i + 1]);
        const auto ba = classify(fleet[i + 1], fleet[i]);
        if (!std::isfinite(ab.cpa.dcpa_m) || !std::isfinite(ab.cpa.tcpa_s) || ab.type != ba.type ||
            ab.role_a != ba.role_b || ab.role_b != ba.role_a) {
            __builtin_trap();
        }
        if ((ab.type == Encounter::Crossing || ab.type == Encounter::Overtaking) && ab.role_a == ab.role_b) {
            __builtin_trap();
        }
    }
    for (const auto& pa : find_encounters(fleet)) {
        if (pa.mmsi_a >= pa.mmsi_b || pa.assessment.cpa.dcpa_m > Params{}.max_dcpa_m) __builtin_trap();
    }
    return 0;
}

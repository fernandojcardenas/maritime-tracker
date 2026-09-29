// libFuzzer target for the pair search: arbitrary points (poles, date line,
// duplicates) and radii. Every method must return exactly what brute force
// returns.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "maritime/spatial/pairs.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace maritime::spatial;
    if (size < 4) return 0;
    std::uint32_t r = 0;
    std::memcpy(&r, data, sizeof r);
    const double radius_m = static_cast<double>(r % 50000000U);  // 0 m to beyond the far side of the Earth
    std::vector<Point> pts;
    for (std::size_t i = 4; i + 8 <= size && pts.size() < 400; i += 8) {
        std::int32_t la = 0;
        std::int32_t lo = 0;
        std::memcpy(&la, data + i, sizeof la);
        std::memcpy(&lo, data + i + 4, sizeof lo);
        pts.push_back({static_cast<double>(la) / 2147483647.0 * 90.0, static_cast<double>(lo) / 2147483647.0 * 180.0});
    }
    const auto reference = pairs_within(pts, radius_m, Method::BruteForce);
    for (const Method m : {Method::LatitudeSweep, Method::Grid, Method::KdTree}) {
        if (pairs_within(pts, radius_m, m) != reference) __builtin_trap();
    }
    return 0;
}

// Every pair of points within a given great-circle distance of each other.
// Collision risk (M5) needs this every minute for every vessel under way.
//
// Four methods, all returning exactly the same pairs:
//   BruteForce     every pair; the reference
//   LatitudeSweep  sort by latitude, compare only points within the radius
//                  north-south (what M5 shipped with)
//   Grid           hash points into cubes the size of the radius in
//                  Earth-centred 3D coordinates; compare with the 27 cubes around
//   KdTree         a static 3D k-d tree, one radius query per point
//
// Distances are chords through the Earth in Earth-centred coordinates: a chord
// is shorter than c exactly when the great-circle distance is shorter than r,
// for the matching c. Grid and tree need no special cases at the date line or
// the poles.
#pragma once

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace maritime::spatial {

struct Point {
    double lat_deg = 0.0;
    double lon_deg = 0.0;
};

enum class Method : std::uint8_t { BruteForce, LatitudeSweep, Grid, KdTree };

[[nodiscard]] std::string_view name(Method m) noexcept;

struct PairStats {
    std::uint64_t distance_checks = 0;  // candidate pairs whose distance was computed
    // Where the time went (wall clock, milliseconds).
    double convert_ms = 0.0;  // positions to Earth-centred coordinates
    double search_ms = 0.0;   // building the index and finding the pairs
    double sort_ms = 0.0;     // putting the pairs in order
};

// Index pairs (i, j), i < j, of points within radius_m of each other along the
// Earth's surface, sorted. `stats`, if given, receives the work done.
[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs_within(const std::vector<Point>& points,
                                                                                double radius_m, Method method,
                                                                                PairStats* stats = nullptr);

}  // namespace maritime::spatial

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <random>

#include "maritime/spatial/pairs.hpp"
#include "maritime/track/geo.hpp"

using namespace maritime;
using spatial::Method;
using spatial::Point;

namespace {

constexpr std::array<Method, 4> kAll{Method::BruteForce, Method::LatitudeSweep, Method::Grid, Method::KdTree};
constexpr double kSixNm = 6.0 * 1852.0;

// Every method must return exactly what brute force returns.
void expect_all_agree(const std::vector<Point>& pts, double radius_m, std::size_t expected_pairs) {
    const auto reference = spatial::pairs_within(pts, radius_m, Method::BruteForce);
    EXPECT_EQ(reference.size(), expected_pairs);
    for (const Method m : kAll) {
        EXPECT_EQ(spatial::pairs_within(pts, radius_m, m), reference) << spatial::name(m);
    }
}

std::vector<Point> clustered(std::size_t n, std::uint64_t seed, double lat0, double lon0) {
    std::mt19937_64 rng(seed);  // NOLINT(cert-msc32-c,cert-msc51-cpp): fixed seed
    std::normal_distribution<double> g(0.0, 1.0);
    std::uniform_int_distribution<int> which(0, 9);
    std::vector<Point> centres;
    for (int c = 0; c < 10; ++c) centres.push_back({lat0 + 0.5 * g(rng), lon0 + 0.5 * g(rng)});
    std::vector<Point> pts;
    for (std::size_t i = 0; i < n; ++i) {
        const Point& c = centres.at(static_cast<std::size_t>(which(rng)));
        pts.push_back({c.lat_deg + 0.08 * g(rng), c.lon_deg + 0.12 * g(rng)});
    }
    return pts;
}

}  // namespace

TEST(Spatial, AllMethodsAgreeOnClusteredTraffic) {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        const auto pts = clustered(800, seed, 55.8, 12.6);
        const auto ref = spatial::pairs_within(pts, kSixNm, Method::BruteForce);
        ASSERT_GT(ref.size(), 1000U);
        for (const Method m : kAll) ASSERT_EQ(spatial::pairs_within(pts, kSixNm, m), ref) << spatial::name(m);
    }
}

TEST(Spatial, AllMethodsAgreeAtSmallAndLargeRadii) {
    const auto pts = clustered(500, 9, -33.9, 18.4);
    for (const double r : {1.0, 100.0, 926.0, 50000.0, 500000.0}) {
        const auto ref = spatial::pairs_within(pts, r, Method::BruteForce);
        for (const Method m : kAll) ASSERT_EQ(spatial::pairs_within(pts, r, m), ref) << spatial::name(m) << " r=" << r;
    }
}

TEST(Spatial, PairsAcrossTheDateLine) {
    // 0.02 degrees of longitude apart across 180 E/W at 60 N: about 1.1 km.
    const std::vector<Point> pts{{60.0, 179.99}, {60.0, -179.99}, {60.0, 170.0}};
    expect_all_agree(pts, kSixNm, 1);
}

TEST(Spatial, PairsNearThePole) {
    // Near 90 N, points far apart in longitude are close together.
    const std::vector<Point> pts{{89.99, 0.0}, {89.99, 90.0}, {89.99, 180.0}, {89.99, -90.0}, {80.0, 0.0}};
    expect_all_agree(pts, kSixNm, 6);
}

TEST(Spatial, DuplicatesAndEdgeCases) {
    expect_all_agree({}, kSixNm, 0);
    expect_all_agree({{55.0, 12.0}}, kSixNm, 0);
    const std::vector<Point> same(50, Point{55.0, 12.0});
    expect_all_agree(same, kSixNm, 50 * 49 / 2);
    expect_all_agree(same, 0.0, 50 * 49 / 2);  // identical positions are 0 m apart
    const auto spread = clustered(300, 3, 10.0, -60.0);
    expect_all_agree(spread, 2.1e7, 300 * 299 / 2);  // more than half the Earth: every pair
}

TEST(Spatial, RadiusBoundaryMatchesGreatCircleDistance) {
    const Point a{56.0, 11.0};
    Point b{56.0, 11.0};
    const track::LocalFrame f(a.lat_deg, a.lon_deg);
    f.to_geo({8000.0, 6000.0}, b.lat_deg, b.lon_deg);
    const double d = track::haversine_m(a.lat_deg, a.lon_deg, b.lat_deg, b.lon_deg);
    for (const Method m : kAll) {
        EXPECT_EQ(spatial::pairs_within({a, b}, d + 0.01, m).size(), 1U) << spatial::name(m);
        EXPECT_EQ(spatial::pairs_within({a, b}, d - 0.01, m).size(), 0U) << spatial::name(m);
    }
}

TEST(Spatial, IndexesCheckFarFewerPairsOnSpreadTraffic) {
    // Traffic spread along a coast, like the Norwegian live feed.
    std::vector<Point> pts;
    std::mt19937_64 rng(4);  // NOLINT(cert-msc32-c,cert-msc51-cpp): fixed seed
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int i = 0; i < 4000; ++i) pts.push_back({58.0 + 20.0 * u(rng), 4.0 + 26.0 * u(rng)});
    spatial::PairStats brute;
    spatial::PairStats sweep;
    spatial::PairStats grid;
    spatial::PairStats tree;
    const auto ref = spatial::pairs_within(pts, kSixNm, Method::BruteForce, &brute);
    EXPECT_EQ(spatial::pairs_within(pts, kSixNm, Method::LatitudeSweep, &sweep), ref);
    EXPECT_EQ(spatial::pairs_within(pts, kSixNm, Method::Grid, &grid), ref);
    EXPECT_EQ(spatial::pairs_within(pts, kSixNm, Method::KdTree, &tree), ref);
    EXPECT_EQ(brute.distance_checks, 4000ULL * 3999 / 2);
    EXPECT_LT(sweep.distance_checks * 50, brute.distance_checks);
    EXPECT_LT(grid.distance_checks * 10, sweep.distance_checks);
    EXPECT_LT(tree.distance_checks * 2, sweep.distance_checks);  // leaves of 8 points are wide in sparse traffic
}

TEST(Spatial, MatchesAnIndependentGreatCircleCheckAndIsSorted) {
    // Reference that shares no code with pairs_within: haversine distances.
    const auto pts = clustered(600, 11, 63.0, 8.0);
    const double r = kSixNm;
    for (const Method m : kAll) {
        const auto got = spatial::pairs_within(pts, r, m);
        for (std::size_t k = 1; k < got.size(); ++k) ASSERT_LT(got[k - 1], got[k]) << spatial::name(m);
        std::size_t g = 0;
        for (std::uint32_t i = 0; i < pts.size(); ++i) {
            for (std::uint32_t j = i + 1; j < pts.size(); ++j) {
                const double d = track::haversine_m(pts[i].lat_deg, pts[i].lon_deg, pts[j].lat_deg, pts[j].lon_deg);
                const bool listed = g < got.size() && got[g] == std::make_pair(i, j);
                if (listed) ++g;
                if (d < r - 0.01) ASSERT_TRUE(listed) << spatial::name(m) << " misses " << i << "," << j;
                if (d > r + 0.01) ASSERT_FALSE(listed) << spatial::name(m) << " wrongly lists " << i << "," << j;
            }
        }
        ASSERT_EQ(g, got.size()) << spatial::name(m);
    }
}

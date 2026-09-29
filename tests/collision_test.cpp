#include "maritime/risk/collision.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <random>

#include "maritime/track/geo.hpp"

using namespace maritime;
using risk::Encounter;
using risk::Motion;
using risk::Role;

namespace {

constexpr double kNm = risk::kNauticalMileM;

// A vessel `east_m`/`north_m` metres from 55 N 12 E, on `course` at `kn`.
Motion vessel(std::uint32_t mmsi, double east_m, double north_m, double course, double kn) {
    static const track::LocalFrame frame(55.0, 12.0);
    Motion m;
    m.mmsi = mmsi;
    frame.to_geo({east_m, north_m}, m.lat_deg, m.lon_deg);
    const double rad = course * std::numbers::pi / 180.0;
    m.v_east = kn * track::kKnotToMs * std::sin(rad);
    m.v_north = kn * track::kKnotToMs * std::cos(rad);
    return m;
}

// A vessel at `bearing` degrees (true) and `range_m` from the origin.
Motion at_bearing(std::uint32_t mmsi, double bearing, double range_m, double course, double kn) {
    const double rad = bearing * std::numbers::pi / 180.0;
    return vessel(mmsi, range_m * std::sin(rad), range_m * std::cos(rad), course, kn);
}

}  // namespace

TEST(Cpa, CollisionCourse) {
    const auto a = vessel(1, 0, 0, 0, 10);        // north at 10 kn
    const auto b = vessel(2, kNm, kNm, 270, 10);  // west at 10 kn, meets a at (0, 1 nm)
    const auto c = risk::closest_approach(a, b);
    EXPECT_NEAR(c.range_m, std::sqrt(2.0) * kNm, 5);
    EXPECT_NEAR(c.tcpa_s, kNm / (10 * track::kKnotToMs), 1);
    EXPECT_NEAR(c.dcpa_m, 0, 5);
}

TEST(Cpa, PassingAtAKnownDistance) {
    const auto a = vessel(1, 0, 0, 0, 12);
    const auto b = vessel(2, 400, 3 * kNm, 180, 12);  // reciprocal, 400 m to starboard
    const auto c = risk::closest_approach(a, b);
    EXPECT_NEAR(c.dcpa_m, 400, 2);
    EXPECT_NEAR(c.tcpa_s, 3 * kNm / (24 * track::kKnotToMs), 1);
}

TEST(Cpa, SameVelocityAndOpening) {
    const auto a = vessel(1, 0, 0, 90, 10);
    const auto same = vessel(2, 0, 1000, 90, 10);
    auto c = risk::closest_approach(a, same);
    EXPECT_DOUBLE_EQ(c.tcpa_s, 0.0);
    EXPECT_NEAR(c.dcpa_m, 1000, 1);
    const auto opening = vessel(3, 0, 1000, 0, 10);  // ahead and going away faster
    c = risk::closest_approach(a, opening);
    EXPECT_LT(c.tcpa_s, 0.0);
    EXPECT_NEAR(c.dcpa_m, c.range_m, 1e-9);
}

TEST(Colregs, HeadOnBothGiveWay) {
    const auto a = vessel(1, 0, 0, 0, 10);
    const auto b = vessel(2, 100, 2 * kNm, 181, 10);
    const auto r = risk::classify(a, b);
    EXPECT_EQ(r.type, Encounter::HeadOn);
    EXPECT_EQ(r.role_a, Role::GiveWay);
    EXPECT_EQ(r.role_b, Role::GiveWay);
}

TEST(Colregs, CrossingTheVesselWithTheOtherToStarboardGivesWay) {
    const auto a = vessel(1, 0, 0, 0, 10);
    const auto from_starboard = vessel(2, kNm, kNm, 270, 10);
    auto r = risk::classify(a, from_starboard);
    EXPECT_EQ(r.type, Encounter::Crossing);
    EXPECT_EQ(r.role_a, Role::GiveWay);
    EXPECT_EQ(r.role_b, Role::StandOn);

    const auto from_port = vessel(3, -kNm, kNm, 90, 10);
    r = risk::classify(a, from_port);
    EXPECT_EQ(r.type, Encounter::Crossing);
    EXPECT_EQ(r.role_a, Role::StandOn);
    EXPECT_EQ(r.role_b, Role::GiveWay);
}

TEST(Colregs, OvertakingVesselGivesWay) {
    const auto a = vessel(1, 0, 0, 0, 8);
    const auto b = vessel(2, 50, -kNm, 0, 15);  // from astern, faster
    const auto r = risk::classify(a, b);
    EXPECT_EQ(r.type, Encounter::Overtaking);
    EXPECT_EQ(r.role_a, Role::StandOn);
    EXPECT_EQ(r.role_b, Role::GiveWay);
}

TEST(Colregs, OvertakingIsDecidedByTheSectorAbaftTheBeam) {
    // Rule 13(b): coming up from more than 22.5 degrees abaft the beam is
    // overtaking even on a converging course; just forward of it is crossing.
    const auto a = vessel(1, 0, 0, 0, 8);
    auto r = risk::classify(a, at_bearing(2, 112.6, kNm, 0, 15));
    EXPECT_EQ(r.type, Encounter::Overtaking);
    EXPECT_EQ(r.role_b, Role::GiveWay);
    r = risk::classify(a, at_bearing(2, 112.4, kNm, 0, 15));
    EXPECT_EQ(r.type, Encounter::Crossing);
    EXPECT_EQ(r.role_a, Role::GiveWay);  // b is on a's starboard side
}

TEST(Colregs, NearlyReciprocalButWellOffTheBowIsNotHeadOn) {
    const auto a = vessel(1, 0, 0, 0, 10);
    // Reciprocal-ish, 40 degrees on the bow: each has the other to starboard,
    // so they pass starboard to starboard and no rule assigns roles.
    auto r = risk::classify(a, at_bearing(2, 40, kNm, 185, 10));
    EXPECT_EQ(r.type, Encounter::Unclear);
    // Same position, heading across a's bow: crossing, a gives way.
    r = risk::classify(a, at_bearing(2, 40, kNm, 250, 10));
    EXPECT_EQ(r.type, Encounter::Crossing);
    EXPECT_EQ(r.role_a, Role::GiveWay);
}

TEST(Colregs, RolesDoNotDependOnArgumentOrder) {
    std::mt19937_64 rng(7);  // NOLINT(cert-msc32-c,cert-msc51-cpp): fixed seed
    std::uniform_real_distribution<double> deg(0, 360);
    std::uniform_real_distribution<double> range(200, 6 * kNm);
    std::uniform_real_distribution<double> kn(2, 25);
    int crossing_or_overtaking = 0;
    for (int i = 0; i < 20000; ++i) {
        const auto a = vessel(1, 0, 0, deg(rng), kn(rng));
        const auto b = at_bearing(2, deg(rng), range(rng), deg(rng), kn(rng));
        const auto ab = risk::classify(a, b);
        const auto ba = risk::classify(b, a);
        ASSERT_EQ(ab.type, ba.type) << i;
        ASSERT_EQ(ab.role_a, ba.role_b) << i;
        ASSERT_EQ(ab.role_b, ba.role_a) << i;
        ASSERT_NEAR(ab.cpa.dcpa_m, ba.cpa.dcpa_m, 1e-6) << i;
        ASSERT_NEAR(ab.cpa.tcpa_s, ba.cpa.tcpa_s, 1e-6) << i;
        if (ab.type == Encounter::Crossing || ab.type == Encounter::Overtaking) {
            ++crossing_or_overtaking;
            ASSERT_NE(ab.role_a, ab.role_b) << "exactly one vessel gives way, case " << i;
        }
    }
    EXPECT_GT(crossing_or_overtaking, 10000);
}

TEST(Colregs, AssessOnlyReportsPairsAtRisk) {
    const auto a = vessel(1, 0, 0, 0, 10);
    EXPECT_TRUE(risk::assess(a, vessel(2, kNm, kNm, 270, 10)));           // collision course
    EXPECT_FALSE(risk::assess(a, vessel(2, kNm, kNm, 270, 1)));           // not under way
    EXPECT_FALSE(risk::assess(a, vessel(2, 2 * kNm, 3 * kNm, 180, 10)));  // passes 2 nm off
    EXPECT_FALSE(risk::assess(a, vessel(2, 0, 7 * kNm, 180, 10)));        // out of range
    EXPECT_FALSE(risk::assess(a, vessel(2, 0, -kNm, 180, 10)));           // opening
    EXPECT_FALSE(risk::assess(a, vessel(2, 0, -2 * kNm, 0, 14)));         // closing at 4 kn: CPA in 30 min
}

TEST(Colregs, FindEncountersReturnsEachRiskyPairOnce) {
    std::vector<Motion> fleet{
        vessel(30, 0, 0, 0, 10), vessel(10, kNm, kNm, 270, 10),  // crossing 30
        vessel(20, 30 * kNm, 0, 0, 10),                          // same latitude, far east: no risk
        vessel(40, 0, 3 * kNm, 180, 10),                         // head-on with 30
    };
    const auto found = risk::find_encounters(fleet);
    ASSERT_EQ(found.size(), 2U);
    EXPECT_EQ(found[0].mmsi_a, 10U);
    EXPECT_EQ(found[0].mmsi_b, 30U);
    EXPECT_EQ(found[0].assessment.type, Encounter::Crossing);
    EXPECT_EQ(found[0].assessment.role_b, Role::GiveWay);  // 30 has 10 on its starboard side
    EXPECT_EQ(found[1].mmsi_a, 30U);
    EXPECT_EQ(found[1].mmsi_b, 40U);
    EXPECT_EQ(found[1].assessment.type, Encounter::HeadOn);
}

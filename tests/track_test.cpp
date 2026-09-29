#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "maritime/track/geo.hpp"
#include "maritime/track/kalman.hpp"
#include "maritime/track/tracker.hpp"

using namespace maritime::track;

// ---------- geo ----------

TEST(Geo, HaversineOneDegreeOfLatitude) {
    EXPECT_NEAR(haversine_m(55.0, 12.0, 56.0, 12.0), 111195.0, 1.0);
    EXPECT_NEAR(haversine_m(55.0, 12.0, 55.0, 12.0), 0.0, 1e-9);
}

TEST(Geo, LocalFrameRoundTripsAndMatchesHaversine) {
    const LocalFrame f(55.6, 12.6);
    const auto p = f.to_local(55.61, 12.62);
    double lat = 0.0;
    double lon = 0.0;
    f.to_geo(p, lat, lon);
    EXPECT_NEAR(lat, 55.61, 1e-9);
    EXPECT_NEAR(lon, 12.62, 1e-9);
    // Within a couple of kilometres the flat approximation agrees with the sphere to well under a metre.
    EXPECT_NEAR(std::hypot(p.east_m, p.north_m), haversine_m(55.6, 12.6, 55.61, 12.62), 0.5);
}

TEST(Geo, LocalFrameHandlesAntimeridian) {
    const LocalFrame f(0.0, 179.99);
    const auto p = f.to_local(0.0, -179.99);
    EXPECT_NEAR(p.east_m, 0.02 * 111195.0, 5.0);
}

TEST(Geo, VelocityFromSogCog) {
    const auto east = velocity_from_sog_cog(10.0, 90.0);
    EXPECT_NEAR(east.east_m, 10.0 * 1852.0 / 3600.0, 1e-9);
    EXPECT_NEAR(east.north_m, 0.0, 1e-9);
    const auto south = velocity_from_sog_cog(4.0, 180.0);
    EXPECT_NEAR(south.north_m, -4.0 * 1852.0 / 3600.0, 1e-9);
}

// ---------- Danish CSV ----------

TEST(DkCsv, ParsesRowAndTime) {
    const auto f = fix_from_dk_csv("22/04/2026 12:00:00,Class A,219000001,55.700000,12.650000,12.4,80.9,77,Passenger");
    ASSERT_TRUE(f);
    EXPECT_EQ(f->mmsi, 219000001U);
    EXPECT_DOUBLE_EQ(f->t, 1776859200.0);  // 2026-04-22T12:00:00Z
    EXPECT_DOUBLE_EQ(f->lat_deg, 55.7);
    EXPECT_DOUBLE_EQ(*f->sog_knots, 12.4);
    EXPECT_DOUBLE_EQ(*f->cog_deg, 80.9);
}

TEST(DkCsv, MissingOrInvalidSpeedLeavesVelocityEmpty) {
    const auto f = fix_from_dk_csv("22/04/2026 12:00:01,Class B,219000002,55.7,12.6,,,,Pleasure");
    ASSERT_TRUE(f);
    EXPECT_FALSE(f->sog_knots);
    const auto g = fix_from_dk_csv("22/04/2026 12:00:01,Class A,219000002,55.7,12.6,102.3,360.0,511,");
    ASSERT_TRUE(g);
    EXPECT_FALSE(g->sog_knots);
}

TEST(DkCsv, RejectsMalformedRows) {
    EXPECT_FALSE(fix_from_dk_csv("# Timestamp,Type of mobile,MMSI,Latitude,Longitude"));
    EXPECT_FALSE(fix_from_dk_csv("22/04/2026 12:00:00,Class A,notanumber,55.7,12.6,1,2,3,x"));
    EXPECT_FALSE(fix_from_dk_csv("22/13/2026 12:00:00,Class A,1,55.7,12.6,1,2,3,x"));
    EXPECT_FALSE(fix_from_dk_csv("22/04/2026 12:00:00,Class A,1,91.0,12.6,1,2,3,x"));
    EXPECT_FALSE(fix_from_dk_csv(""));
}

// ---------- Kalman filter ----------

namespace {

// A vessel steaming at constant velocity, observed with Gaussian noise.
struct Straight {
    double ve = 3.0;
    double vn = -2.0;
    std::mt19937_64 rng{7};
    std::normal_distribution<double> n{0.0, 1.0};
    Measurement at(double t, double pos_sigma, bool with_velocity) {
        Measurement m;
        m.east_m = ve * t + pos_sigma * n(rng);
        m.north_m = vn * t + pos_sigma * n(rng);
        m.pos_sigma_m = pos_sigma;
        if (with_velocity) {
            m.v_east = ve + 0.1 * n(rng);
            m.v_north = vn + 0.1 * n(rng);
        }
        return m;
    }
};

}  // namespace

TEST(Kalman, LearnsVelocityFromPositionsAlone) {
    Straight s;
    KalmanCV kf(s.at(0, 15.0, false), {});
    for (int k = 1; k <= 60; ++k) {
        kf.predict(10.0);
        kf.update(s.at(10.0 * k, 15.0, false));
    }
    // The error is consistent with the filter's own uncertainty (within 3 sigma),
    // and that uncertainty is far below the 5 m/s it started with.
    const double sv = std::sqrt(kf.covariance()[2][2]);
    EXPECT_LT(sv, 1.0);  // steady state is about 0.7 m/s for 15 m noise every 10 s
    EXPECT_NEAR(kf.state()[2], s.ve, 3.0 * sv);
    EXPECT_NEAR(kf.state()[3], s.vn, 3.0 * std::sqrt(kf.covariance()[3][3]));
    // Filtered position is closer to the truth than a single 15 m measurement typically is.
    EXPECT_LT(std::hypot(kf.state()[0] - s.ve * 600, kf.state()[1] - s.vn * 600), 15.0);
}

TEST(Kalman, VelocityMeasurementsConvergeImmediately) {
    Straight s;
    KalmanCV kf(s.at(0, 15.0, true), {});
    kf.predict(10.0);
    kf.update(s.at(10.0, 15.0, true));
    EXPECT_NEAR(kf.state()[2], s.ve, 0.3);
    EXPECT_NEAR(kf.state()[3], s.vn, 0.3);
}

TEST(Kalman, CovarianceStaysSymmetricPositiveDefinite) {
    Straight s;
    KalmanCV kf(s.at(0, 5.0, true), {});
    for (int k = 1; k <= 5000; ++k) {
        kf.predict(k % 7 == 0 ? 300.0 : 2.0);
        kf.update(s.at(2.0 * k, 5.0, k % 3 != 0));
    }
    const auto& p = kf.covariance();
    for (int i = 0; i < 4; ++i) {
        EXPECT_GT(p[i][i], 0.0);
        for (int j = 0; j < 4; ++j) EXPECT_NEAR(p[i][j], p[j][i], 1e-9);
    }
    // 2x2 leading minor positive: a proper position covariance.
    EXPECT_GT(p[0][0] * p[1][1] - p[0][1] * p[1][0], 0.0);
}

TEST(Kalman, PredictGrowsUncertainty) {
    Straight s;
    KalmanCV kf(s.at(0, 10.0, true), {});
    const double before = kf.covariance()[0][0];
    kf.predict(600.0);
    EXPECT_GT(kf.covariance()[0][0], before);
}

// ---------- Tracker ----------

namespace {

Fix fix(std::uint32_t mmsi, double t, double lat, double lon, std::optional<double> sog = 10.0,
        std::optional<double> cog = 90.0) {
    Fix f;
    f.mmsi = mmsi;
    f.t = t;
    f.lat_deg = lat;
    f.lon_deg = lon;
    f.sog_knots = sog;
    f.cog_deg = cog;
    f.pos_sigma_m = 10.0;
    return f;
}

// Position of a vessel doing 10 kn due east from (55.6, 12.0) after t seconds.
Fix eastbound(std::uint32_t mmsi, double t) {
    const LocalFrame fr(55.6, 12.0);
    double lat = 0.0;
    double lon = 0.0;
    fr.to_geo({10.0 * kKnotToMs * t, 0.0}, lat, lon);
    return fix(mmsi, t, lat, lon);
}

}  // namespace

TEST(Tracker, FollowsSteadyVesselAndPredicts) {
    Tracker tr;
    for (int k = 0; k <= 30; ++k) tr.add(eastbound(1, 10.0 * k));
    const auto p = tr.predict(1, 400.0);
    ASSERT_TRUE(p);
    const auto truth = eastbound(1, 400.0);
    EXPECT_LT(haversine_m(p->lat_deg, p->lon_deg, truth.lat_deg, truth.lon_deg), 5.0);
    EXPECT_EQ(tr.stats().updated, 30U);
}

TEST(Tracker, RejectsOutlierThenRestartsAfterRepeatedRejects) {
    Tracker tr;
    for (int k = 0; k <= 10; ++k) tr.add(eastbound(1, 10.0 * k));
    auto jump = eastbound(1, 110.0);
    jump.lat_deg += 0.05;  // about 5.5 km north: implausible
    EXPECT_EQ(tr.add(jump), FixOutcome::Rejected);
    // The next genuine report is accepted and the track is intact.
    EXPECT_EQ(tr.add(eastbound(1, 120.0)), FixOutcome::Updated);

    // If the vessel really is somewhere else, the track restarts after 3 rejections.
    for (int k = 0; k < 2; ++k) {
        auto far = eastbound(1, 130.0 + 10.0 * k);
        far.lat_deg += 0.05;
        EXPECT_EQ(tr.add(far), FixOutcome::Rejected);
    }
    auto far = eastbound(1, 150.0);
    far.lat_deg += 0.05;
    EXPECT_EQ(tr.add(far), FixOutcome::Restarted);
    EXPECT_EQ(tr.stats().restarted, 1U);
}

TEST(Tracker, DuplicateAndOutOfOrderReports) {
    Tracker tr;
    tr.add(eastbound(1, 100.0));
    tr.add(eastbound(1, 110.0));
    EXPECT_EQ(tr.add(eastbound(1, 110.2)), FixOutcome::Duplicate);
    EXPECT_EQ(tr.add(eastbound(1, 90.0)), FixOutcome::OutOfOrder);
}

TEST(Tracker, SeparateTracksPerVesselAndExpiry) {
    TrackerParams p;
    p.stale_after_s = 600.0;
    Tracker tr(p);
    tr.add(eastbound(1, 0.0));
    tr.add(eastbound(2, 500.0));
    EXPECT_EQ(tr.size(), 2U);
    EXPECT_EQ(tr.expire(700.0), 1U);  // vessel 1 silent for 700 s
    EXPECT_FALSE(tr.track(1));
    EXPECT_TRUE(tr.track(2));
}

TEST(Tracker, ReanchorsLongTracksWithoutLosingPosition) {
    Tracker tr;
    // 10 kn for 2.5 hours is about 46 km: at least two re-anchors.
    for (int k = 0; k <= 900; ++k) tr.add(eastbound(1, 10.0 * k));
    EXPECT_GE(tr.stats().reanchored, 2U);
    const auto v = tr.track(1);
    ASSERT_TRUE(v);
    const auto truth = eastbound(1, 9000.0);
    EXPECT_LT(haversine_m(v->lat_deg, v->lon_deg, truth.lat_deg, truth.lon_deg), 5.0);
    EXPECT_NEAR(v->v_east, 10.0 * kKnotToMs, 0.05);
}

TEST(Tracker, FixFromDecodedMessageNeedsTime) {
    maritime::ais::PositionReportA a;
    a.mmsi = 257000001;
    a.position = maritime::ais::Position{59.9, 10.7};
    a.sog_knots = 8.0;
    a.cog_deg = 45.0;
    a.position_accuracy = true;
    maritime::ais::DecodedMessage dm{maritime::ais::Message{a}, std::nullopt, std::nullopt, 'A'};
    EXPECT_FALSE(fix_from(dm));
    dm.unix_time = 1776859200;
    const auto f = fix_from(dm);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->mmsi, 257000001U);
    EXPECT_DOUBLE_EQ(f->pos_sigma_m, 10.0);
    EXPECT_DOUBLE_EQ(*f->sog_knots, 8.0);
}

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "maritime/anomaly/detector.hpp"
#include "maritime/track/barentswatch.hpp"
#include "maritime/track/geo.hpp"

using namespace maritime;
using anomaly::Anomaly;
using anomaly::Detector;
using anomaly::Kind;
using track::Fix;

namespace {

constexpr double kT0 = 1.7e9;
constexpr double kKn = track::kKnotToMs;

// A report `east_m`/`north_m` metres from 55 N 12 E.
Fix at(std::uint32_t mmsi, double t, double east_m, double north_m, std::optional<double> sog = std::nullopt) {
    static const track::LocalFrame frame(55.0, 12.0);
    Fix f;
    f.mmsi = mmsi;
    f.t = t;
    frame.to_geo({east_m, north_m}, f.lat_deg, f.lon_deg);
    f.sog_knots = sog;
    if (sog) f.cog_deg = 0.0;
    return f;
}

// A vessel heading north at `kn`, reporting every `every` seconds in [t0, t1).
void steaming(std::vector<Fix>& out, std::uint32_t mmsi, double t0, double t1, double every, double kn,
              double east_m = 0.0) {
    for (double t = t0; t < t1; t += every) out.push_back(at(mmsi, t, east_m, (t - t0) * kn * kKn, kn));
}

// Other traffic, so the receiver network is visibly working: 20 moored
// vessels, one report each every 60 s, spread across each minute.
void background(std::vector<Fix>& out, double t0, double t1) {
    for (std::uint32_t v = 0; v < 20; ++v) {
        for (double t = t0 + 3.0 * v; t < t1; t += 60.0)
            out.push_back(at(900000000U + v, t, 50000.0 + 100.0 * v, 0.0, 0.0));
    }
}

std::vector<Anomaly> run(std::vector<Fix> fixes, Detector& det) {
    std::stable_sort(fixes.begin(), fixes.end(), [](const Fix& a, const Fix& b) { return a.t < b.t; });
    std::vector<Anomaly> out;
    for (const auto& f : fixes) det.add(f, out);
    det.flush(out);
    return out;
}

std::vector<Anomaly> run(std::vector<Fix> fixes) {
    Detector det;
    return run(std::move(fixes), det);
}

std::size_t count(const std::vector<Anomaly>& v, Kind k) {
    return static_cast<std::size_t>(std::count_if(v.begin(), v.end(), [&](const Anomaly& a) { return a.kind == k; }));
}

}  // namespace

TEST(Anomaly, SteadyTrafficRaisesNothing) {
    std::vector<Fix> fixes;
    background(fixes, kT0, kT0 + 3600);
    steaming(fixes, 1, kT0, kT0 + 3600, 10, 12);
    steaming(fixes, 2, kT0, kT0 + 3600, 30, 35, 20000);  // a fast ferry
    EXPECT_TRUE(run(fixes).empty());
}

TEST(Anomaly, ReportedSpeedAboveTheLimit) {
    std::vector<Fix> fixes;
    steaming(fixes, 1, kT0, kT0 + 60, 10, 10);
    fixes.push_back(at(1, kT0 + 60, 0, 60 * 10 * kKn, 50.0));  // at the limit: fine
    fixes.push_back(at(1, kT0 + 70, 0, 70 * 10 * kKn, 64.0));
    const auto found = run(fixes);
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found[0].kind, Kind::ImpossibleSpeed);
    EXPECT_DOUBLE_EQ(found[0].value, 64.0);
    EXPECT_DOUBLE_EQ(found[0].t, kT0 + 70);
}

TEST(Anomaly, SingleDisplacedReportIsFlaggedOnce) {
    std::vector<Fix> fixes;
    steaming(fixes, 1, kT0, kT0 + 600, 10, 10);
    Fix& bad = fixes[30];
    bad = at(1, bad.t, 5000, (bad.t - kT0) * 10 * kKn, 10.0);  // 5 km east of where it should be
    const auto found = run(fixes);
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found[0].kind, Kind::PositionJump);
    EXPECT_DOUBLE_EQ(found[0].t, kT0 + 300);
    EXPECT_NEAR(found[0].distance_m, 5000, 60);  // from the previous report, 51 m behind
}

TEST(Anomaly, SpeedAwareLimitCatchesJumpsBetweenSparseReports) {
    // A moored vessel reporting 0 kn every 3 minutes. At 50 kn it could cover
    // 4.6 km in 3 minutes, but not at 0 kn + 15 kn.
    std::vector<Fix> fixes;
    for (int i = 0; i < 10; ++i) fixes.push_back(at(1, kT0 + 180.0 * i, 0, 0, 0.0));
    fixes.push_back(at(1, kT0 + 1800, 3000, 0, 0.0));
    const auto found = run(fixes);
    ASSERT_EQ(count(found, Kind::PositionJump), 1U);
    EXPECT_DOUBLE_EQ(found[0].t, kT0 + 1800);
}

TEST(Anomaly, OutAndBackBlamesTheOddReport) {
    // No speed in the reports (common on Class B), so only the 50 kn limit
    // applies: a 3 km spike after 3 minutes is reachable, the return 30 s
    // later is not. The flag goes on the spike, not on the return.
    std::vector<Fix> fixes;
    for (int i = 0; i < 5; ++i) fixes.push_back(at(1, kT0 + 180.0 * i, 0, 0));
    fixes.push_back(at(1, kT0 + 900, 3000, 0));  // the spike
    fixes.push_back(at(1, kT0 + 930, 0, 0));
    fixes.push_back(at(1, kT0 + 1110, 0, 0));
    const auto found = run(fixes);
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found[0].kind, Kind::PositionJump);
    EXPECT_DOUBLE_EQ(found[0].t, kT0 + 900);
    EXPECT_NEAR(found[0].distance_m, 3000, 10);
}

TEST(Anomaly, GapWhileMovingAndNetworkWorking) {
    std::vector<Fix> fixes;
    background(fixes, kT0, kT0 + 7200);
    steaming(fixes, 1, kT0, kT0 + 1800, 10, 12);
    steaming(fixes, 1, kT0 + 1800 + 900, kT0 + 3600, 10, 12);  // 15 minutes of silence
    const auto found = run(fixes);
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found[0].kind, Kind::Gap);
    EXPECT_NEAR(found[0].value, 910, 1);
    EXPECT_DOUBLE_EQ(found[0].t, kT0 + 2700);
}

TEST(Anomaly, NoGapWhenStationaryOrNotHeardRegularly) {
    std::vector<Fix> fixes;
    background(fixes, kT0, kT0 + 7200);
    // Moored: 0 kn when it went quiet.
    for (double t = kT0; t < kT0 + 1800; t += 10) fixes.push_back(at(1, t, 0, 0, 0.0));
    fixes.push_back(at(1, kT0 + 3600, 0, 0, 0.0));
    // Moving, but heard only every 20 minutes (edge of coverage, satellite).
    for (int i = 0; i < 5; ++i) fixes.push_back(at(2, kT0 + 1200.0 * i, 10000, 1200.0 * i * 10 * kKn, 10.0));
    Detector det;
    EXPECT_TRUE(run(fixes, det).empty());
    EXPECT_GE(det.stats().gaps_not_regular, 3U);
}

TEST(Anomaly, NetworkOutageDoesNotCountAsSilence) {
    // The vessel is silent for 12 minutes, 8 of which nobody was heard at all.
    std::vector<Fix> fixes;
    background(fixes, kT0, kT0 + 1800);
    background(fixes, kT0 + 1800 + 480, kT0 + 7200);
    steaming(fixes, 1, kT0, kT0 + 1680, 10, 12);
    steaming(fixes, 1, kT0 + 1680 + 720, kT0 + 3600, 10, 12);
    Detector det;
    EXPECT_TRUE(run(fixes, det).empty());
    EXPECT_EQ(det.stats().gaps_during_outage, 1U);
}

TEST(Anomaly, ManyVesselsReappearingTogetherIsCoverageNotAGap) {
    // Five vessels leave coverage together and come back within a minute of
    // each other (a satellite pass, a receiver restart); the network kept
    // working elsewhere.
    std::vector<Fix> fixes;
    background(fixes, kT0, kT0 + 7200);
    for (std::uint32_t v = 1; v <= 5; ++v) {
        steaming(fixes, v, kT0, kT0 + 1800, 10, 12, 2000.0 * v);
        steaming(fixes, v, kT0 + 3000 + 10.0 * v, kT0 + 3600, 10, 12, 2000.0 * v);
    }
    Detector det;
    EXPECT_EQ(count(run(fixes, det), Kind::Gap), 0U);
    EXPECT_EQ(det.stats().gaps_coverage_returned, 5U);
}

TEST(Anomaly, SilenceThatBeganBeforeListeningIsNotJudged) {
    std::vector<Fix> fixes;
    background(fixes, kT0, kT0 + 7200);
    steaming(fixes, 1, kT0, kT0 + 600, 10, 12);
    steaming(fixes, 1, kT0 + 3000, kT0 + 3600, 10, 12);
    anomaly::Params p;
    p.listening_since = kT0 + 2400;  // e.g. a live stream that opens with old positions
    Detector det(p);
    EXPECT_TRUE(run(fixes, det).empty());
    EXPECT_EQ(det.stats().gaps_before_listening, 1U);
}

TEST(Anomaly, TwoTransmittersSharingOneIdentity) {
    std::vector<Fix> fixes;
    steaming(fixes, 7, kT0, kT0 + 1200, 10, 10);
    steaming(fixes, 7, kT0 + 305, kT0 + 605, 10, 8, 20000);  // a second transmitter 20 km east
    Detector det;
    const auto found = run(fixes, det);
    EXPECT_EQ(count(found, Kind::IdentityConflict), 1U);
    EXPECT_EQ(count(found, Kind::PositionJump), 1U);  // its first report
    const auto conflict =
        std::find_if(found.begin(), found.end(), [](const Anomaly& a) { return a.kind == Kind::IdentityConflict; });
    EXPECT_NEAR(conflict->value, 20000, 300);
    EXPECT_EQ(det.stats().relocated, 0U);
}

TEST(Anomaly, WrongFirstPositionIsReplacedWithoutAConflict) {
    // The first report is 20 km off; every later one is consistent elsewhere.
    std::vector<Fix> fixes;
    fixes.push_back(at(3, kT0, 20000, 0, 10.0));
    steaming(fixes, 3, kT0 + 10, kT0 + 1800, 10, 10);
    Detector det;
    const auto found = run(fixes, det);
    EXPECT_EQ(count(found, Kind::PositionJump), 1U);
    EXPECT_EQ(count(found, Kind::IdentityConflict), 0U);
    EXPECT_EQ(det.stats().relocated, 1U);
}

TEST(Anomaly, RepeatsOfOneTransmissionCountOnce) {
    // Several base stations hear the same displaced report: still one
    // transmission, not three reports from a second transmitter.
    std::vector<Fix> fixes;
    steaming(fixes, 1, kT0, kT0 + 600, 10, 10);
    for (int copy = 0; copy < 3; ++copy) fixes.push_back(at(1, kT0 + 305, 8000, 0, 10.0));
    Detector det;
    const auto found = run(fixes, det);
    EXPECT_EQ(count(found, Kind::PositionJump), 1U);
    EXPECT_EQ(count(found, Kind::IdentityConflict), 0U);
    EXPECT_EQ(det.stats().relocated, 0U);
}

TEST(Anomaly, SearchAndRescueAircraftAreNotChecked) {
    EXPECT_TRUE(anomaly::is_sar_aircraft(111257014));
    EXPECT_FALSE(anomaly::is_sar_aircraft(257014000));
    std::vector<Fix> fixes;
    steaming(fixes, 111257014, kT0, kT0 + 600, 60, 115);  // a helicopter at 115 kn
    Detector det;
    EXPECT_TRUE(run(fixes, det).empty());
    EXPECT_EQ(det.stats().aircraft, 10U);
}

TEST(Anomaly, ExpireForgetsQuietVessels) {
    Detector det;
    std::vector<Anomaly> out;
    det.add(at(1, kT0, 0, 0, 10.0), out);
    det.add(at(2, kT0 + 3000, 0, 0, 10.0), out);
    EXPECT_EQ(det.expire(kT0 + 3600, 1800), 1U);
    EXPECT_EQ(det.stats().expired, 1U);
}

// Real records from the BarentsWatch Live AIS API (testdata, NLOD licence).
// Five thousand consecutive records span about two minutes, too short for a
// gap; this pins that the rules raise nothing unexpected on live data.
TEST(Anomaly, RecordedLiveSlice) {
    std::ifstream in(std::string(MT_TESTDATA_DIR) + "/barentswatch-live-2026-09-29.jsonl");
    ASSERT_TRUE(in);
    std::vector<Fix> fixes;
    std::string line;
    while (std::getline(in, line)) {
        if (auto r = track::parse_barentswatch(line); r.fix) fixes.push_back(*r.fix);
    }
    Detector det;
    const auto found = run(fixes, det);
    EXPECT_EQ(det.stats().fixes, 5000U);
    EXPECT_EQ(count(found, Kind::PositionJump), 0U);
    EXPECT_EQ(count(found, Kind::IdentityConflict), 0U);
    EXPECT_EQ(count(found, Kind::Gap), 0U);
    EXPECT_LE(count(found, Kind::ImpossibleSpeed), 1U);
}

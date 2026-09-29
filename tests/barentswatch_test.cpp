#include "maritime/track/barentswatch.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <string>

using namespace maritime::track;

TEST(Iso8601, ParsesOffsetsZuluAndFractions) {
    EXPECT_DOUBLE_EQ(*parse_iso8601("2026-04-22T12:00:00+00:00"), 1776859200.0);
    EXPECT_DOUBLE_EQ(*parse_iso8601("2026-04-22T12:00:00Z"), 1776859200.0);
    EXPECT_DOUBLE_EQ(*parse_iso8601("2026-04-22T14:00:00+02:00"), 1776859200.0);
    EXPECT_DOUBLE_EQ(*parse_iso8601("2026-04-22T11:30:00-00:30"), 1776859200.0);
    EXPECT_NEAR(*parse_iso8601("2026-04-22T12:00:00.25Z"), 1776859200.25, 1e-6);
}

TEST(Iso8601, RejectsMalformed) {
    EXPECT_FALSE(parse_iso8601(""));
    EXPECT_FALSE(parse_iso8601("2026-04-22T12:00:00"));        // no zone
    EXPECT_FALSE(parse_iso8601("2026-13-22T12:00:00Z"));       // month 13
    EXPECT_FALSE(parse_iso8601("2026-04-22T12:00:00.Z"));      // empty fraction
    EXPECT_FALSE(parse_iso8601("2026-04-22T12:00:00+0200"));   // offset without colon
    EXPECT_FALSE(parse_iso8601("2026/04/22T12:00:00Z"));
}

TEST(BarentsWatch, ParsesPositionRecord) {
    const auto r = parse_barentswatch(
        R"({"courseOverGround":289,"latitude":62.796623,"longitude":6.904042,"name":"TEST VESSEL",)"
        R"("rateOfTurn":0,"shipType":51,"speedOverGround":7.5,"trueHeading":95,"mmsi":257000123,)"
        R"("msgtime":"2026-04-22T12:00:00+00:00"})");
    ASSERT_EQ(r.kind, BwParse::Fix);
    ASSERT_TRUE(r.fix);
    EXPECT_EQ(r.fix->mmsi, 257000123U);
    EXPECT_DOUBLE_EQ(r.fix->t, 1776859200.0);
    EXPECT_DOUBLE_EQ(r.fix->lat_deg, 62.796623);
    EXPECT_DOUBLE_EQ(*r.fix->sog_knots, 7.5);
    EXPECT_DOUBLE_EQ(*r.fix->cog_deg, 289.0);
}

TEST(BarentsWatch, AcceptsServerSentEventsPrefix) {
    const auto r = parse_barentswatch(
        R"(data: {"latitude":60.1,"longitude":5.2,"mmsi":257000124,"msgtime":"2026-04-22T12:00:01Z"})");
    ASSERT_EQ(r.kind, BwParse::Fix);
    EXPECT_FALSE(r.fix->sog_knots);  // no speed/course in the record
}

TEST(BarentsWatch, StaticRecordHasNoPosition) {
    const auto r = parse_barentswatch(
        R"({"mmsi":257000125,"msgtime":"2026-04-22T12:00:02Z","name":"TEST","callSign":"LAXX1","shipType":70})");
    EXPECT_EQ(r.kind, BwParse::NoPosition);
    EXPECT_FALSE(r.fix);
}

TEST(BarentsWatch, NotAvailableSpeedAndCourseAreDropped) {
    const auto r = parse_barentswatch(
        R"({"latitude":60,"longitude":5,"speedOverGround":102.3,"courseOverGround":360,"mmsi":257000126,)"
        R"("msgtime":"2026-04-22T12:00:03Z"})");
    ASSERT_EQ(r.kind, BwParse::Fix);
    EXPECT_FALSE(r.fix->sog_knots);
    EXPECT_FALSE(r.fix->cog_deg);
}

TEST(BarentsWatch, RejectsInvalidRecords) {
    EXPECT_EQ(parse_barentswatch("").kind, BwParse::Invalid);
    EXPECT_EQ(parse_barentswatch("not json").kind, BwParse::Invalid);
    EXPECT_EQ(parse_barentswatch("[1,2,3]").kind, BwParse::Invalid);
    EXPECT_EQ(parse_barentswatch(R"({"latitude":60,"longitude":5,"msgtime":"2026-04-22T12:00:00Z"})").kind,
              BwParse::Invalid);  // no mmsi
    EXPECT_EQ(parse_barentswatch(R"({"latitude":60,"longitude":5,"mmsi":"257000127","msgtime":"2026-04-22T12:00:00Z"})")
                  .kind,
              BwParse::Invalid);  // mmsi as a string
    EXPECT_EQ(parse_barentswatch(R"({"latitude":95,"longitude":5,"mmsi":257000127,"msgtime":"2026-04-22T12:00:00Z"})")
                  .kind,
              BwParse::Invalid);  // latitude out of range
    EXPECT_EQ(parse_barentswatch(R"({"latitude":60,"longitude":5,"mmsi":257000127,"msgtime":"yesterday"})").kind,
              BwParse::Invalid);
    EXPECT_EQ(parse_barentswatch(R"({"latitude":60,"longitude":5,"mmsi":2.5,"msgtime":"2026-04-22T12:00:00Z"})").kind,
              BwParse::Invalid);
}

// Real records from the BarentsWatch Live AIS API, recorded on 2026-09-29
// (testdata/barentswatch-live-2026-09-29.jsonl, NLOD licence, see
// testdata/README.md). Pins the parser's behaviour on live data.
TEST(BarentsWatch, ParsesRecordedLiveSlice) {
    std::ifstream in(std::string(MT_TESTDATA_DIR) + "/barentswatch-live-2026-09-29.jsonl");
    ASSERT_TRUE(in) << "missing testdata/barentswatch-live-2026-09-29.jsonl";
    std::string line;
    std::size_t lines = 0;
    std::size_t fixes = 0;
    std::size_t with_velocity = 0;
    std::set<std::uint32_t> mmsis;
    Tracker tracker;
    while (std::getline(in, line)) {
        ++lines;
        const auto r = parse_barentswatch(line);
        ASSERT_NE(r.kind, BwParse::Invalid) << "line " << lines;
        if (r.kind != BwParse::Fix) continue;
        ++fixes;
        if (r.fix->sog_knots) ++with_velocity;
        mmsis.insert(r.fix->mmsi);
        (void)tracker.add(*r.fix);
    }
    EXPECT_EQ(lines, 5000U);
    EXPECT_EQ(fixes, 5000U);
    EXPECT_EQ(mmsis.size(), 3036U);
    EXPECT_GT(with_velocity, 4000U);
    EXPECT_EQ(tracker.stats().started, 3036U);
    EXPECT_EQ(tracker.stats().updated, 1960U);
    EXPECT_EQ(tracker.stats().rejected, 4U);
}

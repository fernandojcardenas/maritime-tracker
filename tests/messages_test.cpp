#include "maritime/ais/messages.hpp"

#include <gtest/gtest.h>

#include "encode.hpp"

using namespace maritime::ais;
using maritime::test::BitWriter;

namespace {

BitBuffer to_bits(const BitWriter& w) {
    int fill = 0;
    const auto payload = w.armor(fill);
    return *BitBuffer::from_payload(payload, fill);
}

BitWriter class_a(int type, std::uint32_t mmsi, std::int64_t lon_raw, std::int64_t lat_raw, std::uint32_t sog,
                  std::uint32_t cog, std::uint32_t hdg, std::int64_t rot = -128) {
    BitWriter w;
    w.u(static_cast<std::uint64_t>(type), 6).u(0, 2).u(mmsi, 30).u(0, 4).s(rot, 8).u(sog, 10).u(1, 1)
        .s(lon_raw, 28).s(lat_raw, 27).u(cog, 12).u(hdg, 9).u(42, 6).u(0, 2).u(0, 3).u(0, 1).u(0, 19);
    return w;
}

}  // namespace

TEST(Messages, ClassAPositionReport) {
    // 10.5 E, 59.9 N, 12.3 kn, COG 45.6, heading 47
    auto w = class_a(1, 257123456, 6300000, 35940000, 123, 456, 47);
    ASSERT_EQ(w.size(), 168U);
    auto m = decode(to_bits(w));
    ASSERT_TRUE(m);
    const auto& r = std::get<PositionReportA>(*m);
    EXPECT_EQ(r.type, 1);
    EXPECT_EQ(r.mmsi, 257123456U);
    ASSERT_TRUE(r.position);
    EXPECT_NEAR(r.position->lon_deg, 10.5, 1e-9);
    EXPECT_NEAR(r.position->lat_deg, 59.9, 1e-9);
    EXPECT_DOUBLE_EQ(*r.sog_knots, 12.3);
    EXPECT_DOUBLE_EQ(*r.cog_deg, 45.6);
    EXPECT_EQ(r.heading_deg, 47);
    EXPECT_TRUE(r.position_accuracy);
    EXPECT_EQ(r.utc_second, 42);
    EXPECT_FALSE(r.rot_deg_per_min);
    EXPECT_EQ(mmsi_of(*m), 257123456U);
    EXPECT_EQ(type_of(*m), 1);
}

TEST(Messages, ClassANegativeCoordinates) {
    // 70.25 W, 33.5 S
    auto m = decode(to_bits(class_a(3, 1, -42150000, -20100000, 0, 0, 0)));
    ASSERT_TRUE(m);
    const auto& r = std::get<PositionReportA>(*m);
    EXPECT_NEAR(r.position->lon_deg, -70.25, 1e-9);
    EXPECT_NEAR(r.position->lat_deg, -33.5, 1e-9);
}

TEST(Messages, ClassANotAvailableSentinels) {
    // lon 181, lat 91, SOG 1023, COG 3600, heading 511
    auto m = decode(to_bits(class_a(1, 1, 181 * 600000, 91 * 600000, 1023, 3600, 511)));
    ASSERT_TRUE(m);
    const auto& r = std::get<PositionReportA>(*m);
    EXPECT_FALSE(r.position);
    EXPECT_FALSE(r.sog_knots);
    EXPECT_FALSE(r.cog_deg);
    EXPECT_FALSE(r.heading_deg);
}

TEST(Messages, ClassARateOfTurn) {
    auto m = decode(to_bits(class_a(1, 1, 0, 0, 0, 0, 0, 20)));
    ASSERT_TRUE(m);
    const auto& r = std::get<PositionReportA>(*m);
    ASSERT_TRUE(r.rot_deg_per_min);
    EXPECT_NEAR(*r.rot_deg_per_min, (20 / 4.733) * (20 / 4.733), 1e-9);
    auto left = decode(to_bits(class_a(1, 1, 0, 0, 0, 0, 0, -20)));
    EXPECT_LT(*std::get<PositionReportA>(*left).rot_deg_per_min, 0.0);
}

TEST(Messages, ClassATooShort) {
    BitWriter w;
    w.u(1, 6).u(0, 2).u(1, 30).u(0, 100);
    auto m = decode(to_bits(w));
    ASSERT_FALSE(m);
    EXPECT_EQ(m.error(), DecodeError::TooShort);
}

TEST(Messages, StaticVoyageData) {
    BitWriter w;
    w.u(5, 6).u(0, 2).u(257000001, 30).u(0, 2).u(9123456, 30).text("LAXX1", 7).text("SEA TRIAL", 20).u(70, 8)
        .u(100, 9).u(20, 9).u(8, 6).u(7, 6).u(1, 4).u(6, 4).u(15, 5).u(12, 5).u(30, 6).u(64, 8)
        .text("BERGEN", 20).u(0, 1).u(0, 1);
    ASSERT_EQ(w.size(), 424U);
    auto m = decode(to_bits(w));
    ASSERT_TRUE(m);
    const auto& r = std::get<StaticVoyageData>(*m);
    EXPECT_EQ(r.mmsi, 257000001U);
    EXPECT_EQ(r.imo, 9123456U);
    EXPECT_EQ(r.callsign, "LAXX1");
    EXPECT_EQ(r.name, "SEA TRIAL");
    EXPECT_EQ(r.ship_type, 70);
    EXPECT_EQ(r.dims.to_bow_m + r.dims.to_stern_m, 120);
    EXPECT_EQ(r.dims.to_port_m + r.dims.to_starboard_m, 15);
    EXPECT_DOUBLE_EQ(*r.draught_m, 6.4);
    EXPECT_EQ(r.destination, "BERGEN");
}

TEST(Messages, StaticVoyageDataAcceptsShortTail) {
    // Real transmitters often send 420 bits; destination is cut to 19 chars.
    BitWriter w;
    w.u(5, 6).u(0, 2).u(257000001, 30).u(0, 2).u(0, 30).text("", 7).text("X", 20).u(0, 8).u(0, 30).u(0, 4)
        .u(0, 20).u(0, 8).text("TROMSO", 20);
    ASSERT_EQ(w.size(), 422U);
    auto m = decode(to_bits(w));  // armor pads to 426, fill 4 -> 422 bits
    ASSERT_TRUE(m);
    const auto& r = std::get<StaticVoyageData>(*m);
    EXPECT_FALSE(r.imo);
    EXPECT_FALSE(r.draught_m);
    EXPECT_EQ(r.destination, "TROMSO");
}

TEST(Messages, ClassBPositionReport) {
    BitWriter w;
    w.u(18, 6).u(0, 2).u(257999999, 30).u(0, 8).u(55, 10).u(0, 1).s(3000000, 28).s(36000000, 27).u(900, 12)
        .u(511, 9).u(10, 6).u(0, 2).u(0, 7).u(0, 20);  // spare, 7 flag bits, radio status
    ASSERT_EQ(w.size(), 168U);
    auto m = decode(to_bits(w));
    ASSERT_TRUE(m);
    const auto& r = std::get<PositionReportB>(*m);
    EXPECT_EQ(r.type, 18);
    EXPECT_DOUBLE_EQ(*r.sog_knots, 5.5);
    EXPECT_NEAR(r.position->lon_deg, 5.0, 1e-9);
    EXPECT_NEAR(r.position->lat_deg, 60.0, 1e-9);
    EXPECT_DOUBLE_EQ(*r.cog_deg, 90.0);
    EXPECT_FALSE(r.heading_deg);
    EXPECT_FALSE(r.dims);
}

TEST(Messages, ExtendedClassB) {
    BitWriter w;
    w.u(19, 6).u(0, 2).u(257888888, 30).u(0, 8).u(0, 10).u(0, 1).s(0, 28).s(0, 27).u(0, 12).u(0, 9).u(0, 6)
        .u(0, 4).text("DAY SAILER", 20).u(37, 8).u(9, 9).u(3, 9).u(2, 6).u(2, 6).u(1, 4).u(0, 1).u(0, 1).u(0, 1)
        .u(0, 4);
    ASSERT_EQ(w.size(), 312U);
    auto m = decode(to_bits(w));
    ASSERT_TRUE(m);
    const auto& r = std::get<PositionReportB>(*m);
    EXPECT_EQ(r.type, 19);
    EXPECT_EQ(r.name, "DAY SAILER");
    EXPECT_EQ(r.ship_type, 37);
    ASSERT_TRUE(r.dims);
    EXPECT_EQ(r.dims->to_bow_m + r.dims->to_stern_m, 12);
}

TEST(Messages, StaticDataReportParts) {
    BitWriter a;
    a.u(24, 6).u(0, 2).u(257777777, 30).u(0, 2).text("KNOTTY", 20);
    auto ma = decode(to_bits(a));
    ASSERT_TRUE(ma);
    EXPECT_EQ(std::get<StaticDataReport>(*ma).part, 0);
    EXPECT_EQ(std::get<StaticDataReport>(*ma).name, "KNOTTY");

    BitWriter b;
    b.u(24, 6).u(0, 2).u(257777777, 30).u(1, 2).u(36, 8).text("ABC", 7).text("LB1234", 7).u(8, 9).u(2, 9)
        .u(1, 6).u(2, 6).u(0, 6);
    auto mb = decode(to_bits(b));
    ASSERT_TRUE(mb);
    const auto& r = std::get<StaticDataReport>(*mb);
    EXPECT_EQ(r.part, 1);
    EXPECT_EQ(r.ship_type, 36);
    EXPECT_EQ(r.callsign, "LB1234");
    EXPECT_EQ(r.dims->to_bow_m, 8);
}

TEST(Messages, ClassAAcceptsTruncatedRadioStatus) {
    // 168 bits declared with 5 fill bits: 163 bits, all decoded fields present.
    auto w = class_a(1, 257123456, 6300000, 35940000, 123, 456, 47);
    int fill = 0;
    const auto payload = w.armor(fill);
    auto b = BitBuffer::from_payload(payload, 5);
    ASSERT_TRUE(b);
    ASSERT_EQ(b->size(), 163U);
    auto m = decode(*b);
    ASSERT_TRUE(m);
    EXPECT_EQ(std::get<PositionReportA>(*m).heading_deg, 47);
}

TEST(Messages, RejectsUndefinedMessageIds) {
    for (const std::uint64_t id : {0ULL, 28ULL, 37ULL, 63ULL}) {
        BitWriter w;
        w.u(id, 6).u(0, 2).u(2570001, 30).u(0, 130);
        auto m = decode(to_bits(w));
        ASSERT_FALSE(m) << id;
        EXPECT_EQ(m.error(), DecodeError::UnknownType);
    }
}

TEST(Messages, TextTrimsLeadingSpaces) {
    BitWriter w;
    w.u(24, 6).u(0, 2).u(257777777, 30).u(0, 2).text(" LEADING", 20);
    auto m = decode(to_bits(w));
    ASSERT_TRUE(m);
    EXPECT_EQ(std::get<StaticDataReport>(*m).name, "LEADING");
}

TEST(Messages, OtherTypesDecodeHeaderOnly) {
    BitWriter w;
    w.u(4, 6).u(0, 2).u(2570001, 30).u(0, 130);
    auto m = decode(to_bits(w));
    ASSERT_TRUE(m);
    EXPECT_EQ(type_of(*m), 4);
    EXPECT_EQ(mmsi_of(*m), 2570001U);
}

#include "maritime/ais/decoder.hpp"

#include <gtest/gtest.h>

#include "encode.hpp"

using namespace maritime;
using maritime::test::BitWriter;
using maritime::test::sentence;

namespace {

// A type 5 message is 424 bits = 71 armored chars, so real feeds split it in two.
std::string type5_payload(int& fill) {
    BitWriter w;
    w.u(5, 6).u(0, 2).u(257000001, 30).u(0, 2).u(0, 30).text("LAXX1", 7).text("SEA TRIAL", 20).u(70, 8).u(0, 30)
        .u(0, 4).u(0, 20).u(0, 8).text("BERGEN", 20).u(0, 2);
    return w.armor(fill);
}

}  // namespace

TEST(Decoder, ReassemblesTwoFragments) {
    int fill = 0;
    const auto payload = type5_payload(fill);
    ais::Decoder d;
    EXPECT_FALSE(d.feed(sentence(payload.substr(0, 60), 0, 2, 1, "3", 'A')));
    EXPECT_EQ(d.pending(), 1U);
    auto m = d.feed(sentence(payload.substr(60), fill, 2, 2, "3", 'A'));
    ASSERT_TRUE(m);
    EXPECT_EQ(std::get<ais::StaticVoyageData>(m->message).destination, "BERGEN");
    EXPECT_EQ(d.pending(), 0U);
    EXPECT_EQ(d.stats().messages, 1U);
    EXPECT_EQ(d.stats().by_type[5], 1U);
}

TEST(Decoder, DropsSecondFragmentWithoutFirst) {
    int fill = 0;
    const auto payload = type5_payload(fill);
    ais::Decoder d;
    EXPECT_FALSE(d.feed(sentence(payload.substr(60), fill, 2, 2, "3", 'A')));
    EXPECT_EQ(d.stats().fragments_dropped, 1U);
    EXPECT_EQ(d.stats().messages, 0U);
}

TEST(Decoder, KeepsInterleavedSequencesApart) {
    int fill = 0;
    const auto payload = type5_payload(fill);
    ais::Decoder d;
    EXPECT_FALSE(d.feed(sentence(payload.substr(0, 60), 0, 2, 1, "1", 'A')));
    EXPECT_FALSE(d.feed(sentence(payload.substr(0, 60), 0, 2, 1, "2", 'B')));
    EXPECT_TRUE(d.feed(sentence(payload.substr(60), fill, 2, 2, "2", 'B')));
    EXPECT_TRUE(d.feed(sentence(payload.substr(60), fill, 2, 2, "1", 'A')));
    EXPECT_EQ(d.stats().messages, 2U);
}

TEST(Decoder, RestartedSequenceReplacesStaleFragment) {
    int fill = 0;
    const auto payload = type5_payload(fill);
    ais::Decoder d;
    d.feed(sentence(payload.substr(0, 60), 0, 2, 1, "4", 'A'));
    d.feed(sentence(payload.substr(0, 60), 0, 2, 1, "4", 'A'));  // part 2 of the first was lost
    EXPECT_EQ(d.stats().fragments_dropped, 1U);
    EXPECT_TRUE(d.feed(sentence(payload.substr(60), fill, 2, 2, "4", 'A')));
}

TEST(Decoder, EvictsOldestWhenPendingIsFull) {
    int fill = 0;
    const auto payload = type5_payload(fill);
    ais::Decoder d(2);
    d.feed(sentence(payload.substr(0, 60), 0, 2, 1, "1", 'A'));
    d.feed(sentence(payload.substr(0, 60), 0, 2, 1, "2", 'A'));
    d.feed(sentence(payload.substr(0, 60), 0, 2, 1, "3", 'A'));
    EXPECT_EQ(d.pending(), 2U);
    EXPECT_EQ(d.stats().fragments_dropped, 1U);
    EXPECT_FALSE(d.feed(sentence(payload.substr(60), fill, 2, 2, "1", 'A')));  // evicted
    EXPECT_TRUE(d.feed(sentence(payload.substr(60), fill, 2, 2, "3", 'A')));
}

TEST(Decoder, CountsParseErrors) {
    ais::Decoder d;
    d.feed("garbage");
    d.feed("");
    EXPECT_EQ(d.stats().lines, 2U);
    EXPECT_EQ(d.stats().parse_errors[static_cast<std::size_t>(nmea::ParseError::MissingStart)], 1U);
    EXPECT_EQ(d.stats().parse_errors[static_cast<std::size_t>(nmea::ParseError::Empty)], 1U);
}

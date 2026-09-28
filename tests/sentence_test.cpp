#include "maritime/nmea/sentence.hpp"

#include <gtest/gtest.h>

#include "encode.hpp"

using maritime::nmea::parse_sentence;
using maritime::nmea::ParseError;

TEST(Sentence, ParsesSingleFragment) {
    const auto line = maritime::test::sentence("15M67FC000G?ufbE`FepT@3n00Sa", 0, 1, 1, "", 'B');
    auto s = parse_sentence(line);
    ASSERT_TRUE(s) << maritime::nmea::to_string(s.error());
    EXPECT_EQ(s->talker, "AI");
    EXPECT_FALSE(s->own_ship);
    EXPECT_EQ(s->fragment_count, 1);
    EXPECT_EQ(s->fragment_number, 1);
    EXPECT_FALSE(s->sequence_id.has_value());
    EXPECT_EQ(s->channel, 'B');
    EXPECT_EQ(s->payload, "15M67FC000G?ufbE`FepT@3n00Sa");
    EXPECT_EQ(s->fill_bits, 0);
}

TEST(Sentence, IgnoresTrailingCrLf) {
    auto s = parse_sentence(maritime::test::sentence("15M67FC000G?ufbE`FepT@3n00Sa", 0) + "\r\n");
    EXPECT_TRUE(s);
}

TEST(Sentence, AcceptsOtherTalkersAndVdo) {
    std::string body = "BSVDO,1,1,,A,15M67FC000G?ufbE`FepT@3n00Sa,0";
    char cs[4];
    std::snprintf(cs, sizeof cs, "%02X", maritime::nmea::checksum(body));
    auto s = parse_sentence("!" + body + "*" + cs);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->talker, "BS");
    EXPECT_TRUE(s->own_ship);
}

TEST(Sentence, RejectsBadChecksum) {
    auto line = maritime::test::sentence("15M67FC000G?ufbE`FepT@3n00Sa", 0);
    line[line.size() - 1] = line.back() == '0' ? '1' : '0';
    auto s = parse_sentence(line);
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error(), ParseError::BadChecksum);
}

TEST(Sentence, RejectsMissingChecksum) {
    auto s = parse_sentence("!AIVDM,1,1,,A,15M67FC000G?ufbE`FepT@3n00Sa,0");
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error(), ParseError::MissingChecksum);
}

TEST(Sentence, RejectsNonAisSentence) {
    std::string body = "GPGGA,123519,4807.038,N,01131.000,E";
    char cs[4];
    std::snprintf(cs, sizeof cs, "%02X", maritime::nmea::checksum(body));
    auto s = parse_sentence("$" + body + "*" + cs);
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error(), ParseError::MissingStart);
}

TEST(Sentence, RejectsFragmentNumberAboveCount) {
    auto s = parse_sentence(maritime::test::sentence("15M67FC000G?ufbE`FepT@3n00Sa", 0, 2, 3, "1"));
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error(), ParseError::BadFragmentFields);
}

TEST(Sentence, RejectsFillBitsAboveFive) {
    auto s = parse_sentence(maritime::test::sentence("15M67FC000G?ufbE`FepT@3n00Sa", 6));
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error(), ParseError::BadFillBits);
}

TEST(Sentence, RejectsPayloadCharOutsideArmorSet) {
    auto s = parse_sentence(maritime::test::sentence("15M67FC000G?uf[E", 0));  // '[' is in the gap
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error(), ParseError::BadPayloadChar);
}

TEST(Sentence, ParsesTagBlock) {
    const auto inner = maritime::test::sentence("15M67FC000G?ufbE`FepT@3n00Sa", 0);
    const std::string tag_body = "s:2573485,c:1727366000";
    char cs[4];
    std::snprintf(cs, sizeof cs, "%02X", maritime::nmea::checksum(tag_body));
    auto s = parse_sentence("\\" + tag_body + "*" + cs + "\\" + inner);
    ASSERT_TRUE(s) << maritime::nmea::to_string(s.error());
    EXPECT_EQ(s->tag.source, "2573485");
    EXPECT_EQ(s->tag.unix_time, 1727366000);
}

TEST(Sentence, NormalisesMillisecondTagTime) {
    const auto inner = maritime::test::sentence("15M67FC000G?ufbE`FepT@3n00Sa", 0);
    const std::string tag_body = "c:1727366000123";
    char cs[4];
    std::snprintf(cs, sizeof cs, "%02X", maritime::nmea::checksum(tag_body));
    auto s = parse_sentence("\\" + tag_body + "*" + cs + "\\" + inner);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->tag.unix_time, 1727366000);
}

TEST(Sentence, RejectsTagBlockWithBadChecksum) {
    const auto inner = maritime::test::sentence("15M67FC000G?ufbE`FepT@3n00Sa", 0);
    auto s = parse_sentence("\\s:2573485,c:1727366000*00\\" + inner);
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error(), ParseError::BadTagBlock);
}

TEST(Sentence, RejectsEmptyLine) {
    auto s = parse_sentence("  \r\n");
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error(), ParseError::Empty);
}

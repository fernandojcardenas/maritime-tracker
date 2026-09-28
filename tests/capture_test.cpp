// Runs the decoder over 85,194 lines of recorded AIS traffic
// (testdata/pyais-nmea-sample.nmea, see testdata/README.md). The exact counts
// pin down behaviour on real input, including the malformed sentences in it.
// Field-level correctness on the same file is checked against an independent
// decoder by tools/crosscheck.py.
#include <fstream>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "maritime/ais/decoder.hpp"

using namespace maritime;

TEST(Capture, DecodesRecordedTraffic) {
    std::ifstream in(std::string(MT_TESTDATA_DIR) + "/pyais-nmea-sample.nmea");
    ASSERT_TRUE(in) << "missing testdata/pyais-nmea-sample.nmea";

    ais::Decoder d;
    std::set<std::uint32_t> mmsis;
    std::string line;
    while (std::getline(in, line)) {
        if (auto m = d.feed(line)) mmsis.insert(ais::mmsi_of(m->message));
    }
    const auto& s = d.stats();
    EXPECT_EQ(s.lines, 85194U);
    EXPECT_EQ(s.sentences_ok, 85186U);
    // The 8 rejected sentences all declare 6 fill bits (valid range is 0-5).
    EXPECT_EQ(s.parse_errors[static_cast<std::size_t>(nmea::ParseError::BadFillBits)], 8U);
    EXPECT_EQ(s.messages, 82748U);
    EXPECT_EQ(s.decode_unknown_type, 2U);  // message ids 37 and 41
    EXPECT_EQ(s.decode_too_short, 0U);
    EXPECT_EQ(s.decode_invalid, 0U);
    EXPECT_EQ(s.fragments_dropped, 4U);
    EXPECT_EQ(s.by_type[1] + s.by_type[2] + s.by_type[3], 72132U);
    EXPECT_EQ(s.by_type[5], 2221U);
    EXPECT_EQ(s.by_type[18], 1361U);
    EXPECT_EQ(s.by_type[19], 109U);
    EXPECT_EQ(s.by_type[24], 307U);
    EXPECT_EQ(mmsis.size(), 11075U);
}

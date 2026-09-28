#include "maritime/ais/bits.hpp"

#include <gtest/gtest.h>

#include "encode.hpp"

using maritime::ais::BitBuffer;
using maritime::test::BitWriter;

namespace {

BitBuffer roundtrip(const BitWriter& w) {
    int fill = 0;
    const auto payload = w.armor(fill);
    auto b = BitBuffer::from_payload(payload, fill);
    EXPECT_TRUE(b.has_value());
    return *b;
}

}  // namespace

TEST(Bits, DearmorsBoundaryCharacters) {
    // '0' -> 0, 'W' -> 39, '`' -> 40, 'w' -> 63
    auto b = BitBuffer::from_payload("0W`w", 0);
    ASSERT_TRUE(b);
    EXPECT_EQ(b->size(), 24U);
    EXPECT_EQ(b->u(0, 6), 0U);
    EXPECT_EQ(b->u(6, 6), 39U);
    EXPECT_EQ(b->u(12, 6), 40U);
    EXPECT_EQ(b->u(18, 6), 63U);
}

TEST(Bits, RejectsGapCharacters) {
    EXPECT_FALSE(BitBuffer::from_payload("X", 0));
    EXPECT_FALSE(BitBuffer::from_payload("_", 0));
    EXPECT_FALSE(BitBuffer::from_payload("x", 0));
}

TEST(Bits, FillBitsShortenBuffer) {
    auto b = BitBuffer::from_payload("ww", 4);
    ASSERT_TRUE(b);
    EXPECT_EQ(b->size(), 8U);
    EXPECT_FALSE(BitBuffer::from_payload("w", 7));
}

TEST(Bits, UnsignedAndSignedFields) {
    BitWriter w;
    w.u(5, 6).s(-1, 8).s(-3000000, 28).u(0x3FFFFFFF, 30);
    auto b = roundtrip(w);
    EXPECT_EQ(b.u(0, 6), 5U);
    EXPECT_EQ(b.s(6, 8), -1);
    EXPECT_EQ(b.s(14, 28), -3000000);
    EXPECT_EQ(b.u(42, 30), 0x3FFFFFFFU);
}

TEST(Bits, OutOfRangeReadsReturnNullopt) {
    auto b = BitBuffer::from_payload("ww", 0);
    ASSERT_TRUE(b);
    EXPECT_FALSE(b->u(10, 3));
    EXPECT_FALSE(b->u(0, 0));
    EXPECT_FALSE(b->u(0, 33));
    EXPECT_FALSE(b->text(0, 18));
    EXPECT_FALSE(b->u(std::size_t{1} << 40, 1));
}

TEST(Bits, TextTrimsPadding) {
    BitWriter w;
    w.text("SEA TRIAL", 20);
    auto b = roundtrip(w);
    EXPECT_EQ(b.text(0, 120), "SEA TRIAL");
}

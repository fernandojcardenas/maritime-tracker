#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "maritime/ingest/bounded_queue.hpp"
#include "maritime/ingest/line_framer.hpp"
#include "maritime/ingest/recorder.hpp"
#include "maritime/ingest/replay.hpp"
#include "maritime/nmea/sentence.hpp"

using namespace maritime::ingest;
using namespace std::chrono_literals;

// ---------- LineFramer ----------

TEST(LineFramer, ReassemblesLinesSplitAcrossChunks) {
    LineFramer f;
    std::vector<std::string> out;
    auto sink = [&](std::string_view l) { out.emplace_back(l); };
    f.feed("!AIVDM,1,1,,A,1", sink);
    f.feed("3,0*00\r\n!AIVDM,2", sink);
    f.feed(",1\n\n\r\n", sink);
    EXPECT_EQ(out, (std::vector<std::string>{"!AIVDM,1,1,,A,13,0*00", "!AIVDM,2,1"}));
    EXPECT_EQ(f.stats().lines, 2U);
}

TEST(LineFramer, DropsOversizeLineAndRecovers) {
    LineFramer f(8);
    std::vector<std::string> out;
    auto sink = [&](std::string_view l) { out.emplace_back(l); };
    f.feed("0123456789ABCDEF", sink);  // no newline yet, over the limit
    EXPECT_LE(f.buffered(), 8U);
    f.feed("more\nok\n", sink);
    EXPECT_EQ(out, (std::vector<std::string>{"ok"}));
    EXPECT_EQ(f.stats().oversize_dropped, 1U);
}

TEST(LineFramer, ResetDiscardsPartialLine) {
    LineFramer f;
    std::vector<std::string> out;
    auto sink = [&](std::string_view l) { out.emplace_back(l); };
    f.feed("half a li", sink);
    f.reset();
    f.feed("ne\nwhole\n", sink);
    EXPECT_EQ(out, (std::vector<std::string>{"ne", "whole"}));
}

// ---------- BoundedQueue ----------

TEST(BoundedQueue, DropsOldestWhenFull) {
    BoundedQueue<int> q(2);
    q.push(1);
    q.push(2);
    q.push(3);
    EXPECT_EQ(q.dropped(), 1U);
    EXPECT_EQ(q.pop(0ms), 2);
    EXPECT_EQ(q.pop(0ms), 3);
    EXPECT_FALSE(q.pop(0ms));
}

TEST(BoundedQueue, CloseWakesConsumerAndRejectsPush) {
    BoundedQueue<int> q(4);
    std::thread consumer([&] { EXPECT_FALSE(q.pop(10s)); });
    std::this_thread::sleep_for(20ms);
    q.close();
    consumer.join();
    EXPECT_FALSE(q.push(1));
    EXPECT_TRUE(q.closed_and_empty());
}

TEST(BoundedQueue, PushWaitNeverDropsAndPreservesOrder) {
    BoundedQueue<int> q(8);  // far smaller than the input, so the producer must wait
    constexpr int kItems = 50000;
    std::vector<int> got;
    got.reserve(kItems);
    std::thread consumer([&] {
        while (true) {
            if (auto v = q.pop(50ms)) {
                got.push_back(*v);
            } else if (q.closed_and_empty()) {
                break;
            }
        }
    });
    for (int i = 0; i < kItems; ++i) ASSERT_TRUE(q.push_wait(i));
    q.close();
    consumer.join();
    EXPECT_EQ(q.dropped(), 0U);
    ASSERT_EQ(got.size(), static_cast<std::size_t>(kItems));
    for (int i = 0; i < kItems; ++i) ASSERT_EQ(got[static_cast<std::size_t>(i)], i);
}

TEST(BoundedQueue, PushWaitUnblocksOnClose) {
    BoundedQueue<int> q(1);
    q.push(1);
    std::thread producer([&] { EXPECT_FALSE(q.push_wait(2)); });  // full, so it waits
    std::this_thread::sleep_for(20ms);
    q.close();
    producer.join();
}

TEST(BoundedQueue, ProducerConsumerAccountsForEveryItem) {
    BoundedQueue<int> q(64);
    constexpr int kItems = 20000;
    std::uint64_t received = 0;
    std::thread consumer([&] {
        while (true) {
            if (q.pop(50ms)) {
                ++received;
            } else if (q.closed_and_empty()) {
                break;
            }
        }
    });
    for (int i = 0; i < kItems; ++i) q.push(i);
    q.close();
    consumer.join();
    EXPECT_EQ(received + q.dropped(), static_cast<std::uint64_t>(kItems));
}

// ---------- Replay ----------

namespace {

class FakeClock final : public Clock {
public:
    [[nodiscard]] time_point now() const override { return now_; }
    void sleep_until(time_point t) override {
        sleeps.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(t - start_));
        if (t > now_) now_ = t;
    }
    std::vector<std::chrono::milliseconds> sleeps;

private:
    time_point start_{std::chrono::seconds(1000)};
    time_point now_{start_};
};

std::string tagged(std::int64_t t, std::string_view sentence = "!AIVDM,1,1,,A,13,0*00") {
    return Recorder::with_timestamp(sentence, t);
}

}  // namespace

TEST(Replay, ReadsTagBlockTime) {
    EXPECT_EQ(tag_block_time(tagged(1727366000)), 1727366000);
    EXPECT_EQ(tag_block_time("\\s:2573345,c:1682294942*00\\!BSVDM,1,1,,B,13mo2<?P05Pm<5rU<oBFGgv62@28,0*04"),
              1682294942);
    EXPECT_FALSE(tag_block_time("!AIVDM,1,1,,A,13,0*00"));
    EXPECT_FALSE(tag_block_time("\\c:1727366000*00\\!AIVDM"));  // bad tag checksum
}

TEST(Replay, PacesByTimestampAtSpeed) {
    std::stringstream in;
    in << tagged(100) << '\n' << tagged(101) << '\n' << tagged(110) << '\n';
    FakeClock clock;
    std::vector<std::string> out;
    const std::atomic<bool> stop{false};
    const auto stats = replay(in, {.speed = 10.0}, clock, [&](std::string_view l) { out.emplace_back(l); }, stop);
    EXPECT_EQ(out.size(), 3U);
    EXPECT_EQ(stats.timestamped, 3U);
    // 0 s, 1 s and 10 s of recorded time at 10x speed.
    EXPECT_EQ(clock.sleeps, (std::vector<std::chrono::milliseconds>{0ms, 100ms, 1000ms}));
}

TEST(Replay, ShortensLongGaps) {
    std::stringstream in;
    in << tagged(0) << '\n' << tagged(3600) << '\n' << tagged(3601) << '\n';
    FakeClock clock;
    const std::atomic<bool> stop{false};
    const auto stats = replay(in, {.speed = 1.0, .max_gap = 5s}, clock, [](std::string_view) {}, stop);
    EXPECT_EQ(stats.gaps_shortened, 1U);
    EXPECT_EQ(clock.sleeps, (std::vector<std::chrono::milliseconds>{0ms, 5000ms, 6000ms}));
}

TEST(Replay, BackwardsTimestampDoesNotRewindClock) {
    std::stringstream in;
    in << tagged(10) << '\n' << tagged(12) << '\n' << tagged(11) << '\n';
    FakeClock clock;
    const std::atomic<bool> stop{false};
    const auto stats = replay(in, {.speed = 1.0}, clock, [](std::string_view) {}, stop);
    EXPECT_EQ(stats.backwards_timestamps, 1U);
    EXPECT_EQ(clock.sleeps, (std::vector<std::chrono::milliseconds>{0ms, 2000ms, 2000ms}));
}

TEST(Replay, SpeedZeroAndUntaggedLinesDontSleep) {
    std::stringstream in;
    in << tagged(0) << '\n' << "!AIVDM,1,1,,A,13,0*00\n" << tagged(50) << '\n';
    FakeClock clock;
    const std::atomic<bool> stop{false};
    std::size_t n = 0;
    replay(in, {.speed = 0.0}, clock, [&](std::string_view) { ++n; }, stop);
    EXPECT_EQ(n, 3U);
    EXPECT_TRUE(clock.sleeps.empty());
}

TEST(Replay, StopsWhenAsked) {
    std::stringstream in;
    for (int i = 0; i < 10; ++i) in << tagged(i) << '\n';
    FakeClock clock;
    std::atomic<bool> stop{false};
    std::size_t n = 0;
    replay(in, {.speed = 0.0}, clock,
           [&](std::string_view) {
               if (++n == 3) stop = true;
           },
           stop);
    EXPECT_EQ(n, 3U);
}

// ---------- Recorder ----------

TEST(Recorder, NamesFilesByUtcHour) {
    EXPECT_EQ(Recorder::file_name_for(0), "ais-19700101-00.nmea");
    EXPECT_EQ(Recorder::file_name_for(1727366000), "ais-20240926-15.nmea");
}

TEST(Recorder, AddsValidTimestampTagBlock) {
    const std::string sentence = "!BSVDM,1,1,,B,13mo2<?P05Pm<5rU<oBFGgv62@28,0*04";
    const auto line = Recorder::with_timestamp(sentence, 1727366000);
    auto parsed = maritime::nmea::parse_sentence(line);
    ASSERT_TRUE(parsed) << maritime::nmea::to_string(parsed.error());
    EXPECT_EQ(parsed->tag.unix_time, 1727366000);
    // An existing tag block is kept as is.
    EXPECT_EQ(Recorder::with_timestamp(line, 5), line);
}

TEST(Recorder, RotatesHourlyAndRoundTripsThroughReplay) {
    const auto dir = std::filesystem::temp_directory_path() / ("mt-rec-" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    {
        Recorder rec(dir);
        rec.write("!AIVDM,1,1,,A,13,0*00", 1727366000);  // 15:53 UTC
        rec.write("!AIVDM,1,1,,A,14,0*00", 1727369999);  // 16:59 UTC
        rec.flush();
        EXPECT_EQ(rec.stats().files_opened, 2U);
        EXPECT_EQ(rec.stats().timestamps_added, 2U);
    }
    std::ifstream first(dir / "ais-20240926-15.nmea");
    std::string line;
    ASSERT_TRUE(std::getline(first, line));
    EXPECT_EQ(tag_block_time(line), 1727366000);
    EXPECT_TRUE(std::filesystem::exists(dir / "ais-20240926-16.nmea"));
    std::filesystem::remove_all(dir);
}

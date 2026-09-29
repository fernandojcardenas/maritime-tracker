// mt-ingest: read AIS from a live TCP feed, stdin or a recorded file, decode
// it, track vessels, and optionally record the raw lines for later replay.
//
//   mt-ingest --tcp 153.44.253.27:5631 --record recordings/
//   mt-ingest --replay recordings/ais-20260928-17.nmea --speed 60 --json > messages.jsonl
//   tools/barentswatch_stream.sh | mt-ingest --stdin --format barentswatch --duration 3600
//
// Formats: "nmea" (raw !AIVDM sentences, the default) or "barentswatch"
// (one JSON record per line from the BarentsWatch Live AIS API).
//
// A reader thread (network or file) feeds a bounded queue; the main thread
// decodes. If decoding falls behind, the oldest lines are dropped and counted
// rather than blocking the reader. Statistics go to stderr every few seconds.

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>

#include "maritime/ais/decoder.hpp"
#include "maritime/ais/json.hpp"
#include "maritime/ingest/bounded_queue.hpp"
#include "maritime/ingest/fd_source.hpp"
#include "maritime/ingest/recorder.hpp"
#include "maritime/ingest/replay.hpp"
#include "maritime/ingest/tcp_source.hpp"
#include "maritime/track/barentswatch.hpp"
#include "maritime/track/tracker.hpp"

namespace {

using namespace maritime;
using namespace std::chrono;

std::atomic<bool> g_stop{false};

extern "C" void on_signal(int /*signal*/) { g_stop.store(true); }

struct Item {
    std::string line;
    std::int64_t received_unix = 0;
};

struct Args {
    std::optional<std::string> tcp;  // host:port
    std::optional<std::string> replay_file;
    bool stdin_source = false;
    bool barentswatch = false;  // --format barentswatch
    double speed = 1.0;
    std::optional<std::string> record_dir;
    bool json = false;
    seconds stats_every{10};
    std::optional<seconds> duration;
    std::size_t queue_capacity = 65536;
};

void usage() {
    std::cerr << "usage: mt-ingest (--tcp HOST:PORT | --stdin | --replay FILE [--speed N])\n"
                 "                 [--format nmea|barentswatch] [--record DIR] [--json]\n"
                 "                 [--stats-every SECONDS] [--duration SECONDS] [--queue N]\n"
                 "  --speed 0 replays as fast as possible; 1 is real time (default). NMEA replay is\n"
                 "  paced by tag-block time; barentswatch replay is not paced.\n";
}

std::optional<Args> parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string(argv[++i]);
        };
        if (arg == "--json") {
            a.json = true;
            continue;
        }
        if (arg == "--stdin") {
            a.stdin_source = true;
            continue;
        }
        const std::optional<std::string> v = next();
        if (!v) return std::nullopt;
        if (arg == "--tcp") {
            a.tcp = *v;
        } else if (arg == "--replay") {
            a.replay_file = *v;
        } else if (arg == "--speed") {
            a.speed = std::stod(*v);
        } else if (arg == "--record") {
            a.record_dir = *v;
        } else if (arg == "--stats-every") {
            a.stats_every = seconds(std::stol(*v));
        } else if (arg == "--duration") {
            a.duration = seconds(std::stol(*v));
        } else if (arg == "--queue") {
            a.queue_capacity = std::stoul(*v);
        } else if (arg == "--format") {
            if (*v != "nmea" && *v != "barentswatch") return std::nullopt;
            a.barentswatch = *v == "barentswatch";
        } else {
            return std::nullopt;
        }
    }
    const int sources = (a.tcp ? 1 : 0) + (a.replay_file ? 1 : 0) + (a.stdin_source ? 1 : 0);
    if (sources != 1 || a.speed < 0 || a.stats_every.count() <= 0) return std::nullopt;
    if (a.tcp && a.tcp->rfind(':') == std::string::npos) return std::nullopt;
    return a;
}

std::int64_t unix_now() { return duration_cast<seconds>(system_clock::now().time_since_epoch()).count(); }

int run(int argc, char** argv) {
    const auto args = parse_args(argc, argv);
    if (!args) {
        usage();
        return 2;
    }
    if (std::signal(SIGINT, on_signal) == SIG_ERR || std::signal(SIGTERM, on_signal) == SIG_ERR) {
        std::cerr << "mt-ingest: cannot install signal handlers\n";
        return 1;
    }

    ingest::BoundedQueue<Item> queue(args->queue_capacity);
    std::unique_ptr<ingest::TcpSource> tcp;
    ingest::ReplayStats replay_stats;
    std::atomic<bool> reader_done{false};

    std::thread reader;
    if (args->tcp) {
        const auto colon = args->tcp->rfind(':');
        ingest::TcpOptions opts;
        opts.host = args->tcp->substr(0, colon);
        opts.port = args->tcp->substr(colon + 1);
        tcp = std::make_unique<ingest::TcpSource>(opts);
        reader = std::thread([&] {
            tcp->run(g_stop, [&](std::string_view l) { queue.push(Item{std::string(l), unix_now()}); });
            reader_done = true;
            queue.close();
        });
    } else if (args->stdin_source) {
        reader = std::thread([&] {
            ingest::read_lines(0, g_stop, [&](std::string_view l) { queue.push(Item{std::string(l), unix_now()}); });
            reader_done = true;
            queue.close();
        });
    } else {
        reader = std::thread([&] {
            std::ifstream in(*args->replay_file);
            if (!in) {
                std::cerr << "mt-ingest: cannot open " << *args->replay_file << '\n';
            } else {
                ingest::SteadyClock clock;
                replay_stats = ingest::replay(in, {.speed = args->speed}, clock,
                                              [&](std::string_view l) { queue.push(Item{std::string(l), unix_now()}); },
                                              g_stop);
            }
            reader_done = true;
            queue.close();
        });
    }

    std::optional<ingest::Recorder> recorder;
    if (args->record_dir) {
        ingest::RecorderOptions ro;
        if (args->barentswatch) ro = {".jsonl", false};
        recorder.emplace(*args->record_dir, ro);
    }

    std::ios::sync_with_stdio(false);
    ais::Decoder decoder;
    track::Tracker tracker;
    std::set<std::uint32_t> mmsis;
    // Counters for the barentswatch format (the NMEA decoder keeps its own).
    std::uint64_t bw_lines = 0;
    std::uint64_t bw_fixes = 0;
    std::uint64_t bw_no_position = 0;
    std::uint64_t bw_invalid = 0;
    double next_expire = 0.0;
    const auto track_fix = [&](const track::Fix& f) {
        if (f.t >= next_expire) {
            tracker.expire(f.t);
            next_expire = f.t + 60.0;
        }
        (void)tracker.add(f);
    };
    const auto lines_seen = [&] { return args->barentswatch ? bw_lines : decoder.stats().lines; };
    const auto messages_seen = [&] { return args->barentswatch ? bw_fixes : decoder.stats().messages; };
    const auto start = steady_clock::now();
    auto next_stats = start + args->stats_every;
    std::uint64_t messages_at_last = 0;
    auto last_stats = start;

    const auto print_stats = [&](steady_clock::time_point now) {
        const double interval = duration<double>(now - last_stats).count();
        const auto msgs = messages_seen();
        const double rate = interval > 0 ? static_cast<double>(msgs - messages_at_last) / interval : 0.0;
        std::cerr << std::fixed << std::setprecision(1) << "[" << duration<double>(now - start).count() << " s] "
                  << "lines " << lines_seen() << "  msgs " << msgs << "  msg/s " << rate << "  vessels "
                  << mmsis.size() << "  tracks " << tracker.size() << "  gate-rejects " << tracker.stats().rejected
                  << "  queue-dropped " << queue.dropped();
        if (tcp) {
            const auto t = tcp->stats();
            std::cerr << "  connects " << t.connects << "  connect-failures " << t.connect_failures
                      << "  peer-closes " << t.peer_closes << "  idle-timeouts " << t.idle_timeouts;
        }
        if (recorder) std::cerr << "  recorded " << recorder->stats().lines;
        std::cerr << '\n';
        messages_at_last = msgs;
        last_stats = now;
    };

    while (true) {
        const auto now = steady_clock::now();
        if (args->duration && now - start >= *args->duration) g_stop = true;
        if (now >= next_stats) {
            print_stats(now);
            if (recorder) recorder->flush();
            next_stats = now + args->stats_every;
        }
        auto item = queue.pop(milliseconds(200));
        if (!item) {
            if (queue.closed_and_empty()) break;
            continue;
        }
        if (recorder) recorder->write(item->line, item->received_unix);
        if (args->barentswatch) {
            ++bw_lines;
            const auto r = track::parse_barentswatch(item->line);
            if (r.kind == track::BwParse::Invalid) {
                ++bw_invalid;
            } else if (r.kind == track::BwParse::NoPosition) {
                ++bw_no_position;
            } else {
                const auto& f = *r.fix;
                ++bw_fixes;
                mmsis.insert(f.mmsi);
                track_fix(f);
                if (args->json) {
                    std::cout << std::setprecision(10) << "{\"mmsi\":" << f.mmsi << ",\"t\":" << f.t << ",\"lat\":" << f.lat_deg
                              << ",\"lon\":" << f.lon_deg << ",\"sog\":";
                    if (f.sog_knots) std::cout << *f.sog_knots; else std::cout << "null";
                    std::cout << ",\"cog\":";
                    if (f.cog_deg) std::cout << *f.cog_deg; else std::cout << "null";
                    std::cout << "}\n";
                }
            }
        } else if (auto dm = decoder.feed(item->line)) {
            mmsis.insert(ais::mmsi_of(dm->message));
            if (auto f = track::fix_from(*dm)) track_fix(*f);
            if (args->json) ais::write_json(std::cout, *dm);
        }
    }
    reader.join();
    if (recorder) recorder->flush();

    print_stats(steady_clock::now());
    std::cerr << "--- final ---\n";
    if (args->barentswatch) {
        std::cerr << "lines            " << bw_lines << '\n'
                  << "messages         " << bw_fixes << '\n'
                  << "unique MMSIs     " << mmsis.size() << '\n'
                  << "no position      " << bw_no_position << '\n'
                  << "invalid records  " << bw_invalid << '\n';
    } else {
        ais::write_stats(std::cerr, decoder.stats(), mmsis.size());
    }
    const auto& ts = tracker.stats();
    std::cerr << "tracker          " << ts.started << " tracks started, " << ts.updated << " updates, " << ts.rejected
              << " gate rejections, " << ts.restarted << " restarts, " << ts.duplicates << " duplicates, "
              << ts.out_of_order << " out of order\n";
    std::cerr << "queue dropped    " << queue.dropped() << '\n';
    if (args->replay_file) {
        std::cerr << "replay lines " << replay_stats.lines << ", timestamped " << replay_stats.timestamped
                  << ", gaps shortened " << replay_stats.gaps_shortened << ", backwards timestamps "
                  << replay_stats.backwards_timestamps << '\n';
    }
    if (recorder) {
        std::cerr << "recorded " << recorder->stats().lines << " lines into " << recorder->stats().files_opened
                  << " file(s), write errors " << recorder->stats().write_errors << '\n';
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "mt-ingest: " << e.what() << '\n';
        return 1;
    }
}

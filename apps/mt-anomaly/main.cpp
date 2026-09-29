// mt-anomaly: run the anomaly detector over recorded traffic.
//
// Without --inject it reports every flag raised on the data as recorded. With
// --inject N it also plants N labelled anomalies of each kind into a copy of
// the data, for each of --seeds seeds, and measures how many the detector
// finds and how many extra flags the planted data causes:
//
//   jump      one transmission moved 3-30 km (a spoofed or corrupted position)
//   speed     one transmission's speed set to 55-100 kn
//   gap       a moving vessel's reports removed for 15-40 minutes (AIS switched off)
//   conflict  a second vessel's reports copied under this vessel's MMSI for
//             10 minutes, at least 5 km away (two transmitters, one identity)
//
//   mt-anomaly --dk-csv oresund.csv [--inject 20 --seeds 5] [--list]
//   mt-anomaly --barentswatch live.jsonl [--list]
//   mt-anomaly --nmea recording.nmea

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "maritime/ais/decoder.hpp"
#include "maritime/anomaly/detector.hpp"
#include "maritime/track/barentswatch.hpp"
#include "maritime/track/geo.hpp"
#include "maritime/track/tracker.hpp"

namespace {

using namespace maritime;
using anomaly::Anomaly;
using anomaly::Kind;
using track::Fix;

constexpr std::array<Kind, 4> kKinds{Kind::ImpossibleSpeed, Kind::PositionJump, Kind::Gap, Kind::IdentityConflict};

// Sizes of planted anomalies. Defaults are clear-cut cases; smaller values
// probe where detection stops.
struct Sizes {
    double jump_km_lo = 3.0, jump_km_hi = 30.0;
    double speed_kn_lo = 55.0, speed_kn_hi = 100.0;
    double gap_min_lo = 15.0, gap_min_hi = 40.0;
    double conflict_km_lo = 5.0, conflict_km_hi = 1e9;  // distance between the two transmitters
};

struct Args {
    Sizes sizes;
    std::optional<std::string> dk_csv, nmea, barentswatch;
    double listening_since = -1e300;
    int inject = 0;
    int seeds = 1;
    bool list = false;
    bool missed = false;
};

std::vector<Fix> load(const Args& a) {
    std::vector<Fix> fixes;
    std::string line;
    const std::string path = a.dk_csv.value_or(a.nmea.value_or(a.barentswatch.value_or("")));
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    ais::Decoder d;
    while (std::getline(in, line)) {
        if (a.dk_csv) {
            if (auto f = track::fix_from_dk_csv(line)) fixes.push_back(*f);
        } else if (a.nmea) {
            if (auto dm = d.feed(line)) {
                if (auto f = track::fix_from(*dm)) fixes.push_back(*f);
            }
        } else {
            if (auto r = track::parse_barentswatch(line); r.fix) fixes.push_back(*r.fix);
        }
    }
    std::stable_sort(fixes.begin(), fixes.end(), [](const Fix& x, const Fix& y) { return x.t < y.t; });
    return fixes;
}

double g_listening_since = -1e300;  // set once from the command line

std::vector<Anomaly> detect(const std::vector<Fix>& fixes, anomaly::Stats* stats = nullptr) {
    anomaly::Params params;
    params.listening_since = g_listening_since;
    anomaly::Detector det(params);
    std::vector<Anomaly> out;
    double next_expire = 0.0;
    for (const auto& f : fixes) {
        if (f.t >= next_expire) {
            det.expire(f.t, 3.0 * 3600.0);
            next_expire = f.t + 600.0;
        }
        det.add(f, out);
    }
    det.flush(out);
    if (stats != nullptr) *stats = det.stats();
    return out;
}

// ---- planted anomalies -------------------------------------------------------

struct Planted {
    Kind kind;
    std::uint32_t mmsi;
    double from;  // a flag for this vessel in [from, to] counts as detecting it
    double to;
};

using ByVessel = std::map<std::uint32_t, std::vector<std::size_t>>;  // indices into the sorted reports

ByVessel index_by_vessel(const std::vector<Fix>& fixes) {
    ByVessel by;
    for (std::size_t i = 0; i < fixes.size(); ++i) by[fixes[i].mmsi].push_back(i);
    return by;
}

bool moving(const Fix& f) {
    return f.sog_knots && *f.sog_knots >= 2.0;
}

// Distinct report times of one vessel in [t0, t1).
std::size_t reports_between(const std::vector<Fix>& fixes, const std::vector<std::size_t>& idx, double t0, double t1) {
    std::set<double> times;
    for (const std::size_t i : idx)
        if (fixes[i].t >= t0 && fixes[i].t < t1) times.insert(fixes[i].t);
    return times.size();
}

// Plants `n` anomalies of each kind, each on a different vessel. A vessel is
// eligible if it reports at least 60 times over at least 40 minutes, so there
// is traffic before and after every planted case. Returns the modified
// reports (sorted) and the labels.
std::vector<Fix> plant(const std::vector<Fix>& clean, int n, std::uint64_t seed, const Sizes& sz,
                       std::vector<Planted>& labels) {
    std::mt19937_64 rng(seed);  // NOLINT(cert-msc32-c,cert-msc51-cpp): seeded so evaluations are reproducible
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const auto by = index_by_vessel(clean);

    std::vector<std::uint32_t> eligible;
    std::vector<std::uint32_t> all;
    for (const auto& [mmsi, idx] : by) {
        all.push_back(mmsi);
        if (idx.size() >= 60 && clean[idx.back()].t - clean[idx.front()].t >= 2400.0) eligible.push_back(mmsi);
    }
    std::shuffle(eligible.begin(), eligible.end(), rng);
    std::size_t next_vessel = 0;
    auto take = [&]() -> std::optional<std::uint32_t> {
        if (next_vessel >= eligible.size()) return std::nullopt;
        return eligible[next_vessel++];
    };
    auto pick = [&](std::size_t lo, std::size_t hi) {  // uniform in [lo, hi)
        return lo + static_cast<std::size_t>(unit(rng) * static_cast<double>(hi - lo));
    };

    std::vector<Fix> out = clean;
    std::vector<bool> removed(out.size(), false);
    std::vector<Fix> added;

    // Applies `fn` to every copy of one transmission (same vessel, same time).
    auto each_copy = [&](const std::vector<std::size_t>& idx, double t, auto fn) {
        for (const std::size_t i : idx)
            if (clean[i].t == t) fn(out[i]);
    };

    for (int k = 0; k < n; ++k) {
        // Position jump.
        if (auto m = take()) {
            const auto& idx = by.at(*m);
            const double t = clean[idx[pick(10, idx.size() - 10)]].t;
            const double r = 1000.0 * (sz.jump_km_lo + (sz.jump_km_hi - sz.jump_km_lo) * unit(rng));
            const double th = 2.0 * std::numbers::pi * unit(rng);
            each_copy(idx, t, [&](Fix& f) {
                const track::LocalFrame fr(f.lat_deg, f.lon_deg);
                fr.to_geo({r * std::cos(th), r * std::sin(th)}, f.lat_deg, f.lon_deg);
            });
            labels.push_back({Kind::PositionJump, *m, t, t});
        }
        // Impossible speed.
        if (auto m = take()) {
            const auto& idx = by.at(*m);
            const double t = clean[idx[pick(10, idx.size() - 10)]].t;
            const double sog = sz.speed_kn_lo + (sz.speed_kn_hi - sz.speed_kn_lo) * unit(rng);
            each_copy(idx, t, [&](Fix& f) { f.sog_knots = sog; });
            labels.push_back({Kind::ImpossibleSpeed, *m, t, t});
        }
        // Gap: from a report where the vessel is moving, silence it for 15-40
        // minutes. The vessel must report again within 10 minutes of the gap
        // ending, or the end of the gap could not be seen at all.
        // Try vessels until one has a suitable moving report.
        for (bool done = false; !done;) {
            const auto m = take();
            if (!m) break;
            const auto& idx = by.at(*m);
            for (int attempt = 0; attempt < 50 && !done; ++attempt) {
                const Fix& start = clean[idx[pick(5, idx.size() - 5)]];
                const double d = 60.0 * (sz.gap_min_lo + (sz.gap_min_hi - sz.gap_min_lo) * unit(rng));
                const auto after =
                    std::find_if(idx.begin(), idx.end(), [&](std::size_t i) { return clean[i].t > start.t + d; });
                if (!moving(start) || after == idx.end() || clean[*after].t > start.t + d + 600.0) continue;
                for (const std::size_t i : idx)
                    if (clean[i].t > start.t && clean[i].t <= start.t + d) removed[i] = true;
                labels.push_back({Kind::Gap, *m, start.t + d, clean[*after].t});
                done = true;
            }
        }
        // Identity conflict: for 10 minutes, a second transmitter uses B's
        // MMSI. Its reports are copied from another vessel A that is at least
        // between conflict_km_lo and conflict_km_hi from B and reported at least 10
        // times in those 10 minutes.
        if (auto b = take()) {
            const auto& ib = by.at(*b);
            for (int attempt = 0; attempt < 500; ++attempt) {
                const double t0 = clean[ib[pick(5, ib.size() / 2)]].t;
                const double t1 = t0 + 600.0;
                if (t1 + 60.0 > clean[ib.back()].t) continue;
                const std::uint32_t a = all[pick(0, all.size())];
                if (a == *b) continue;
                const auto& ia = by.at(a);
                if (reports_between(clean, ia, t0, t1) < 10) continue;
                const auto first_a =
                    std::find_if(ia.begin(), ia.end(), [&](std::size_t i) { return clean[i].t >= t0; });
                const auto first_b =
                    std::find_if(ib.begin(), ib.end(), [&](std::size_t i) { return clean[i].t >= t0; });
                const double apart = track::haversine_m(clean[*first_a].lat_deg, clean[*first_a].lon_deg,
                                                        clean[*first_b].lat_deg, clean[*first_b].lon_deg);
                if (apart < 1000.0 * sz.conflict_km_lo || apart > 1000.0 * sz.conflict_km_hi) continue;
                for (const std::size_t i : ia) {
                    if (clean[i].t >= t0 && clean[i].t < t1) {
                        Fix copy = clean[i];
                        copy.mmsi = *b;
                        added.push_back(copy);
                    }
                }
                labels.push_back({Kind::IdentityConflict, *b, t0, t1 + 60.0});
                break;
            }
        }
    }

    std::vector<Fix> result;
    result.reserve(out.size() + added.size());
    for (std::size_t i = 0; i < out.size(); ++i)
        if (!removed[i]) result.push_back(out[i]);
    result.insert(result.end(), added.begin(), added.end());
    std::stable_sort(result.begin(), result.end(), [](const Fix& x, const Fix& y) { return x.t < y.t; });
    return result;
}

// ---- reporting ---------------------------------------------------------------

void print_list(const std::vector<Anomaly>& found) {
    std::printf("\nkind,mmsi,time,lat,lon,value,distance_m\n");
    for (const auto& a : found) {
        std::printf("%.*s,%u,%.0f,%.5f,%.5f,%.1f,%.0f\n", static_cast<int>(name(a.kind).size()), name(a.kind).data(),
                    a.mmsi, a.t, a.lat_deg, a.lon_deg, a.value, a.distance_m);
    }
}

void summarize(const std::vector<Fix>& fixes, const std::vector<Anomaly>& found, const anomaly::Stats& s) {
    std::set<std::uint32_t> vessels;
    for (const auto& f : fixes) vessels.insert(f.mmsi);
    const double hours = fixes.empty() ? 0.0 : (fixes.back().t - fixes.front().t) / 3600.0;
    std::printf("reports: %zu, vessels: %zu, span: %.2f h\n", fixes.size(), vessels.size(), hours);
    std::printf("\n| Flag | Count | Vessels flagged |\n|---|---:|---:|\n");
    for (const Kind k : kKinds) {
        std::set<std::uint32_t> flagged;
        std::size_t n = 0;
        for (const auto& a : found) {
            if (a.kind != k) continue;
            ++n;
            flagged.insert(a.mmsi);
        }
        std::printf("| %.*s | %zu | %zu |\n", static_cast<int>(name(k).size()), name(k).data(), n, flagged.size());
    }
    std::printf("\nsecond positions that took over after the first fell silent: %llu\n",
                static_cast<unsigned long long>(s.relocated));
    std::printf("silences not flagged because the network was down for part of them: %llu\n",
                static_cast<unsigned long long>(s.gaps_during_outage));
    std::printf("silences not judged because they began before listening started: %llu\n",
                static_cast<unsigned long long>(s.gaps_before_listening));
    std::printf("silences not flagged because several vessels reappeared together (coverage returned): %llu\n",
                static_cast<unsigned long long>(s.gaps_coverage_returned));
    std::printf("silences not flagged because the vessel had not been heard regularly before: %llu\n",
                static_cast<unsigned long long>(s.gaps_not_regular));
    std::printf("reports from search-and-rescue aircraft, not checked: %llu\n",
                static_cast<unsigned long long>(s.aircraft));
}

struct Score {
    std::array<std::uint64_t, 4> planted{}, found{};
    std::uint64_t extra = 0;  // flags that neither match a planted case nor appear on the unmodified data
};

std::size_t slot(Kind k) {
    return static_cast<std::size_t>(k);
}

std::string conflict_range(const Sizes& sz) {
    std::ostringstream os;
    os << sz.conflict_km_lo;
    if (sz.conflict_km_hi >= 1e6) {
        os << '+';
    } else {
        os << '-' << sz.conflict_km_hi;
    }
    return os.str();
}

void evaluate(const std::vector<Fix>& clean, const std::vector<Anomaly>& baseline, int n, int seeds, const Sizes& sz,
              bool print_missed) {
    std::vector<std::pair<int, Planted>> missed;
    std::set<std::tuple<Kind, std::uint32_t, double>> known;
    for (const auto& a : baseline) known.emplace(a.kind, a.mmsi, a.t);

    Score total;
    for (int seed = 1; seed <= seeds; ++seed) {
        std::vector<Planted> labels;
        const auto input = plant(clean, n, static_cast<std::uint64_t>(seed), sz, labels);
        const auto found = detect(input);
        for (const auto& l : labels) {
            ++total.planted.at(slot(l.kind));
            const bool hit = std::any_of(found.begin(), found.end(), [&](const Anomaly& a) {
                return a.kind == l.kind && a.mmsi == l.mmsi && a.t >= l.from && a.t <= l.to;
            });
            if (hit) {
                ++total.found.at(slot(l.kind));
            } else {
                missed.emplace_back(seed, l);
            }
        }
        for (const auto& a : found) {
            if (known.contains({a.kind, a.mmsi, a.t})) continue;
            // Any flag on a planted vessel inside its planted window is explained
            // by the plant (for example the jump that opens an identity conflict).
            const bool explained = std::any_of(labels.begin(), labels.end(), [&](const Planted& l) {
                return a.mmsi == l.mmsi && a.t >= l.from && a.t <= l.to;
            });
            if (!explained) ++total.extra;
        }
    }
    std::printf(
        "\nplanted: %d per kind per seed, seeds 1-%d; jumps %g-%g km, speeds %g-%g kn, gaps %g-%g min, "
        "second transmitter %s km away\n\n",
        n, seeds, sz.jump_km_lo, sz.jump_km_hi, sz.speed_kn_lo, sz.speed_kn_hi, sz.gap_min_lo, sz.gap_min_hi,
        conflict_range(sz).c_str());
    std::printf("| Planted anomaly | Planted | Detected | Recall |\n|---|---:|---:|---:|\n");
    for (const Kind k : kKinds) {
        const auto p = total.planted.at(slot(k));
        const auto f = total.found.at(slot(k));
        std::printf("| %.*s | %llu | %llu | %.1f%% |\n", static_cast<int>(name(k).size()), name(k).data(),
                    static_cast<unsigned long long>(p), static_cast<unsigned long long>(f),
                    p > 0 ? 100.0 * static_cast<double>(f) / static_cast<double>(p) : 0.0);
    }
    std::printf("\nflags caused by planting that match no planted case: %llu\n",
                static_cast<unsigned long long>(total.extra));
    if (print_missed) {
        std::printf("\nmissed planted cases (seed,kind,mmsi,from,to):\n");
        for (const auto& [seed, l] : missed) {
            std::printf("%d,%.*s,%u,%.0f,%.0f\n", seed, static_cast<int>(name(l.kind).size()), name(l.kind).data(),
                        l.mmsi, l.from, l.to);
        }
    }
}

// "LO,HI" -> lo, hi
bool range(const std::string& v, double& lo, double& hi) {
    const auto comma = v.find(',');
    if (comma == std::string::npos) return false;
    lo = std::stod(v.substr(0, comma));
    hi = std::stod(v.substr(comma + 1));
    return lo <= hi;
}

std::optional<Args> parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string_view k = argv[i];
        if (k == "--list" || k == "--missed") {
            (k == "--list" ? a.list : a.missed) = true;
            continue;
        }
        if (i + 1 >= argc) return std::nullopt;
        const std::string v = argv[++i];
        if (k == "--dk-csv") {
            a.dk_csv = v;
        } else if (k == "--nmea") {
            a.nmea = v;
        } else if (k == "--barentswatch") {
            a.barentswatch = v;
        } else if (k == "--inject") {
            a.inject = std::stoi(v);
        } else if (k == "--seeds") {
            a.seeds = std::stoi(v);
        } else if (k == "--listening-since") {
            a.listening_since = std::stod(v);
        } else if (k == "--jump-km") {
            if (!range(v, a.sizes.jump_km_lo, a.sizes.jump_km_hi)) return std::nullopt;
        } else if (k == "--speed-kn") {
            if (!range(v, a.sizes.speed_kn_lo, a.sizes.speed_kn_hi)) return std::nullopt;
        } else if (k == "--gap-min") {
            if (!range(v, a.sizes.gap_min_lo, a.sizes.gap_min_hi)) return std::nullopt;
        } else if (k == "--conflict-km") {
            if (!range(v, a.sizes.conflict_km_lo, a.sizes.conflict_km_hi)) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    const int sources = int{a.dk_csv.has_value()} + int{a.nmea.has_value()} + int{a.barentswatch.has_value()};
    if (sources != 1 || a.inject < 0 || a.seeds < 1) return std::nullopt;
    return a;
}

int run_main(int argc, char** argv) {
    const auto a = parse(argc, argv);
    if (!a) {
        std::cerr << "usage: mt-anomaly (--dk-csv FILE | --nmea FILE | --barentswatch FILE)\n"
                     "                  [--inject N [--seeds S] [--missed]] [--list] [--listening-since EPOCH]\n"
                     "                  [--jump-km LO,HI] [--speed-kn LO,HI] [--gap-min LO,HI] [--conflict-km LO,HI]\n";
        return 2;
    }
    g_listening_since = a->listening_since;
    const auto fixes = load(*a);
    const anomaly::Params p;
    std::printf(
        "rules: speed limit %.0f kn, jump slack %.0f m, gap %.0f s when moving >= %.0f kn, "
        "identity conflict after %d reports from a second position\n",
        p.max_speed_kn, p.slack_m, p.gap_s, p.gap_min_sog_kn, p.conflict_min_fixes);
    anomaly::Stats stats;
    const auto found = detect(fixes, &stats);
    summarize(fixes, found, stats);
    if (a->inject > 0) evaluate(fixes, found, a->inject, a->seeds, a->sizes, a->missed);
    if (a->list) print_list(found);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run_main(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "mt-anomaly: " << e.what() << '\n';
        return 1;
    }
}

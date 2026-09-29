// mt-bench: how fast is each way of finding every pair of vessels within
// 6 nm of each other, on real traffic and at larger scale?
//
// Workloads:
//   real      the busiest minute of each Danish slice (every vessel that
//             reported in that minute), and every vessel in a BarentsWatch
//             recording
//   global    copies of the largest real snapshot placed around the globe,
//             up to a million vessels: a worldwide feed at real density
//   port      the first snapshot given made denser, as in the busiest straits
//
// Every method must return exactly the same pairs; the run fails otherwise.
// Times are the median of several runs and include converting positions,
// building the index, querying and sorting the result. Distance checks
// (candidate pairs whose distance was computed) do not depend on the machine.
//
//   mt-bench --dk-csv data/oresund-2026-04-22.csv --dk-csv data/skagen-2026-04-22.csv [--barentswatch live.jsonl]
//   [--quick] valgrind --tool=callgrind mt-bench --barentswatch live.jsonl --profile grid 100000

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "maritime/spatial/pairs.hpp"
#include "maritime/track/barentswatch.hpp"
#include "maritime/track/geo.hpp"
#include "maritime/track/tracker.hpp"

namespace {

using namespace maritime;
using spatial::Method;
using spatial::Point;

constexpr double kRadius = 6.0 * 1852.0;
constexpr std::array<Method, 4> kMethods{Method::BruteForce, Method::LatitudeSweep, Method::Grid, Method::KdTree};

struct Workload {
    std::string name;
    std::vector<Point> points;
};

std::string file_stem(const std::string& path) {
    const auto slash = path.find_last_of('/');
    const std::string b = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = b.find('.');
    return dot == std::string::npos ? b : b.substr(0, dot);
}

// The minute with the most distinct vessels, and their last position in it.
Workload busiest_minute(const std::string& name, const std::vector<track::Fix>& fixes) {
    std::map<std::int64_t, std::unordered_map<std::uint32_t, Point>> by_minute;
    for (const auto& f : fixes) {
        by_minute[static_cast<std::int64_t>(std::floor(f.t / 60.0))][f.mmsi] = {f.lat_deg, f.lon_deg};
    }
    const auto best = std::max_element(by_minute.begin(), by_minute.end(),
                                       [](const auto& a, const auto& b) { return a.second.size() < b.second.size(); });
    Workload w{name, {}};
    if (best == by_minute.end()) return w;
    std::vector<std::pair<std::uint32_t, Point>> v(best->second.begin(), best->second.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [m, p] : v) w.points.push_back(p);
    return w;
}

Workload load_dk(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<track::Fix> fixes;
    std::string line;
    while (std::getline(in, line)) {
        if (auto f = track::fix_from_dk_csv(line)) fixes.push_back(*f);
    }
    return busiest_minute(file_stem(path) + ", busiest minute", fixes);
}

// Every vessel's last position in a recording (a whole live stream at once).
Workload load_bw(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::map<std::uint32_t, Point> last;
    std::string line;
    while (std::getline(in, line)) {
        if (auto r = track::parse_barentswatch(line); r.fix) last[r.fix->mmsi] = {r.fix->lat_deg, r.fix->lon_deg};
    }
    Workload w{file_stem(path) + ", every vessel", {}};
    for (const auto& [m, p] : last) w.points.push_back(p);
    return w;
}

// Copies of `base` rotated to random places on the globe (a rotation of the
// sphere keeps distances), until there are `n` points.
Workload global(const Workload& base, std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);  // NOLINT(cert-msc32-c,cert-msc51-cpp): seeded so runs are reproducible
    std::uniform_real_distribution<double> u(0.0, 1.0);
    Workload w{"", {}};
    constexpr double kDeg = std::numbers::pi / 180.0;
    while (w.points.size() < n) {
        // A random rotation: random axis (uniform on the sphere) and angle.
        const double z = 2.0 * u(rng) - 1.0;
        const double phi = 2.0 * std::numbers::pi * u(rng);
        const double s = std::sqrt(1.0 - z * z);
        const double ax = s * std::cos(phi);
        const double ay = s * std::sin(phi);
        const double az = z;
        const double ang = 2.0 * std::numbers::pi * u(rng);
        const double c = std::cos(ang);
        const double sn = std::sin(ang);
        for (const auto& p : base.points) {
            if (w.points.size() >= n) break;
            const double x = std::cos(p.lat_deg * kDeg) * std::cos(p.lon_deg * kDeg);
            const double y = std::cos(p.lat_deg * kDeg) * std::sin(p.lon_deg * kDeg);
            const double zz = std::sin(p.lat_deg * kDeg);
            // Rodrigues' rotation formula.
            const double dot = ax * x + ay * y + az * zz;
            const double rx = x * c + (ay * zz - az * y) * sn + ax * dot * (1.0 - c);
            const double ry = y * c + (az * x - ax * zz) * sn + ay * dot * (1.0 - c);
            const double rz = zz * c + (ax * y - ay * x) * sn + az * dot * (1.0 - c);
            w.points.push_back({std::asin(std::clamp(rz, -1.0, 1.0)) / kDeg, std::atan2(ry, rx) / kDeg});
        }
    }
    return w;
}

// `base` made denser: each vessel repeated with a small random offset.
Workload denser(const Workload& base, std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);  // NOLINT(cert-msc32-c,cert-msc51-cpp): seeded so runs are reproducible
    std::normal_distribution<double> g(0.0, 1.0);
    Workload w{"", {}};
    for (std::size_t i = 0; w.points.size() < n; ++i) {
        const Point& p = base.points[i % base.points.size()];
        const track::LocalFrame f(p.lat_deg, p.lon_deg);
        Point q{};
        f.to_geo({1000.0 * g(rng), 1000.0 * g(rng)}, q.lat_deg, q.lon_deg);
        w.points.push_back(q);
    }
    return w;
}

struct Result {
    double ms = NAN;
    std::uint64_t checks = 0;
    std::size_t pairs = 0;
};

Result measure(const std::vector<Point>& pts, Method m, double budget_s, int max_runs) {
    std::vector<double> times;
    Result r;
    const auto start = std::chrono::steady_clock::now();
    while (static_cast<int>(times.size()) < max_runs) {
        spatial::PairStats st;
        const auto t0 = std::chrono::steady_clock::now();
        const auto pairs = spatial::pairs_within(pts, kRadius, m, &st);
        const auto t1 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        r.checks = st.distance_checks;
        r.pairs = pairs.size();
        if (times.size() >= 3 && std::chrono::duration<double>(t1 - start).count() > budget_s) break;
    }
    std::sort(times.begin(), times.end());
    r.ms = times[times.size() / 2];
    return r;
}

std::string with_commas(std::uint64_t v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

bool run_table(const char* title, const std::vector<Workload>& ws, std::size_t brute_limit, double budget_s) {
    bool ok = true;
    std::printf("\n### %s\n\n", title);
    std::printf(
        "| Workload | Vessels | Pairs within 6 nm | Brute force ms (checks) | Latitude sweep ms (checks) | "
        "Grid ms (checks) | k-d tree ms (checks) |\n|---|---:|---:|---:|---:|---:|---:|\n");
    for (const auto& w : ws) {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> reference;
        bool have_reference = false;
        std::printf("| %s | %s |", w.name.c_str(), with_commas(w.points.size()).c_str());
        std::array<std::optional<Result>, 4> res;
        for (std::size_t k = 0; k < kMethods.size(); ++k) {
            const Method m = kMethods.at(k);
            if (m == Method::BruteForce && w.points.size() > brute_limit) continue;
            res.at(k) = measure(w.points, m, budget_s, 15);
            // Equality of the pair lists, not just the counts.
            const auto pairs = spatial::pairs_within(w.points, kRadius, m);
            if (!have_reference) {
                reference = pairs;
                have_reference = true;
            } else if (pairs != reference) {
                ok = false;
                (void)std::fprintf(stderr, "MISMATCH: %s disagrees on %s\n", std::string(spatial::name(m)).c_str(),
                                   w.name.c_str());
            }
        }
        std::printf(" %s |", with_commas(reference.size()).c_str());
        for (const auto& r : res) {
            if (!r) {
                std::printf(" not run |");
            } else {
                std::printf(" %.*f (%s) |", r->ms < 10 ? 2 : 0, r->ms, with_commas(r->checks).c_str());
            }
        }
        std::printf("\n");
        (void)std::fflush(stdout);
    }
    return ok;
}

int run_main(int argc, char** argv) {
    std::vector<Workload> real;
    bool quick = false;
    std::optional<std::pair<Method, std::size_t>> profile;  // run one method once on the global workload
    for (int i = 1; i < argc; ++i) {
        const std::string_view k = argv[i];
        if (k == "--quick") {
            quick = true;
        } else if (k == "--dk-csv" && i + 1 < argc) {
            real.push_back(load_dk(argv[++i]));
        } else if (k == "--barentswatch" && i + 1 < argc) {
            real.push_back(load_bw(argv[++i]));
        } else if (k == "--profile" && i + 2 < argc) {
            const std::string_view m = argv[++i];
            const std::size_t n = std::stoul(argv[++i]);
            for (const Method x : kMethods)
                if (spatial::name(x) == m) profile = std::make_pair(x, n);
            if (!profile) {
                std::cerr << "mt-bench: unknown method " << m << '\n';
                return 2;
            }
        } else {
            std::cerr << "usage: mt-bench (--dk-csv FILE | --barentswatch FILE)... [--quick | --profile METHOD N]\n";
            return 2;
        }
    }
    if (real.empty()) {
        std::cerr << "mt-bench: give at least one --dk-csv or --barentswatch file\n";
        return 2;
    }
    if (profile) {  // for a profiler: build the workload, then one call
        const Workload& b = *std::max_element(real.begin(), real.end(), [](const Workload& a, const Workload& c) {
            return a.points.size() < c.points.size();
        });
        const Workload w = global(b, profile->second, 1);
        spatial::PairStats st;
        const auto pairs = spatial::pairs_within(w.points, kRadius, profile->first, &st);
        std::printf(
            "%s on %zu vessels: %zu pairs, %llu distance checks; convert %.1f ms, search %.1f ms, sort %.1f ms\n",
            std::string(spatial::name(profile->first)).c_str(), w.points.size(), pairs.size(),
            static_cast<unsigned long long>(st.distance_checks), st.convert_ms, st.search_ms, st.sort_ms);
        return 0;
    }
    const double budget = quick ? 0.3 : 2.0;
    bool ok = run_table("Real traffic", real, 1000000, budget);

    // The largest real snapshot, placed around the globe.
    const Workload& base = *std::max_element(real.begin(), real.end(), [](const Workload& a, const Workload& b) {
        return a.points.size() < b.points.size();
    });
    std::vector<Workload> glob;
    const std::vector<std::size_t> sizes =
        quick ? std::vector<std::size_t>{10000, 100000} : std::vector<std::size_t>{10000, 100000, 1000000};
    for (const std::size_t n : sizes) {
        glob.push_back(global(base, n, 1));
        glob.back().name = "copies of \"" + base.name + "\" around the globe";
    }
    ok = run_table("Global feed: the largest real snapshot, copied around the globe", glob, 20000, budget) && ok;

    // The first Danish snapshot, denser.
    const Workload& port = real.front();
    std::vector<Workload> dense;
    const std::vector<std::size_t> dsizes =
        quick ? std::vector<std::size_t>{1000, 2000} : std::vector<std::size_t>{1000, 2000, 5000};
    for (const std::size_t n : dsizes) {
        dense.push_back(denser(port, n, 2));
        dense.back().name = "\"" + port.name + "\" made denser";
    }
    ok = run_table("Busy port: the first snapshot given, made denser", dense, 1000000, budget) && ok;

    std::printf("\nall methods returned identical pairs: %s\n", ok ? "yes" : "NO");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run_main(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "mt-bench: " << e.what() << '\n';
        return 1;
    }
}

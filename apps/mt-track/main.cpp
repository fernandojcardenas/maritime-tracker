// mt-track: run the tracker over recorded traffic and measure how well it
// predicts each vessel's next reported position, against two baselines:
//   hold  - the vessel stays at its last reported position
//   dr    - dead reckoning from the last report's speed and course
//   kf    - the Kalman track's prediction
// Errors are great-circle distances to the next report, grouped by how far
// ahead the prediction is. Reported positions are themselves noisy (roughly
// 10-25 m), so no method can score zero.
//
//   mt-track --dk-csv oresund.csv --eval-from 1776862800 --eval-to 1776866400
//   mt-track --dk-csv oresund.csv --eval-to 1776862800 --sigma-a 0.01,0.05,0.1 --pos-sigma 2,5,15
//   mt-track --nmea recording.nmea       (tag-block timestamps required)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <random>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "maritime/ais/decoder.hpp"
#include "maritime/track/geo.hpp"
#include "maritime/track/tracker.hpp"

namespace {

using namespace maritime;

struct Args {
    std::optional<std::string> dk_csv;
    std::optional<std::string> nmea;
    double eval_from = -std::numeric_limits<double>::infinity();
    double eval_to = std::numeric_limits<double>::infinity();
    std::vector<double> sigma_a{track::KalmanParams{}.sigma_a};
    std::vector<double> pos_sigma{track::Fix{}.pos_sigma_m};
    std::vector<double> sigma_vel{track::KalmanParams{}.sigma_vel};
    double noise_m = 0.0;       // Gaussian position noise added to every report
    double drop_velocity = 0.0; // probability that a report loses SOG/COG
    double outliers = 0.0;      // probability that a report is displaced 500-2000 m
};

constexpr std::array<double, 7> kEdges{0.5, 10, 30, 60, 180, 600, 1800};

std::string bucket_name(std::size_t i) {
    std::ostringstream os;
    os << kEdges.at(i) << "-" << kEdges.at(i + 1) << " s";
    return os.str();
}

std::optional<std::size_t> bucket_of(double dt) {
    for (std::size_t i = 0; i + 1 < kEdges.size(); ++i) {
        if (dt >= kEdges.at(i) && dt < kEdges.at(i + 1)) return i;
    }
    return std::nullopt;
}

struct Errors {
    std::vector<double> hold, dr, kf;
};

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return std::nan("");
    const auto k = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

struct Result {
    std::array<Errors, kEdges.size() - 1> all{};
    std::array<Errors, kEdges.size() - 1> moving{};
    track::TrackerStats stats;
    std::uint64_t evaluated = 0;
    std::uint64_t inside_gate = 0;
    // Gate behaviour on reports in the evaluation window (injected outliers vs genuine reports).
    std::uint64_t outliers = 0, outliers_rejected = 0, genuine = 0, genuine_rejected = 0;
};

std::vector<track::Fix> load(const Args& a) {
    std::vector<track::Fix> fixes;
    std::string line;
    if (a.dk_csv) {
        std::ifstream in(*a.dk_csv);
        if (!in) throw std::runtime_error("cannot open " + *a.dk_csv);
        while (std::getline(in, line)) {
            if (auto f = track::fix_from_dk_csv(line)) fixes.push_back(*f);
        }
    } else if (a.nmea) {
        std::ifstream in(*a.nmea);
        if (!in) throw std::runtime_error("cannot open " + *a.nmea);
        ais::Decoder d;
        while (std::getline(in, line)) {
            if (auto dm = d.feed(line)) {
                if (auto f = track::fix_from(*dm)) fixes.push_back(*f);
            }
        }
    }
    // Reports from several base stations can arrive slightly out of order.
    std::stable_sort(fixes.begin(), fixes.end(), [](const auto& x, const auto& y) { return x.t < y.t; });
    return fixes;
}

// Corrupts a copy of the reports to test robustness; the clean reports stay
// the ground truth that every method is scored against. Seeded, so runs are
// reproducible.
std::vector<track::Fix> perturb(const std::vector<track::Fix>& clean, const Args& a, std::vector<bool>& is_outlier) {
    std::vector<track::Fix> out = clean;
    is_outlier.assign(clean.size(), false);
    if (a.noise_m <= 0.0 && a.drop_velocity <= 0.0 && a.outliers <= 0.0) return out;
    std::mt19937_64 rng(42);  // NOLINT(cert-msc32-c,cert-msc51-cpp): fixed seed so evaluations are reproducible
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (std::size_t i = 0; i < out.size(); ++i) {
        auto& f = out[i];
        const track::LocalFrame fr(f.lat_deg, f.lon_deg);
        double de = a.noise_m * gauss(rng);
        double dn = a.noise_m * gauss(rng);
        if (unit(rng) < a.outliers) {  // a wild report 500-2000 m off, in a random direction
            const double r = 500.0 + 1500.0 * unit(rng);
            const double th = 2.0 * std::numbers::pi * unit(rng);
            de += r * std::cos(th);
            dn += r * std::sin(th);
            is_outlier[i] = true;
        }
        fr.to_geo({de, dn}, f.lat_deg, f.lon_deg);
        if (unit(rng) < a.drop_velocity) {
            f.sog_knots.reset();
            f.cog_deg.reset();
        }
    }
    return out;
}

// Each method predicts where the vessel is at the time of the next report
// and is scored against the clean position of that report. Hold and dead
// reckoning predict from the vessel's previous raw report (they have no
// notion of a bad report); the Kalman filter predicts from its track.
Result run(const std::vector<track::Fix>& truth, const std::vector<track::Fix>& input, const std::vector<bool>& is_outlier,
           const Args& a, double sigma_a, double pos_sigma, double sigma_vel) {
    track::TrackerParams params;
    params.kalman.sigma_a = sigma_a;
    params.kalman.sigma_vel = sigma_vel;
    track::Tracker tracker(params);
    std::unordered_map<std::uint32_t, track::Fix> last;  // previous raw report per vessel
    Result r;
    double next_expire = 0.0;

    for (std::size_t i = 0; i < input.size(); ++i) {
        track::Fix f = input[i];
        const track::Fix& clean = truth[i];
        f.pos_sigma_m = pos_sigma;
        if (f.t >= next_expire) {
            tracker.expire(f.t);
            next_expire = f.t + 60.0;
        }
        const auto prev = last.find(f.mmsi);
        const auto pred = tracker.predict(f.mmsi, f.t);
        const auto outcome = tracker.add(f);
        const bool rejected = outcome == track::FixOutcome::Rejected || outcome == track::FixOutcome::Restarted;
        const bool counted = outcome != track::FixOutcome::Started && outcome != track::FixOutcome::Duplicate &&
                             outcome != track::FixOutcome::OutOfOrder;
        if (counted && f.t >= a.eval_from && f.t < a.eval_to) {
            if (is_outlier[i]) {
                ++r.outliers;
                if (rejected) ++r.outliers_rejected;
            } else {
                ++r.genuine;
                if (rejected) ++r.genuine_rejected;
            }
        }
        // Repeats of the same report (several base stations hear one
        // transmission) are skipped for every method alike.
        if (prev != last.end() && f.t - prev->second.t < params.min_dt_s) continue;

        if (prev != last.end()) {
            const auto& p = prev->second;
            const double dt = f.t - p.t;
            const auto b = bucket_of(dt);
            if (pred && b && f.t >= a.eval_from && f.t < a.eval_to) {
                const double e_hold = track::haversine_m(p.lat_deg, p.lon_deg, clean.lat_deg, clean.lon_deg);
                double e_dr = e_hold;
                if (p.sog_knots && p.cog_deg) {
                    const auto v = track::velocity_from_sog_cog(*p.sog_knots, *p.cog_deg);
                    const track::LocalFrame fr(p.lat_deg, p.lon_deg);
                    double lat = 0.0;
                    double lon = 0.0;
                    fr.to_geo({v.east_m * dt, v.north_m * dt}, lat, lon);
                    e_dr = track::haversine_m(lat, lon, clean.lat_deg, clean.lon_deg);
                }
                const double e_kf = track::haversine_m(pred->lat_deg, pred->lon_deg, clean.lat_deg, clean.lon_deg);
                auto push = [&](Errors& e) {
                    e.hold.push_back(e_hold);
                    e.dr.push_back(e_dr);
                    e.kf.push_back(e_kf);
                };
                push(r.all.at(*b));
                if (clean.sog_knots && *clean.sog_knots >= 2.0) push(r.moving.at(*b));
                ++r.evaluated;
                if (outcome == track::FixOutcome::Updated) ++r.inside_gate;
            }
        }
        last[f.mmsi] = f;
    }
    r.stats = tracker.stats();
    return r;
}

void print_table(const char* title, const std::array<Errors, kEdges.size() - 1>& rows) {
    std::printf("\n%s\n\n", title);
    std::printf("| Horizon | Reports | Hold median / p90 (m) | Dead reckoning median / p90 (m) | Kalman median / p90 (m) |\n");
    std::printf("|---|---:|---:|---:|---:|\n");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& e = rows.at(i);
        if (e.kf.empty()) continue;
        std::printf("| %s | %zu | %.0f / %.0f | %.0f / %.0f | %.0f / %.0f |\n", bucket_name(i).c_str(), e.kf.size(),
                    quantile(e.hold, 0.5), quantile(e.hold, 0.9), quantile(e.dr, 0.5), quantile(e.dr, 0.9),
                    quantile(e.kf, 0.5), quantile(e.kf, 0.9));
    }
}

// Mean over prediction horizons of the median error on moving vessels.
double score(const Result& r, bool kalman) {
    double s = 0.0;
    int n = 0;
    for (const auto& e : r.moving) {
        if (e.kf.size() < 30) continue;
        s += quantile(kalman ? e.kf : e.dr, 0.5);
        ++n;
    }
    return n > 0 ? s / n : std::nan("");
}

std::vector<double> list(const std::string& v) {
    std::vector<double> out;
    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(std::stod(item));
    return out;
}

std::optional<Args> parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view k = argv[i];
        const std::string v = argv[i + 1];
        if (k == "--dk-csv") {
            a.dk_csv = v;
        } else if (k == "--nmea") {
            a.nmea = v;
        } else if (k == "--eval-from") {
            a.eval_from = std::stod(v);
        } else if (k == "--eval-to") {
            a.eval_to = std::stod(v);
        } else if (k == "--sigma-a") {
            a.sigma_a = list(v);
        } else if (k == "--pos-sigma") {
            a.pos_sigma = list(v);
        } else if (k == "--sigma-vel") {
            a.sigma_vel = list(v);
        } else if (k == "--noise") {
            a.noise_m = std::stod(v);
        } else if (k == "--drop-velocity") {
            a.drop_velocity = std::stod(v);
        } else if (k == "--outliers") {
            a.outliers = std::stod(v);
        } else {
            return std::nullopt;
        }
    }
    if (argc % 2 == 0 || a.dk_csv.has_value() == a.nmea.has_value() || a.sigma_a.empty() || a.pos_sigma.empty() ||
        a.sigma_vel.empty()) {
        return std::nullopt;
    }
    return a;
}

int run_main(int argc, char** argv) {
    const auto a = parse(argc, argv);
    if (!a) {
        std::cerr << "usage: mt-track (--dk-csv FILE | --nmea FILE) [--eval-from T] [--eval-to T]\n"
                     "                [--sigma-a X[,Y...]] [--pos-sigma X[,Y...]] [--sigma-vel X[,Y...]]\n"
                     "                [--noise M] [--drop-velocity P] [--outliers P]\n"
                     "  several values in any list run a grid search and print a score table\n";
        return 2;
    }
    const auto fixes = load(*a);
    std::vector<bool> is_outlier;
    const auto input = perturb(fixes, *a, is_outlier);
    std::printf("fixes loaded: %zu\n", fixes.size());
    std::printf("perturbation: noise %.1f m, velocity dropped %.0f%%, outliers %.1f%% (seed 42)\n", a->noise_m,
                100.0 * a->drop_velocity, 100.0 * a->outliers);

    if (a->sigma_a.size() * a->pos_sigma.size() * a->sigma_vel.size() > 1) {
        std::printf("\n| sigma_a (m/s^2) | position sigma (m) | velocity sigma (m/s) | Kalman score (m) | dead reckoning score (m) |\n|---:|---:|---:|---:|---:|\n");
        for (const double sa : a->sigma_a)
            for (const double ps : a->pos_sigma)
                for (const double sv : a->sigma_vel)
                {
                    const auto res = run(fixes, input, is_outlier, *a, sa, ps, sv);
                    std::printf("| %.3f | %.1f | %.2f | %.2f | %.2f |\n", sa, ps, sv, score(res, true), score(res, false));
                }
        std::printf("\nscore: mean over prediction horizons of the median error on moving vessels\n");
        return 0;
    }

    const auto r = run(fixes, input, is_outlier, *a, a->sigma_a.front(), a->pos_sigma.front(), a->sigma_vel.front());
    const auto& s = r.stats;
    std::printf("sigma_a %.3f m/s^2, position sigma %.1f m, velocity sigma %.2f m/s\n", a->sigma_a.front(),
                a->pos_sigma.front(), a->sigma_vel.front());
    std::printf("tracker: %llu fixes, %llu tracks started, %llu updates, %llu duplicates, %llu out of order, "
                "%llu rejected by the gate, %llu restarts, %llu expired, %llu re-anchored, %llu numeric resets\n",
                static_cast<unsigned long long>(s.fixes), static_cast<unsigned long long>(s.started),
                static_cast<unsigned long long>(s.updated), static_cast<unsigned long long>(s.duplicates),
                static_cast<unsigned long long>(s.out_of_order), static_cast<unsigned long long>(s.rejected),
                static_cast<unsigned long long>(s.restarted), static_cast<unsigned long long>(s.expired),
                static_cast<unsigned long long>(s.reanchored), static_cast<unsigned long long>(s.numeric_resets));
    std::printf("evaluated reports: %llu, inside the gate: %.2f%%\n", static_cast<unsigned long long>(r.evaluated),
                r.evaluated > 0 ? 100.0 * static_cast<double>(r.inside_gate) / static_cast<double>(r.evaluated) : 0.0);
    const auto pct = [](std::uint64_t n, std::uint64_t d) { return d > 0 ? 100.0 * static_cast<double>(n) / static_cast<double>(d) : 0.0; };
    std::printf("gate: rejected %llu of %llu injected outliers (%.1f%%) and %llu of %llu genuine reports (%.2f%%)\n",
                static_cast<unsigned long long>(r.outliers_rejected), static_cast<unsigned long long>(r.outliers),
                pct(r.outliers_rejected, r.outliers), static_cast<unsigned long long>(r.genuine_rejected),
                static_cast<unsigned long long>(r.genuine), pct(r.genuine_rejected, r.genuine));
    print_table("All vessels", r.all);
    print_table("Moving vessels (SOG >= 2 kn at the report)", r.moving);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run_main(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "mt-track: " << e.what() << '\n';
        return 1;
    }
}

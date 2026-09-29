// mt-risk: collision risk on recorded traffic, checked against what the
// vessels actually did.
//
// Every minute of data time, every pair of vessels under way is assessed
// (closest point of approach, COLREGs encounter type, who gives way) from two
// predictors of each vessel's motion:
//   dr  the last report's position, moved along its reported speed and course
//   kf  the Kalman track (M3), moved along its velocity
// Two checks against the recorded future:
//   1. Accuracy: for every pair predicted to pass within 0.5 nm within 20
//      minutes, how close did they really pass, and when?
//   2. Behaviour: for each encounter (first moment a pair is at risk), did
//      the vessel that has to give way change course or speed more often
//      than the vessel that stands on, and more often than vessels that were
//      in no encounter at all? In crossing encounters, did the give-way
//      vessel pass astern of the stand-on vessel (Rule 15)?
//
//   mt-risk --dk-csv oresund.csv [--max-dcpa-nm 0.25] [--commercial-only] [--list]

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
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "maritime/risk/collision.hpp"
#include "maritime/track/geo.hpp"
#include "maritime/track/tracker.hpp"

namespace {

using namespace maritime;
using risk::Encounter;
using risk::Motion;
using risk::Role;
using track::Fix;

constexpr double kStep = 60.0;                             // assess every minute of data time
constexpr double kFresh = 60.0;                            // a vessel needs a report this recent to be assessed
constexpr double kMaxGap = 120.0;                          // ground truth: interpolate across gaps up to this long
constexpr double kDevelopingRange = risk::kNauticalMileM;  // behaviour: first at risk at least 1 nm apart
constexpr double kDevelopingTcpa = 240.0;                  // ... and at least 4 minutes before CPA
constexpr double kActed = 0.1 * risk::kNauticalMileM;      // a manoeuvre that opened the CPA by 0.1 nm or more

// ---- ground truth ------------------------------------------------------------

struct Report {
    double t, lat, lon;
    std::optional<double> sog, cog;
};

using Truth = std::unordered_map<std::uint32_t, std::vector<Report>>;

Truth build_truth(const std::vector<Fix>& fixes) {
    Truth truth;
    for (const auto& f : fixes) {
        auto& v = truth[f.mmsi];
        if (!v.empty() && v.back().t == f.t) continue;  // repeats of one transmission
        v.push_back({f.t, f.lat_deg, f.lon_deg, f.sog_knots, f.cog_deg});
    }
    return truth;
}

// Position at time t, interpolated between reports at most kMaxGap apart.
std::optional<std::pair<double, double>> position_at(const std::vector<Report>& r, double t) {
    const auto it = std::lower_bound(r.begin(), r.end(), t, [](const Report& x, double v) { return x.t < v; });
    if (it != r.end() && it->t == t) return std::make_pair(it->lat, it->lon);
    if (it == r.begin() || it == r.end()) return std::nullopt;
    const Report& b = *it;
    const Report& a = *(it - 1);
    if (b.t - a.t > kMaxGap) return std::nullopt;
    const double w = (t - a.t) / (b.t - a.t);
    return std::make_pair(a.lat + w * (b.lat - a.lat), a.lon + w * (b.lon - a.lon));
}

struct Actual {
    double t_min = 0.0;
    double d_min = 0.0;
};

// Closest the two vessels really came in [t0, t1], or nullopt if either
// vessel's reports do not cover the whole window.
std::optional<Actual> actual_closest(const std::vector<Report>& a, const std::vector<Report>& b, double t0, double t1) {
    std::vector<double> times{t0, t1};
    for (const auto* s : {&a, &b}) {
        for (const auto& r : *s)
            if (r.t > t0 && r.t < t1) times.push_back(r.t);
    }
    std::sort(times.begin(), times.end());
    Actual best{t0, INFINITY};
    for (const double t : times) {
        const auto pa = position_at(a, t);
        const auto pb = position_at(b, t);
        if (!pa || !pb) return std::nullopt;
        const double d = track::haversine_m(pa->first, pa->second, pb->first, pb->second);
        if (d < best.d_min) best = Actual{t, d};
    }
    return best;
}

// How much a vessel's own manoeuvring changed the passing distance: the real
// closest approach minus the closest approach had this vessel held its course
// and speed from t0 (dead reckoning from its last report), while the other
// vessel did what it really did. Positive: its action opened the distance.
std::optional<double> contribution(const std::vector<Report>& self, const std::vector<Report>& other, double t0,
                                   double t1, double actual_min) {
    const auto it = std::upper_bound(self.begin(), self.end(), t0, [](double v, const Report& x) { return v < x.t; });
    if (it == self.begin()) return std::nullopt;
    const Report& base = *(it - 1);
    if (!base.sog || !base.cog || t0 - base.t > kFresh) return std::nullopt;
    const auto v = track::velocity_from_sog_cog(*base.sog, *base.cog);
    std::vector<double> times{t0, t1};
    for (const auto& r : other)
        if (r.t > t0 && r.t < t1) times.push_back(r.t);
    double best = INFINITY;
    for (const double t : times) {
        const auto po = position_at(other, t);
        if (!po) return std::nullopt;
        double lat = 0.0;
        double lon = 0.0;
        const track::LocalFrame f(base.lat, base.lon);
        f.to_geo({v.east_m * (t - base.t), v.north_m * (t - base.t)}, lat, lon);
        best = std::min(best, track::haversine_m(lat, lon, po->first, po->second));
    }
    return actual_min - best;
}

// ---- predictors --------------------------------------------------------------

Motion moved(std::uint32_t mmsi, double lat, double lon, double ve, double vn, double dt) {
    Motion m{mmsi, lat, lon, ve, vn};
    const track::LocalFrame f(lat, lon);
    f.to_geo({ve * dt, vn * dt}, m.lat_deg, m.lon_deg);
    return m;
}

struct Snapshot {
    std::vector<Motion> kf, dr;
};

// ---- scoring -----------------------------------------------------------------

constexpr std::array<double, 5> kHorizon{0, 120, 300, 600, 1200};

struct Errors {
    std::vector<double> dcpa, tcpa;
};

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return std::nan("");
    const auto k = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

struct EncounterRecord {
    std::uint32_t a = 0, b = 0;
    double t0 = 0.0, last = 0.0;
    risk::Assessment first;
};

struct Behaviour {
    std::uint64_t n = 0;
    std::vector<double> give_way, stand_on;  // each vessel's contribution to the passing distance (m)
    std::uint64_t give_way_more = 0;         // give-way opened the CPA more than stand-on did (crossing, overtaking)
    std::uint64_t passed_astern_known = 0, passed_astern = 0;
};

int run_main(int argc, char** argv) {
    std::optional<std::string> csv;
    bool list = false;
    bool commercial_only = false;
    risk::Params params;
    for (int i = 1; i < argc; ++i) {
        const std::string_view k = argv[i];
        if (k == "--list") {
            list = true;
        } else if (k == "--commercial-only") {
            commercial_only = true;
        } else if (k == "--dk-csv" && i + 1 < argc) {
            csv = argv[++i];
        } else if (k == "--max-dcpa-nm" && i + 1 < argc) {
            params.max_dcpa_m = std::stod(argv[++i]) * risk::kNauticalMileM;
        } else {
            csv.reset();
            break;
        }
    }
    if (!csv) {
        std::cerr << "usage: mt-risk --dk-csv FILE [--max-dcpa-nm X] [--commercial-only] [--list]\n";
        return 2;
    }
    std::vector<Fix> fixes;
    std::unordered_map<std::uint32_t, std::string> ship_type;  // last column of the Danish CSV
    {
        std::ifstream in(*csv);
        if (!in) throw std::runtime_error("cannot open " + *csv);
        std::string line;
        while (std::getline(in, line)) {
            if (auto f = track::fix_from_dk_csv(line)) {
                fixes.push_back(*f);
                const auto comma = line.rfind(',');
                if (comma != std::string::npos) ship_type[f->mmsi] = line.substr(comma + 1);
            }
        }
    }
    // Rule 18 gives vessels fishing, sailing or restricted in their ability to
    // manoeuvre (pilot, tug, dredging, SAR and similar work) right of way over
    // power-driven vessels, whatever the geometry. --commercial-only keeps
    // encounters between cargo ships, tankers, passenger ships and high-speed
    // craft, where Rules 13-15 alone decide.
    const auto commercial = [&](std::uint32_t mmsi) {
        const auto it = ship_type.find(mmsi);
        if (it == ship_type.end()) return false;
        const std::string& t = it->second;
        return t == "Cargo" || t == "Tanker" || t == "Passenger" || t == "HSC";
    };
    std::stable_sort(fixes.begin(), fixes.end(), [](const Fix& x, const Fix& y) { return x.t < y.t; });
    if (fixes.empty()) throw std::runtime_error("no reports");
    const Truth truth = build_truth(fixes);

    track::Tracker tracker;
    std::unordered_map<std::uint32_t, Fix> last;  // last report with speed and course

    std::array<Errors, kHorizon.size() - 1> err_kf{};
    std::array<Errors, kHorizon.size() - 1> err_dr{};
    std::uint64_t at_risk_kf = 0;
    std::uint64_t scored = 0;
    std::uint64_t unscorable = 0;
    std::uint64_t pairs_examined_max = 0;
    std::map<std::pair<std::uint32_t, std::uint32_t>, EncounterRecord> open;
    std::vector<EncounterRecord> encounters;
    std::set<std::uint32_t> vessels_seen;

    std::size_t i = 0;
    double next = std::floor(fixes.front().t / kStep) * kStep + kStep;
    const double end = fixes.back().t;
    while (next <= end) {
        for (; i < fixes.size() && fixes[i].t < next; ++i) {
            const Fix& f = fixes[i];
            vessels_seen.insert(f.mmsi);
            (void)tracker.add(f);
            if (f.sog_knots && f.cog_deg) last[f.mmsi] = f;
        }
        const double t = next;
        next += kStep;
        tracker.expire(t);

        // Both predictors over the same set of vessels: those with a fresh
        // report carrying speed and course, and a track.
        Snapshot s;
        for (const auto& [mmsi, f] : last) {
            if (t - f.t > kFresh) continue;
            const auto tr = tracker.track(mmsi);
            if (!tr || t - tr->t > kFresh) continue;
            const auto v = track::velocity_from_sog_cog(*f.sog_knots, *f.cog_deg);
            s.dr.push_back(moved(mmsi, f.lat_deg, f.lon_deg, v.east_m, v.north_m, t - f.t));
            s.kf.push_back(moved(mmsi, tr->lat_deg, tr->lon_deg, tr->v_east, tr->v_north, t - tr->t));
        }
        pairs_examined_max = std::max<std::uint64_t>(pairs_examined_max, s.kf.size());
        std::unordered_map<std::uint32_t, const Motion*> dr_of;
        for (const auto& m : s.dr) dr_of[m.mmsi] = &m;

        for (const auto& pa : risk::find_encounters(s.kf, params)) {
            if (commercial_only && (!commercial(pa.mmsi_a) || !commercial(pa.mmsi_b))) continue;
            ++at_risk_kf;
            const auto& as = pa.assessment;
            // Accuracy against the recorded future.
            const double horizon_end = t + std::min(as.cpa.tcpa_s * 2.0 + 120.0, params.max_tcpa_s + 600.0);
            const auto act = actual_closest(truth.at(pa.mmsi_a), truth.at(pa.mmsi_b), t, horizon_end);
            if (act) {
                ++scored;
                std::size_t bin = 0;
                while (bin + 2 < kHorizon.size() && as.cpa.tcpa_s >= kHorizon.at(bin + 1)) ++bin;
                err_kf.at(bin).dcpa.push_back(std::abs(as.cpa.dcpa_m - act->d_min));
                err_kf.at(bin).tcpa.push_back(std::abs(as.cpa.tcpa_s - (act->t_min - t)));
                const auto c = risk::closest_approach(*dr_of.at(pa.mmsi_a), *dr_of.at(pa.mmsi_b));
                if (c.tcpa_s > 0) {
                    err_dr.at(bin).dcpa.push_back(std::abs(c.dcpa_m - act->d_min));
                    err_dr.at(bin).tcpa.push_back(std::abs(c.tcpa_s - (act->t_min - t)));
                } else {  // dr thinks they are already opening: its CPA is now
                    err_dr.at(bin).dcpa.push_back(std::abs(c.range_m - act->d_min));
                    err_dr.at(bin).tcpa.push_back(std::abs(act->t_min - t));
                }
            } else {
                ++unscorable;
            }
            // Encounters: a pair's first minute at risk, until it has been
            // out of risk for more than one step.
            const auto key = std::make_pair(pa.mmsi_a, pa.mmsi_b);
            auto it = open.find(key);
            if (it == open.end() || t - it->second.last > kStep * 1.5) {
                if (it != open.end()) encounters.push_back(it->second);
                open[key] = EncounterRecord{pa.mmsi_a, pa.mmsi_b, t, t, as};
            } else {
                it->second.last = t;
            }
        }
    }
    for (const auto& [k, e] : open) encounters.push_back(e);
    std::sort(encounters.begin(), encounters.end(),
              [](const EncounterRecord& x, const EncounterRecord& y) { return x.t0 < y.t0; });

    // Behaviour, on developing encounters only: first at risk while at least
    // 1 nm apart and at least 4 minutes before CPA, so there was time and
    // room to act (Rule 16).
    std::map<Encounter, Behaviour> by_type;
    std::map<Encounter, std::uint64_t> developing;
    for (const auto& e : encounters) {
        if (e.first.cpa.range_m < kDevelopingRange || e.first.cpa.tcpa_s < kDevelopingTcpa) continue;
        ++developing[e.first.type];
        const auto& ta = truth.at(e.a);
        const auto& tb = truth.at(e.b);
        const double t1 = e.t0 + e.first.cpa.tcpa_s * 2.0 + 120.0;
        const auto act = actual_closest(ta, tb, e.t0, t1);
        if (!act) continue;
        const auto ca = contribution(ta, tb, e.t0, t1, act->d_min);
        const auto cb = contribution(tb, ta, e.t0, t1, act->d_min);
        if (!ca || !cb) continue;
        Behaviour& b = by_type[e.first.type];
        ++b.n;
        (e.first.role_a == Role::GiveWay ? b.give_way : b.stand_on).push_back(*ca);
        (e.first.role_b == Role::GiveWay ? b.give_way : b.stand_on).push_back(*cb);
        if (e.first.role_a != e.first.role_b) {
            const double gw = e.first.role_a == Role::GiveWay ? *ca : *cb;
            const double so = e.first.role_a == Role::GiveWay ? *cb : *ca;
            if (gw > so) ++b.give_way_more;
        }
        // Rule 15: in a crossing, the give-way vessel should not cross ahead.
        // At the real closest approach, is it abaft the stand-on vessel's beam?
        if (e.first.type == Encounter::Crossing) {
            const bool a_gives = e.first.role_a == Role::GiveWay;
            const auto& gw = a_gives ? ta : tb;
            const auto& so = a_gives ? tb : ta;
            const auto pg = position_at(gw, act->t_min);
            const auto ps = position_at(so, act->t_min);
            const auto so_now =
                std::lower_bound(so.begin(), so.end(), act->t_min, [](const Report& x, double v) { return x.t < v; });
            if (pg && ps && so_now != so.begin() && (so_now - 1)->cog) {
                const track::LocalFrame f(ps->first, ps->second);
                const auto rel = f.to_local(pg->first, pg->second);
                const double bearing = std::atan2(rel.east_m, rel.north_m) * 180.0 / std::numbers::pi;
                const double relb = std::fmod(bearing - *(so_now - 1)->cog + 720.0, 360.0);
                ++b.passed_astern_known;
                if (relb > 90.0 && relb < 270.0) ++b.passed_astern;
            }
        }
    }

    // ---- report ----
    std::printf("reports: %zu, vessels: %zu, assessed every %.0f s; at most %llu vessels under way at once\n",
                fixes.size(), vessels_seen.size(), kStep, static_cast<unsigned long long>(pairs_examined_max));
    std::printf("risk: CPA within %.2f nm within %.0f min, range under %.0f nm, both at %.0f kn or more%s\n",
                params.max_dcpa_m / risk::kNauticalMileM, params.max_tcpa_s / 60.0,
                params.max_range_m / risk::kNauticalMileM, params.min_speed_kn,
                commercial_only ? "; cargo, tanker, passenger and high-speed craft only" : "");
    std::printf(
        "pair-minutes at risk (Kalman predictor): %llu; scored against the recorded future: %llu; "
        "not scorable (reports missing): %llu\n",
        static_cast<unsigned long long>(at_risk_kf), static_cast<unsigned long long>(scored),
        static_cast<unsigned long long>(unscorable));

    std::printf(
        "\n| Time to CPA | Pair-minutes | CPA distance error, dead reckoning median / p90 (m) | "
        "CPA distance error, Kalman median / p90 (m) | CPA time error, Kalman median / p90 (s) |\n");
    std::printf("|---|---:|---:|---:|---:|\n");
    for (std::size_t b = 0; b + 1 < kHorizon.size(); ++b) {
        const auto& k = err_kf.at(b);
        const auto& d = err_dr.at(b);
        if (k.dcpa.empty()) continue;
        std::printf("| %.0f-%.0f min | %zu | %.0f / %.0f | %.0f / %.0f | %.0f / %.0f |\n", kHorizon.at(b) / 60.0,
                    kHorizon.at(b + 1) / 60.0, k.dcpa.size(), quantile(d.dcpa, 0.5), quantile(d.dcpa, 0.9),
                    quantile(k.dcpa, 0.5), quantile(k.dcpa, 0.9), quantile(k.tcpa, 0.5), quantile(k.tcpa, 0.9));
    }

    std::map<Encounter, std::uint64_t> count;
    for (const auto& e : encounters) ++count[e.first.type];
    const auto pct = [](std::uint64_t n, std::uint64_t d) {
        return d > 0 ? 100.0 * static_cast<double>(n) / static_cast<double>(d) : std::nan("");
    };
    const auto acted = [](const std::vector<double>& v) {
        return static_cast<std::uint64_t>(std::count_if(v.begin(), v.end(), [](double c) { return c >= kActed; }));
    };
    std::printf(
        "\nencounters: %zu (a pair's first minute at risk); developing (first at risk >= 1 nm apart, >= 4 min "
        "before CPA): %llu\n\n",
        encounters.size(), [&] {
            std::uint64_t n = 0;
            for (const auto& [k, v] : developing) n += v;
            return static_cast<unsigned long long>(n);
        }());
    std::printf(
        "Each vessel's contribution: how much its own manoeuvring opened (+) or closed (-) the passing distance, "
        "against holding course and speed.\n\n");
    std::printf(
        "| Encounter | All | Developing | Scored | Give-way: median contribution (m), opened >= 0.1 nm | "
        "Stand-on: median contribution (m), opened >= 0.1 nm | Give-way opened it more | Give-way passed astern |\n");
    std::printf("|---|---:|---:|---:|---:|---:|---:|---:|\n");
    for (const Encounter e : {Encounter::Crossing, Encounter::Overtaking, Encounter::HeadOn, Encounter::Unclear}) {
        const Behaviour& b = by_type[e];
        std::printf("| %.*s | %llu | %llu | %llu | %.0f, %llu of %zu (%.0f%%) |", static_cast<int>(name(e).size()),
                    name(e).data(), static_cast<unsigned long long>(count[e]),
                    static_cast<unsigned long long>(developing[e]), static_cast<unsigned long long>(b.n),
                    quantile(b.give_way, 0.5), static_cast<unsigned long long>(acted(b.give_way)), b.give_way.size(),
                    pct(acted(b.give_way), b.give_way.size()));
        if (b.stand_on.empty()) {
            std::printf(" - | - |");
        } else {
            std::printf(" %.0f, %llu of %zu (%.0f%%) | %llu of %llu (%.0f%%) |", quantile(b.stand_on, 0.5),
                        static_cast<unsigned long long>(acted(b.stand_on)), b.stand_on.size(),
                        pct(acted(b.stand_on), b.stand_on.size()), static_cast<unsigned long long>(b.give_way_more),
                        static_cast<unsigned long long>(b.n), pct(b.give_way_more, b.n));
        }
        if (e == Encounter::Crossing) {
            std::printf(" %llu of %llu (%.0f%%) |\n", static_cast<unsigned long long>(b.passed_astern),
                        static_cast<unsigned long long>(b.passed_astern_known),
                        pct(b.passed_astern, b.passed_astern_known));
        } else {
            std::printf(" - |\n");
        }
    }

    if (list) {
        std::printf("\nt0,mmsi_a,mmsi_b,type,role_a,role_b,range_m,tcpa_s,dcpa_m\n");
        for (const auto& e : encounters) {
            const auto& a = e.first;
            std::printf("%.0f,%u,%u,%.*s,%.*s,%.*s,%.0f,%.0f,%.0f\n", e.t0, e.a, e.b,
                        static_cast<int>(name(a.type).size()), name(a.type).data(),
                        static_cast<int>(name(a.role_a).size()), name(a.role_a).data(),
                        static_cast<int>(name(a.role_b).size()), name(a.role_b).data(), a.cpa.range_m, a.cpa.tcpa_s,
                        a.cpa.dcpa_m);
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run_main(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "mt-risk: " << e.what() << '\n';
        return 1;
    }
}

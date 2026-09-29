#include "maritime/serve/feed.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <unordered_set>

#include "maritime/track/geo.hpp"

namespace maritime::serve {

namespace {

void append(std::string& out, const char* fmt, auto... args) {
    char buf[160];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): snprintf target
    const int n = std::snprintf(buf, sizeof buf, fmt, args...);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (n > 0) out.append(buf, static_cast<std::size_t>(std::min<int>(n, sizeof buf - 1)));
}

void track_row(std::string& out, const track::TrackView& t) {
    const double sog = std::hypot(t.v_east, t.v_north) / track::kKnotToMs;
    double cog = std::atan2(t.v_east, t.v_north) * 180.0 / std::numbers::pi;
    if (cog < 0.0) cog += 360.0;
    append(out, "[%u,%.5f,%.5f,%.1f,%.0f,%.0f]", t.mmsi, t.lat_deg, t.lon_deg, sog, cog, t.t);
}

void anomaly_obj(std::string& out, const anomaly::Anomaly& a) {
    append(out, R"({"kind":"%.*s","mmsi":%u,"t":%.0f,"lat":%.5f,"lon":%.5f,"value":%.1f})",
           static_cast<int>(anomaly::name(a.kind).size()), anomaly::name(a.kind).data(), a.mmsi, a.t, a.lat_deg,
           a.lon_deg, a.value);
}

void encounter_obj(std::string& out, const risk::PairAssessment& p) {
    const auto& a = p.assessment;
    append(out, R"({"a":%u,"b":%u,"type":"%.*s","role_a":"%.*s","role_b":"%.*s","tcpa":%.0f,"dcpa":%.0f})", p.mmsi_a,
           p.mmsi_b, static_cast<int>(risk::name(a.type).size()), risk::name(a.type).data(),
           static_cast<int>(risk::name(a.role_a).size()), risk::name(a.role_a).data(),
           static_cast<int>(risk::name(a.role_b).size()), risk::name(a.role_b).data(), a.cpa.tcpa_s, a.cpa.dcpa_m);
}

template <class T, class F>
void list(std::string& out, const char* key, const T& items, F write) {
    out += ",\"";
    out += key;
    out += "\":[";
    bool first = true;
    for (const auto& x : items) {
        if (!first) out += ',';
        first = false;
        write(out, x);
    }
    out += ']';
}

void totals_obj(std::string& out, double data_time, std::size_t tracks, const FeedTotals& t) {
    append(out, R"(,"t":%.0f,"totals":{"messages":%llu,"vessels":%llu,"tracks":%zu,"anomalies":%llu,)", data_time,
           static_cast<unsigned long long>(t.messages), static_cast<unsigned long long>(t.vessels), tracks,
           static_cast<unsigned long long>(t.anomalies));
    append(out, R"("encounters":%llu,"dropped":%llu})", static_cast<unsigned long long>(t.encounters),
           static_cast<unsigned long long>(t.dropped));
}

}  // namespace

void LiveFeed::add_anomaly(const anomaly::Anomaly& a) {
    new_anomalies_.push_back(a);
    recent_.push_back(a);
    while (recent_.size() > kRecentAnomalies) recent_.pop_front();
}

void LiveFeed::set_encounters(std::vector<risk::PairAssessment> now) {
    encounters_ = std::move(now);
}

LiveFeed::Messages LiveFeed::tick(const track::Tracker& tracker, double data_time, const FeedTotals& totals) {
    auto tracks = tracker.tracks();
    std::sort(tracks.begin(), tracks.end(), [](const auto& a, const auto& b) { return a.mmsi < b.mmsi; });
    Messages m;

    // Update: tracks whose time moved since last sent, and tracks gone.
    m.update = R"({"type":"update")";
    std::vector<const track::TrackView*> changed;
    std::unordered_set<std::uint32_t> present;
    present.reserve(tracks.size());
    for (const auto& t : tracks) {
        present.insert(t.mmsi);
        const auto it = sent_.find(t.mmsi);
        if (it == sent_.end() || it->second != t.t) changed.push_back(&t);
    }
    std::vector<std::uint32_t> removed;
    for (auto it = sent_.begin(); it != sent_.end();) {
        if (!present.contains(it->first)) {
            removed.push_back(it->first);
            it = sent_.erase(it);
        } else {
            ++it;
        }
    }
    std::sort(removed.begin(), removed.end());
    for (const auto* t : changed) sent_[t->mmsi] = t->t;
    totals_obj(m.update, data_time, tracks.size(), totals);
    list(m.update, "tracks", changed, [](std::string& o, const track::TrackView* t) { track_row(o, *t); });
    list(m.update, "removed", removed, [](std::string& o, std::uint32_t mmsi) { append(o, "%u", mmsi); });
    list(m.update, "anomalies", new_anomalies_, anomaly_obj);
    list(m.update, "encounters", encounters_, encounter_obj);
    m.update += '}';
    new_anomalies_.clear();

    // Snapshot: everything.
    m.snapshot = R"({"type":"snapshot")";
    totals_obj(m.snapshot, data_time, tracks.size(), totals);
    list(m.snapshot, "tracks", tracks, track_row);
    list(m.snapshot, "anomalies", recent_, anomaly_obj);
    list(m.snapshot, "encounters", encounters_, encounter_obj);
    m.snapshot += '}';
    return m;
}

}  // namespace maritime::serve

#include "maritime/spatial/pairs.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

#include "maritime/track/geo.hpp"

namespace maritime::spatial {

namespace {

using Pairs = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

struct P3 {
    double x, y, z;
};

P3 to_ecef(const Point& p) {
    constexpr double kDeg = std::numbers::pi / 180.0;
    const double lat = p.lat_deg * kDeg;
    const double lon = p.lon_deg * kDeg;
    const double r = track::kEarthRadiusM;
    return {r * std::cos(lat) * std::cos(lon), r * std::cos(lat) * std::sin(lon), r * std::sin(lat)};
}

double dist2(const P3& a, const P3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// Straight-line distance through the Earth between two surface points a
// great-circle distance `radius_m` apart.
double chord(double radius_m) {
    const double r = track::kEarthRadiusM;
    return 2.0 * r * std::sin(std::min(radius_m / (2.0 * r), std::numbers::pi / 2.0));
}

std::pair<std::uint32_t, std::uint32_t> ordered(std::size_t i, std::size_t j) {
    const auto a = static_cast<std::uint32_t>(i);
    const auto b = static_cast<std::uint32_t>(j);
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

Pairs brute_force(const std::vector<P3>& p, double c2, PairStats& s) {
    Pairs out;
    for (std::size_t i = 0; i < p.size(); ++i) {
        for (std::size_t j = i + 1; j < p.size(); ++j) {
            ++s.distance_checks;
            if (dist2(p[i], p[j]) <= c2) out.push_back(ordered(i, j));
        }
    }
    return out;
}

Pairs latitude_sweep(const std::vector<Point>& pts, const std::vector<P3>& p, double radius_m, double c2,
                     PairStats& s) {
    std::vector<std::uint32_t> order(pts.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<std::uint32_t>(i);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return pts[a].lat_deg < pts[b].lat_deg; });
    // Two points further apart in latitude than this are further apart than
    // the radius. A hair wider, so rounding never drops a pair.
    const double window = radius_m / track::kEarthRadiusM * 180.0 / std::numbers::pi * (1.0 + 1e-9) + 1e-12;
    Pairs out;
    for (std::size_t a = 0; a < order.size(); ++a) {
        for (std::size_t b = a + 1; b < order.size() && pts[order[b]].lat_deg - pts[order[a]].lat_deg <= window; ++b) {
            ++s.distance_checks;
            if (dist2(p[order[a]], p[order[b]]) <= c2) out.push_back(ordered(order[a], order[b]));
        }
    }
    return out;
}

// Cubes of side `cell`, keyed by their integer coordinates packed into 63 bits.
Pairs grid(const std::vector<P3>& p, double c, PairStats& s) {
    constexpr std::int64_t kBias = 1 << 20;  // 21 bits per axis
    // Keep coordinates within 21 bits: cubes no smaller than 1/2^20 of the radius.
    const double cell = std::max(c, track::kEarthRadiusM / static_cast<double>(kBias - 2));
    const double c2 = c * c;
    const auto coord = [&](double v) { return static_cast<std::int64_t>(std::floor(v / cell)); };
    const auto pack = [](std::int64_t ix, std::int64_t iy, std::int64_t iz) {
        return (static_cast<std::uint64_t>(ix + kBias) << 42U) | (static_cast<std::uint64_t>(iy + kBias) << 21U) |
               static_cast<std::uint64_t>(iz + kBias);
    };

    struct Entry {
        std::uint64_t key;
        std::uint32_t idx;
    };
    std::vector<Entry> entries(p.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
        entries[i] = {pack(coord(p[i].x), coord(p[i].y), coord(p[i].z)), static_cast<std::uint32_t>(i)};
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });

    // Each occupied cube: where its points are in `entries`. (Copying the
    // points into cube order for memory locality was tried and made no
    // measurable difference; see docs/spatial-index-benchmark.md.)
    std::unordered_map<std::uint64_t, std::pair<std::size_t, std::size_t>> cells;
    cells.reserve(p.size());
    for (std::size_t b = 0; b < entries.size();) {
        std::size_t e = b;
        while (e < entries.size() && entries[e].key == entries[b].key) ++e;
        cells.emplace(entries[b].key, std::make_pair(b, e));
        b = e;
    }

    Pairs out;
    const auto check = [&](std::size_t a, std::size_t b) {
        ++s.distance_checks;
        if (dist2(p[entries[a].idx], p[entries[b].idx]) <= c2) out.push_back(ordered(entries[a].idx, entries[b].idx));
    };
    for (const auto& [key, range] : cells) {
        const auto ix = static_cast<std::int64_t>(key >> 42U) - kBias;
        const auto iy = static_cast<std::int64_t>((key >> 21U) & 0x1FFFFFU) - kBias;
        const auto iz = static_cast<std::int64_t>(key & 0x1FFFFFU) - kBias;
        // Within the cube.
        for (std::size_t a = range.first; a < range.second; ++a) {
            for (std::size_t b = a + 1; b < range.second; ++b) check(a, b);
        }
        // With each neighbouring cube, once: only neighbours with a larger key.
        for (std::int64_t dx = -1; dx <= 1; ++dx) {
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                for (std::int64_t dz = -1; dz <= 1; ++dz) {
                    const std::uint64_t nkey = pack(ix + dx, iy + dy, iz + dz);
                    if (nkey <= key) continue;
                    const auto it = cells.find(nkey);
                    if (it == cells.end()) continue;
                    for (std::size_t a = range.first; a < range.second; ++a) {
                        for (std::size_t b = it->second.first; b < it->second.second; ++b) check(a, b);
                    }
                }
            }
        }
    }
    return out;
}

// A static k-d tree over the 3D points, built once, queried once per point.
class KdTree {
public:
    explicit KdTree(const std::vector<P3>& p) : pts_(p), idx_(p.size()) {
        for (std::size_t i = 0; i < idx_.size(); ++i) idx_[i] = static_cast<std::uint32_t>(i);
        if (!idx_.empty()) root_ = build(0, idx_.size());
    }

    // Appends (i, j) for every j > i within sqrt(c2) of point i.
    void query(std::uint32_t i, double c, double c2, Pairs& out, PairStats& s) const {
        if (nodes_.empty()) return;
        const P3& q = pts_[i];
        std::array<std::size_t, 64> stack{};
        std::size_t top = 0;
        stack[top++] = root_;
        while (top > 0) {
            const Node& n = nodes_[stack[--top]];
            if (n.leaf) {
                for (std::size_t k = n.begin; k < n.end; ++k) {
                    const std::uint32_t j = idx_[k];
                    if (j <= i) continue;
                    ++s.distance_checks;
                    if (dist2(q, pts_[j]) <= c2) out.emplace_back(i, j);
                }
                continue;
            }
            const double d = axis(q, n.dim) - n.split;
            // Near side always; far side only if the sphere of radius c crosses the plane.
            const std::size_t near = d <= 0.0 ? n.left : n.right;
            const std::size_t far = d <= 0.0 ? n.right : n.left;
            if (std::abs(d) <= c) stack[top++] = far;
            stack[top++] = near;
        }
    }

private:
    static constexpr std::size_t kLeaf = 8;
    struct Node {
        bool leaf = false;
        int dim = 0;
        double split = 0.0;
        std::size_t begin = 0, end = 0;
        std::size_t left = 0, right = 0;
    };

    static double axis(const P3& p, int d) {
        switch (d) {
            case 0:
                return p.x;
            case 1:
                return p.y;
            default:
                return p.z;
        }
    }

    // Recursion depth is log2(n / kLeaf): 17 levels for a million points.
    std::size_t build(std::size_t b, std::size_t e) {  // NOLINT(misc-no-recursion)
        const std::size_t id = nodes_.size();
        nodes_.emplace_back();
        if (e - b <= kLeaf) {
            nodes_[id].leaf = true;
            nodes_[id].begin = b;
            nodes_[id].end = e;
            return id;
        }
        // Split the widest axis at the median.
        constexpr double kInf = std::numeric_limits<double>::infinity();
        std::array<double, 3> lo{kInf, kInf, kInf};
        std::array<double, 3> hi{-kInf, -kInf, -kInf};
        for (std::size_t k = b; k < e; ++k) {
            for (int d = 0; d < 3; ++d) {
                lo.at(static_cast<std::size_t>(d)) =
                    std::min(lo.at(static_cast<std::size_t>(d)), axis(pts_[idx_[k]], d));
                hi.at(static_cast<std::size_t>(d)) =
                    std::max(hi.at(static_cast<std::size_t>(d)), axis(pts_[idx_[k]], d));
            }
        }
        int dim = 0;
        for (int d = 1; d < 3; ++d) {
            if (hi.at(static_cast<std::size_t>(d)) - lo.at(static_cast<std::size_t>(d)) >
                hi.at(static_cast<std::size_t>(dim)) - lo.at(static_cast<std::size_t>(dim))) {
                dim = d;
            }
        }
        const std::size_t mid = b + (e - b) / 2;
        std::nth_element(idx_.begin() + static_cast<std::ptrdiff_t>(b), idx_.begin() + static_cast<std::ptrdiff_t>(mid),
                         idx_.begin() + static_cast<std::ptrdiff_t>(e),
                         [&](std::uint32_t x, std::uint32_t y) { return axis(pts_[x], dim) < axis(pts_[y], dim); });
        const double split = axis(pts_[idx_[mid]], dim);
        const std::size_t left = build(b, mid);
        const std::size_t right = build(mid, e);
        Node& n = nodes_[id];
        n.dim = dim;
        n.split = split;
        n.left = left;
        n.right = right;
        return id;
    }

    const std::vector<P3>& pts_;
    std::vector<std::uint32_t> idx_;
    std::vector<Node> nodes_;
    std::size_t root_ = 0;
};

// Sorts (i, j) pairs, i < n. A general sort costs O(P log P) on P pairs and
// was the largest single cost at scale (see docs/spatial-index-benchmark.md);
// bucketing by i is O(P + n), and each bucket (one point's neighbours) is small.
void sort_pairs(Pairs& pairs, std::size_t n) {
    std::vector<std::size_t> start(n + 1, 0);
    for (const auto& pr : pairs) ++start[pr.first + 1];
    for (std::size_t i = 0; i < n; ++i) start[i + 1] += start[i];
    Pairs sorted(pairs.size());
    std::vector<std::size_t> next(start.begin(), start.end() - 1);
    for (const auto& pr : pairs) sorted[next[pr.first]++] = pr;
    for (std::size_t i = 0; i < n; ++i) {
        const auto b = sorted.begin() + static_cast<std::ptrdiff_t>(start[i]);
        const auto e = sorted.begin() + static_cast<std::ptrdiff_t>(start[i + 1]);
        if (e - b > 1) std::sort(b, e);
    }
    pairs.swap(sorted);
}

}  // namespace

std::string_view name(Method m) noexcept {
    switch (m) {
        case Method::BruteForce:
            return "brute_force";
        case Method::LatitudeSweep:
            return "latitude_sweep";
        case Method::Grid:
            return "grid";
        case Method::KdTree:
            return "kd_tree";
    }
    return "unknown";
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs_within(const std::vector<Point>& points, double radius_m,
                                                                  Method method, PairStats* stats) {
    PairStats local;
    PairStats& s = stats != nullptr ? *stats : local;
    s = {};
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const auto t0 = Clock::now();
    std::vector<P3> p(points.size());
    std::transform(points.begin(), points.end(), p.begin(), to_ecef);
    const auto t1 = Clock::now();
    const double c = chord(radius_m);
    const double c2 = c * c;
    Pairs out;
    switch (method) {
        case Method::BruteForce:
            out = brute_force(p, c2, s);
            break;
        case Method::LatitudeSweep:
            out = latitude_sweep(points, p, radius_m, c2, s);
            break;
        case Method::Grid:
            out = grid(p, c, s);
            break;
        case Method::KdTree: {
            const KdTree tree(p);
            for (std::size_t i = 0; i < p.size(); ++i) tree.query(static_cast<std::uint32_t>(i), c, c2, out, s);
            break;
        }
    }
    const auto t2 = Clock::now();
    sort_pairs(out, points.size());
    const auto t3 = Clock::now();
    s.convert_ms = ms(t0, t1);
    s.search_ms = ms(t1, t2);
    s.sort_ms = ms(t2, t3);
    return out;
}

}  // namespace maritime::spatial

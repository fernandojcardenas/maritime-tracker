#include "maritime/track/geo.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace maritime::track {

namespace {
constexpr double kDegToRad = std::numbers::pi / 180.0;
}

double haversine_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
    const double p1 = lat1_deg * kDegToRad;
    const double p2 = lat2_deg * kDegToRad;
    const double dp = p2 - p1;
    const double dl = (lon2_deg - lon1_deg) * kDegToRad;
    const double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(a)));
}

LocalFrame::LocalFrame(double lat0_deg, double lon0_deg)
    : lat0_(lat0_deg),
      lon0_(lon0_deg),
      m_per_deg_lat_(kEarthRadiusM * kDegToRad),
      m_per_deg_lon_(kEarthRadiusM * kDegToRad * std::cos(lat0_deg * kDegToRad)) {}

EastNorth LocalFrame::to_local(double lat_deg, double lon_deg) const {
    double dlon = lon_deg - lon0_;
    if (dlon > 180.0) dlon -= 360.0;  // across the antimeridian
    if (dlon < -180.0) dlon += 360.0;
    return {dlon * m_per_deg_lon_, (lat_deg - lat0_) * m_per_deg_lat_};
}

void LocalFrame::to_geo(EastNorth p, double& lat_deg, double& lon_deg) const {
    lat_deg = lat0_ + p.north_m / m_per_deg_lat_;
    lon_deg = lon0_ + p.east_m / m_per_deg_lon_;
    if (lon_deg > 180.0) lon_deg -= 360.0;
    if (lon_deg < -180.0) lon_deg += 360.0;
}

EastNorth velocity_from_sog_cog(double sog_knots, double cog_deg) {
    const double v = sog_knots * kKnotToMs;
    const double c = cog_deg * kDegToRad;  // clockwise from true north
    return {v * std::sin(c), v * std::cos(c)};
}

}  // namespace maritime::track

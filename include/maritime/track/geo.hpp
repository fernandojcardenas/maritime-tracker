// Small geodesy helpers. Tracks are filtered in a local east/north plane in
// metres around an origin near the vessel; over the tens of kilometres a
// track covers before it is re-anchored, the equirectangular approximation
// is accurate to well under a metre per kilometre.
#pragma once

namespace maritime::track {

inline constexpr double kEarthRadiusM = 6371008.8;  // mean radius (IUGG)
inline constexpr double kKnotToMs = 1852.0 / 3600.0;

// Great-circle distance in metres.
[[nodiscard]] double haversine_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);

struct EastNorth {
    double east_m = 0.0;
    double north_m = 0.0;
};

class LocalFrame {
public:
    LocalFrame(double lat0_deg, double lon0_deg);

    [[nodiscard]] EastNorth to_local(double lat_deg, double lon_deg) const;
    void to_geo(EastNorth p, double& lat_deg, double& lon_deg) const;

    [[nodiscard]] double lat0() const noexcept { return lat0_; }
    [[nodiscard]] double lon0() const noexcept { return lon0_; }

private:
    double lat0_;
    double lon0_;
    double m_per_deg_lat_;
    double m_per_deg_lon_;
};

// Velocity in m/s (east, north) from speed over ground and course over ground.
[[nodiscard]] EastNorth velocity_from_sog_cog(double sog_knots, double cog_deg);

}  // namespace maritime::track

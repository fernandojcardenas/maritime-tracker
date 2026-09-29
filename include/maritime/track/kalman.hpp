// Constant-velocity Kalman filter in a local east/north plane.
//
// State x = [east, north, v_east, v_north] (m, m/s). Process noise is white
// acceleration with standard deviation sigma_a (m/s^2). Two measurement
// models are fused: position alone (2-D), or position plus the velocity
// derived from AIS speed and course over ground (4-D).
#pragma once

#include <array>
#include <optional>

namespace maritime::track {

using Vec4 = std::array<double, 4>;
using Mat4 = std::array<std::array<double, 4>, 4>;

struct KalmanParams {
    double sigma_a = 0.05;       // process noise, m/s^2 (tuned on real traffic, see docs/tracker-evaluation.md)
    double sigma_vel = 0.1;      // AIS SOG/COG velocity noise, m/s (tuned likewise)
    double initial_vel_sigma = 5.0;  // velocity uncertainty when a track starts without SOG/COG, m/s
};

struct Measurement {
    double east_m = 0.0;
    double north_m = 0.0;
    double pos_sigma_m = 15.0;
    std::optional<double> v_east;  // both or neither
    std::optional<double> v_north;
};

class KalmanCV {
public:
    KalmanCV(const Measurement& first, const KalmanParams& params);

    // Advances the state by dt seconds (dt >= 0).
    void predict(double dt);

    // Squared Mahalanobis distance of the position part of `m` from the
    // current prediction, used for gating before update().
    [[nodiscard]] double position_nis(const Measurement& m) const;

    void update(const Measurement& m);

    // Moves the state origin by (de, dn) metres, e.g. when a track is
    // re-anchored to a new local frame.
    void shift(double de, double dn);

    [[nodiscard]] const Vec4& state() const noexcept { return x_; }
    [[nodiscard]] const Mat4& covariance() const noexcept { return p_; }

private:
    KalmanParams params_;
    Vec4 x_{};
    Mat4 p_{};
};

}  // namespace maritime::track

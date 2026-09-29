#include "maritime/track/kalman.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace maritime::track {

namespace {

// Minimal fixed-size linear algebra; N <= 4 throughout.
template <std::size_t R, std::size_t C>
using Mat = std::array<std::array<double, C>, R>;

template <std::size_t R, std::size_t K, std::size_t C>
Mat<R, C> mul(const Mat<R, K>& a, const Mat<K, C>& b) {
    Mat<R, C> out{};
    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t k = 0; k < K; ++k)
            for (std::size_t j = 0; j < C; ++j) out[i][j] += a[i][k] * b[k][j];
    return out;
}

template <std::size_t R, std::size_t C>
Mat<C, R> transpose(const Mat<R, C>& a) {
    Mat<C, R> out{};
    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t j = 0; j < C; ++j) out[j][i] = a[i][j];
    return out;
}

// Inverse of a symmetric positive-definite matrix via Cholesky.
template <std::size_t N>
Mat<N, N> spd_inverse(const Mat<N, N>& a) {
    Mat<N, N> l{};
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double s = a[i][j];
            for (std::size_t k = 0; k < j; ++k) s -= l[i][k] * l[j][k];
            if (i == j) {
                if (s <= 0.0) throw std::runtime_error("covariance not positive definite");
                l[i][i] = std::sqrt(s);
            } else {
                l[i][j] = s / l[j][j];
            }
        }
    }
    // inv(L) by forward substitution, then inv(A) = inv(L)^T inv(L).
    Mat<N, N> li{};
    for (std::size_t i = 0; i < N; ++i) {
        li[i][i] = 1.0 / l[i][i];
        for (std::size_t j = 0; j < i; ++j) {
            double s = 0.0;
            for (std::size_t k = j; k < i; ++k) s -= l[i][k] * li[k][j];
            li[i][j] = s / l[i][i];
        }
    }
    return mul(transpose(li), li);
}

template <std::size_t M>
void update_impl(Vec4& x, Mat4& p, const std::array<double, M>& z, const Mat<M, M>& r, const Mat<M, 4>& h) {
    // Innovation y = z - H x and its covariance S = H P H^T + R.
    std::array<double, M> y{};
    for (std::size_t i = 0; i < M; ++i) {
        double hx = 0.0;
        for (std::size_t j = 0; j < 4; ++j) hx += h[i][j] * x[j];
        y[i] = z[i] - hx;
    }
    const auto pht = mul(p, transpose(h));
    auto s = mul(h, pht);
    for (std::size_t i = 0; i < M; ++i)
        for (std::size_t j = 0; j < M; ++j) s[i][j] += r[i][j];
    const auto k = mul(pht, spd_inverse(s));  // Kalman gain, 4 x M

    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < M; ++j) x[i] += k[i][j] * y[j];

    // Joseph form keeps P symmetric positive-definite despite rounding:
    // P = (I - K H) P (I - K H)^T + K R K^T
    Mat4 ikh{};
    const auto kh = mul(k, h);
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j) ikh[i][j] = (i == j ? 1.0 : 0.0) - kh[i][j];
    auto np = mul(mul(ikh, p), transpose(ikh));
    const auto krk = mul(mul(k, r), transpose(k));
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j) np[i][j] += krk[i][j];
    // Symmetrise.
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = i + 1; j < 4; ++j) np[i][j] = np[j][i] = 0.5 * (np[i][j] + np[j][i]);
    p = np;
}

bool has_velocity(const Measurement& m) { return m.v_east.has_value() && m.v_north.has_value(); }

}  // namespace

KalmanCV::KalmanCV(const Measurement& first, const KalmanParams& params) : params_(params) {
    const double pv = first.pos_sigma_m * first.pos_sigma_m;
    x_ = {first.east_m, first.north_m, 0.0, 0.0};
    double vv = params_.initial_vel_sigma * params_.initial_vel_sigma;
    if (has_velocity(first)) {
        x_[2] = first.v_east.value_or(0.0);
        x_[3] = first.v_north.value_or(0.0);
        vv = params_.sigma_vel * params_.sigma_vel;
    }
    p_ = {};
    p_[0][0] = p_[1][1] = pv;
    p_[2][2] = p_[3][3] = vv;
}

void KalmanCV::predict(double dt) {
    if (dt <= 0.0) return;
    // x = F x
    x_[0] += dt * x_[2];
    x_[1] += dt * x_[3];
    // P = F P F^T + Q
    const Mat4 f{{{1, 0, dt, 0}, {0, 1, 0, dt}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    auto np = mul(mul(f, p_), transpose(f));
    const double q = params_.sigma_a * params_.sigma_a;
    const double dt2 = dt * dt;
    const double dt3 = dt2 * dt;
    const double dt4 = dt3 * dt;
    for (std::size_t a = 0; a < 2; ++a) {
        np[a][a] += q * dt4 / 4.0;
        np[a][a + 2] += q * dt3 / 2.0;
        np[a + 2][a] += q * dt3 / 2.0;
        np[a + 2][a + 2] += q * dt2;
    }
    p_ = np;
}

double KalmanCV::position_nis(const Measurement& m) const {
    const double r = m.pos_sigma_m * m.pos_sigma_m;
    const Mat<2, 2> s{{{p_[0][0] + r, p_[0][1]}, {p_[1][0], p_[1][1] + r}}};
    const auto si = spd_inverse(s);
    const double y0 = m.east_m - x_[0];
    const double y1 = m.north_m - x_[1];
    return y0 * (si[0][0] * y0 + si[0][1] * y1) + y1 * (si[1][0] * y0 + si[1][1] * y1);
}

void KalmanCV::update(const Measurement& m) {
    const double r = m.pos_sigma_m * m.pos_sigma_m;
    if (has_velocity(m)) {
        const double rv = params_.sigma_vel * params_.sigma_vel;
        const Mat<4, 4> rr{{{r, 0, 0, 0}, {0, r, 0, 0}, {0, 0, rv, 0}, {0, 0, 0, rv}}};
        const Mat<4, 4> h{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
        update_impl<4>(x_, p_, {m.east_m, m.north_m, m.v_east.value_or(0.0), m.v_north.value_or(0.0)}, rr, h);
    } else {
        const Mat<2, 2> rr{{{r, 0}, {0, r}}};
        const Mat<2, 4> h{{{1, 0, 0, 0}, {0, 1, 0, 0}}};
        update_impl<2>(x_, p_, {m.east_m, m.north_m}, rr, h);
    }
}

void KalmanCV::shift(double de, double dn) {
    x_[0] -= de;
    x_[1] -= dn;
}

}  // namespace maritime::track

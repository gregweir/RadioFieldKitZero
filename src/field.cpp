#include "field.hpp"
#include <algorithm>
#include <cmath>

namespace rfk {
std::optional<double> frequency_mhz(std::string_view text) {
    if (text.empty() || text.size() > 9) return {};
    bool dot = false, digit = false;
    double value = 0.0, scale = 0.1;
    for (char c : text) {
        if (c == '.' && !dot) { dot = true; continue; }
        if (c < '0' || c > '9') return {};
        digit = true;
        if (dot) { value += (c - '0') * scale; scale *= 0.1; }
        else { value = value * 10 + c - '0'; }
    }
    // Field calculations are scoped to HF, not an authorization band plan.
    if (!digit || value < 1.8 - 1e-9 || value > 30.0 + 1e-9) return {};
    return std::clamp(value, 1.8, 30.0);
}

double starting_length_m(double mhz) { return speed_m_per_us * wire_factor / (4.0 * mhz); }

std::optional<Position> grid_center(std::string_view grid) {
    const auto n = grid.size();
    if (n != 4 && n != 6 && n != 8) return {};
    auto letter = [](char c) { return c >= 'a' && c <= 'z' ? c - 'a' : c >= 'A' && c <= 'Z' ? c - 'A' : -1; };
    auto digit = [](char c) { return c - '0'; };
    auto in = [](int v, int end) { return v >= 0 && v < end; };
    const int x = letter(grid[0]), y = letter(grid[1]), d = digit(grid[2]), e = digit(grid[3]);
    if (!in(x, 18) || !in(y, 18) || !in(d, 10) || !in(e, 10)) return {};
    double lon = -180 + x * 20 + d * 2, lat = -90 + y * 10 + e;
    double w = 2, h = 1;
    if (n >= 6) {
        const int a = letter(grid[4]), b = letter(grid[5]);
        if (!in(a, 24) || !in(b, 24)) return {};
        w /= 24; h /= 24; lon += a * w; lat += b * h;
    }
    if (n == 8) {
        const int a = digit(grid[6]), b = digit(grid[7]);
        if (!in(a, 10) || !in(b, 10)) return {};
        w /= 10; h /= 10; lon += a * w; lat += b * h;
    }
    return Position{lat + h / 2, lon + w / 2};
}

Path great_circle(Position from, Position to) {
    constexpr double rad = 3.14159265358979323846 / 180;
    const auto a = from.latitude * rad, b = to.latitude * rad;
    const auto change = (to.longitude - from.longitude) * rad;
    const auto sin_lat = std::sin((b - a) / 2), sin_lon = std::sin(change / 2);
    const auto h = std::clamp(sin_lat * sin_lat + std::cos(a) * std::cos(b) * sin_lon * sin_lon, 0., 1.);
    const auto angle = 2 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
    const auto km = 6371.0088 * angle;
    const auto east = std::sin(change) * std::cos(b);
    const auto north = std::cos(a) * std::sin(b) - std::sin(a) * std::cos(b) * std::cos(change);
    if (std::hypot(east, north) < 1e-12) return {km, {}};
    return {km, std::fmod(std::atan2(east, north) / rad + 360., 360.)};
}
double solar_elevation(Position at, const std::tm& utc) {
    // NOAA Global Monitoring Division's fractional-year approximation;
    // UTC input means the timezone offset in true solar time is zero.
    constexpr double rad = 3.14159265358979323846 / 180;
    const int year = utc.tm_year + 1900;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const double hour = utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0;
    const double gamma = 2 * 180 * rad / (leap ? 366 : 365) *
        (utc.tm_yday + (hour - 12) / 24);
    const double c1 = std::cos(gamma), s1 = std::sin(gamma);
    const double c2 = std::cos(2 * gamma), s2 = std::sin(2 * gamma);
    const double equation = 229.18 * (0.000075 + 0.001868*c1 - 0.032077*s1 - 0.014615*c2 - 0.040849*s2);
    const double declination = 0.006918 - 0.399912*c1 + 0.070257*s1 -
        0.006758*c2 + 0.000907*s2 - 0.002697*std::cos(3*gamma) + 0.00148*std::sin(3*gamma);
    const double phase = (15 * hour + equation / 4 + at.longitude - 180) * rad;
    const double latitude = at.latitude * rad;
    const double cosine = std::sin(latitude)*std::sin(declination) +
        std::cos(latitude)*std::cos(declination)*std::cos(phase);
    return std::asin(std::clamp(cosine, -1., 1.)) / rad;
}

}

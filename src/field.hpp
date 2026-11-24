#pragma once
// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include <ctime>
#include <optional>
#include <string_view>

namespace rfk {
constexpr double speed_m_per_us = 299.792458;
constexpr double wire_factor = 0.95;
constexpr double feet_per_m = 1.0 / 0.3048;

struct Position { double latitude, longitude; };
struct Path { double km; std::optional<double> bearing; };
std::optional<double> frequency_mhz(std::string_view text);
double starting_length_m(double mhz);
std::optional<Position> grid_center(std::string_view grid);
Path great_circle(Position from, Position to);
double solar_elevation(Position at, const std::tm& utc);
}

// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#pragma once
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace rfk {
enum class Index { flux, kp };
struct Reading {
    double value;
    std::time_t source_utc;
    bool downloaded = false;
};
struct Weather {
    std::optional<Reading> flux, kp;
};
std::optional<Reading> parse_swpc(Index type, std::string_view data, std::time_t now);
std::string weather_line(Index type, const std::optional<Reading>& reading, std::time_t now);
Weather load_weather();
Weather refresh_weather(Weather old);
}  // namespace rfk

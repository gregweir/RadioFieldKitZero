// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include "weather.hpp"

#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

namespace rfk {
namespace {
using json = nlohmann::json;
constexpr std::size_t max_response = 64 * 1024;

std::optional<std::time_t> parse_utc(std::string_view s) {
    // SWPC time_tag is UTC, even when the feed omits the trailing Z.
    if (s.size() == 20 && s.back() == 'Z') s.remove_suffix(1);
    if (s.size() != 19 || s[4] != '-' || s[7] != '-' ||
        s[10] != 'T' || s[13] != ':' || s[16] != ':') return {};
    auto number = [&](std::size_t start, std::size_t count) {
        int n = 0;
        for (std::size_t i = start; i < start + count; ++i) {
            if (s[i] < '0' || s[i] > '9') return -1;
            n = n * 10 + (s[i] - '0');
        }
        return n;
    };
    const int y = number(0,4), m = number(5,2), d = number(8,2);
    const int h = number(11,2), min = number(14,2), sec = number(17,2);
    if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31 ||
        h < 0 || h > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) return {};
    std::tm utc{};
    utc.tm_year = y - 1900; utc.tm_mon = m - 1; utc.tm_mday = d;
    utc.tm_hour = h; utc.tm_min = min; utc.tm_sec = sec;
    const auto t = timegm(&utc);
    if (t < 0 || utc.tm_year != y - 1900 || utc.tm_mon != m - 1 || utc.tm_mday != d) return {};
    return t;
}

const char* field(Index index) { return index == Index::flux ? "flux" : "Kp"; }

std::filesystem::path cache_path() {
    const char* base = std::getenv("XDG_CACHE_HOME");
    if (base && *base && std::filesystem::path(base).is_absolute())
        return std::filesystem::path(base) / "radiofieldkitzero" / "swpc.json";
    const char* home = std::getenv("HOME");
    if (home && *home && std::filesystem::path(home).is_absolute())
        return std::filesystem::path(home) / ".cache/radiofieldkitzero/swpc.json";
    return {};
}

std::string fetch(Index type) {
    // Fixed HTTPS GETs only. No location, user input, .curlrc, redirect or custom trust override.
#if RFK_DEVICE
#define RFK_CURL "/usr/bin/curl"
#else
#define RFK_CURL "curl"
#endif
#define RFK_REQ RFK_CURL " -q --silent --fail --proto '=https' --connect-timeout 2 --max-time 4 --max-filesize 65536 "
    const char* command = type == Index::flux ?
        RFK_REQ "https://services.swpc.noaa.gov/products/summary/10cm-flux.json" :
        RFK_REQ "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json";
#undef RFK_REQ
#undef RFK_CURL
    auto* pipe = popen(command, "r");
    if (!pipe) return {};
    std::string data;
    char chunk[4096];
    bool large = false;
    while (const auto n = std::fread(chunk, 1, sizeof(chunk), pipe)) {
        if (large || data.size() + n > max_response) { large = true; continue; }
        data.append(chunk, n);
    }
    return pclose(pipe) == 0 && !large ? data : std::string{};
}

void save_one(json& root, Index type, const std::optional<Reading>& reading) {
    if (!reading) return;
    const auto t = reading->source_utc;
    std::tm utc{};
    char tag[24];
    if (gmtime_r(&t, &utc) && std::strftime(tag, sizeof(tag), "%Y-%m-%dT%H:%M:%S", &utc))
        root[field(type)] = json::array({{{field(type), reading->value}, {"time_tag", tag}}});
}
void save_weather(const Weather& weather) {
    const auto path = cache_path();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return;
    json root = json::object();
    save_one(root, Index::flux, weather.flux);
    save_one(root, Index::kp, weather.kp);
    auto tmp = path;
    tmp += ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    out << root.dump();
    out.close();
    if (out) std::filesystem::rename(tmp, path, ec);
}
}

std::optional<Reading> parse_swpc(Index type, std::string_view data, std::time_t now) {
    if (data.empty() || data.size() > max_response) return {};
    const auto dates = json::parse(data, nullptr, false);
    if (!dates.is_array()) return {};
    std::optional<Reading> newest;
    for (const auto& row : dates) {
        if (!row.is_object()) continue;
        const auto value = row.find(field(type)), t = row.find("time_tag");
        if (value == row.end() || t == row.end() || !value->is_number() || !t->is_string()) continue;
        const double n = value->get<double>();
        if (!std::isfinite(n) || (type == Index::flux ? n < 40 || n > 1000 : n < 0 || n > 9)) continue;
        const auto stamp = parse_utc(t->get_ref<const std::string&>());
        if (stamp && *stamp <= now && (!newest || newest->source_utc < *stamp))
            newest = Reading{n, *stamp};
    }
    return newest;
}

std::string weather_line(Index type, const std::optional<Reading>& reading, std::time_t now) {
    const char* name = type == Index::flux ? "Solar flux" : "Kp (est.)";
    if (!reading) return std::string(name) + ": --";
    const auto age = std::difftime(now, reading->source_utc);
    const auto limit = type == Index::flux ? 36 * 3600 : 6 * 3600;
    const auto source = reading->source_utc;
    char time[32], result[128];
    std::tm utc{};
    if (!gmtime_r(&source, &utc) || !std::strftime(time, sizeof(time), "%d %b %H:%MZ", &utc))
        return std::string(name) + ": --";
    std::snprintf(result, sizeof(result), "%s %.*f  %s%s", name, type == Index::flux ? 0 : 2,
        reading->value, time, age < 0 || age > limit ? "  old" : reading->downloaded ? "" : "  saved");
    return result;
}

Weather load_weather() {
    try {
        const auto path = cache_path();
        if (path.empty()) return {};
        std::ifstream in(path);
        char data[1024];
        in.read(data, sizeof(data));
        if (!in.eof() || in.gcount() <= 0) return {};
        const auto root = json::parse(data, data + in.gcount(), nullptr, false);
        if (!root.is_object()) return {};
        const auto now = std::time(nullptr);
        Weather weather;
        for (const auto type : {Index::flux, Index::kp}) {
            const auto itr = root.find(field(type));
            if (itr != root.end()) (type == Index::flux ? weather.flux : weather.kp) =
                parse_swpc(type, itr->dump(), now);
        }
        return weather;
    } catch (const std::exception&) { return {}; }
}

Weather refresh_weather(Weather old) {
    bool changed = false;
    for (const auto type : {Index::flux, Index::kp}) {
        try {
            const auto raw = fetch(type);
            const auto sample = parse_swpc(type, raw, std::time(nullptr));
            auto& dest = type == Index::flux ? old.flux : old.kp;
            if (dest) dest->downloaded = false;
            if (sample && (!dest || sample->source_utc >= dest->source_utc)) {
                dest = sample;
                dest->downloaded = true;
                changed = true;
            }
        } catch (const std::exception&) { /* A failed request leaves that reading intact. */ }
    }
    if (changed) {
        try { save_weather(old); } catch (const std::exception&) {}
    }
    return old;
}
}

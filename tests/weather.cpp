// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include "weather.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>

using namespace rfk;

void check(bool passed, const char* why) {
    if (!passed) throw std::runtime_error(why);
}

int main() try {
    const auto now = std::time(nullptr);
    const std::string_view flux = R"([{"flux":88,"time_tag":"2026-11-24T20:00:00"}])";
    const std::string_view kp = R"([{"Kp":1.33,"time_tag":"2026-11-24T18:00:00"},
        {"Kp":10,"time_tag":"2026-11-24T21:00:00"},{"Kp":1,"time_tag":"2026-11-31T20:00:00"},
        {"Kp":0.3,"time_tag":"2026-11-24T16:00:00"}, {"Kp":9,"time_tag":"2026-11-25T00:00:00"}])";
    constexpr std::time_t sample_now = 1795561200; // 2026-11-24 23:00 UTC
    const auto solar = parse_swpc(Index::flux, flux, sample_now);
    const auto geomag = parse_swpc(Index::kp, kp, sample_now);
    check(solar && solar->value == 88 && solar->source_utc == sample_now - 3*3600, "flux UTC");
    check(geomag && geomag->value == 1.33 && geomag->source_utc == sample_now - 5*3600, "latest valid Kp");
    check(weather_line(Index::flux, solar, sample_now) == "Solar flux 88  24 Nov 20:00Z  saved", "saved");
    check(weather_line(Index::kp, geomag, sample_now + 2*3600) == "Kp (est.) 1.33  24 Nov 18:00Z  old", "old");
    check(!parse_swpc(Index::kp, "{broken", sample_now), "invalid JSON");
    check(!parse_swpc(Index::flux, R"([{"flux":"88","time_tag":"2026-11-24T20:00:00"}])", sample_now), "wrong type");

    char scratch[] = "/tmp/rfk-weather-test-XXXXXX";
    check(mkdtemp(scratch) != nullptr, "scratch");
    const std::filesystem::path dir = scratch;
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code ec; std::filesystem::remove_all(path, ec);
    }} cleanup{dir};
    setenv("XDG_CACHE_HOME", scratch, 1);
    setenv("PATH", scratch, 1);
    check(!load_weather().flux && !load_weather().kp, "empty cache");
    const auto stub = dir / "curl";
    auto response = [&](bool have_flux, bool have_kp) {
        const std::time_t t = now - 60;
        std::tm utc{};
        char tag[32];
        gmtime_r(&t, &utc);
        std::strftime(tag, sizeof(tag), "%Y-%m-%dT%H:%M:%S", &utc);
        std::ofstream out(stub);
        out << "#!/bin/sh\ncase \"$*\" in\n"
            << "  *10cm-flux*) " << (have_flux ? "printf '%s' '[{\"flux\":88,\"time_tag\":\"" + std::string(tag) + "\"}]'" : "exit 7") << " ;;\n"
            << "  *planetary-k-index*) " << (have_kp ? "printf '%s' '[{\"Kp\":1.33,\"time_tag\":\"" + std::string(tag) + "\"}]'" : "exit 7") << " ;;\n"
            << "  *) exit 42 ;;\nesac\n";
        out.close();
        check(out.good() && chmod(stub.c_str(), 0700) == 0, "stub init");
    };
    response(true, true);
    auto readings = refresh_weather({});
    check(readings.flux && readings.flux->downloaded && readings.kp && readings.kp->downloaded, "refresh");
    auto saved = load_weather();
    check(saved.flux && !saved.flux->downloaded && saved.kp && !saved.kp->downloaded, "cache reload");
    response(false, true);
    readings = refresh_weather(readings);
    check(readings.flux && !readings.flux->downloaded && readings.kp && readings.kp->downloaded, "partial refresh");
    response(false, false);
    readings = refresh_weather(readings);
    check(readings.flux && !readings.flux->downloaded && readings.kp && !readings.kp->downloaded, "offline fallback");
    saved = load_weather();
    check(saved.flux && saved.kp, "cache survives failed refresh");
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}

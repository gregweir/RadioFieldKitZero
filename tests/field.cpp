#include "field.hpp"
#include <cmath>
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* name) { if (!v) { std::cerr << name << '\n'; ++failures; } };
    auto close = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    check(rfk::frequency_mhz("14.200") && close(*rfk::frequency_mhz("14.200"),14.2), "MHz decimal");
    check(rfk::frequency_mhz("1.8") && rfk::frequency_mhz("30"), "frequency boundaries");
    for (auto s : {"", ".", "nan", "1.799999", "30.000001", "14..2", "1e1", "-14", "14.2 MHz"})
        check(!rfk::frequency_mhz(s), s);
    check(close(rfk::starting_length_m(14.2), 5.01413442), "dipole per leg");
    check(close(rfk::starting_length_m(14.2)*rfk::feet_per_m,16.45057225), "exact metric-foot conversion");
    const auto aa = rfk::grid_center("aA00");
    check(aa && close(aa->longitude,-179) && close(aa->latitude,-89.5), "grid center (not corner)");
    const auto rr = rfk::grid_center("RR99XX99");
    check(rr && close(rr->longitude,179.99583333) && close(rr->latitude,89.99791667), "8-digit grid");
    const auto jp = rfk::grid_center("PM95xq");
    check(jp && close(jp->longitude,139.95833333) && close(jp->latitude,35.6875), "6-digit grid");
    for (auto s : {"AA", "AA0", "AA0Q", "SA03", "AA03yq", "AA03qxQ9", "AA03x_", "NO GRID", "FN20AA11X"})
        check(!rfk::grid_center(s), s);
    const auto crossing = rfk::great_circle({0,179}, {0,-179});
    check(std::abs(crossing.km-222.39016)<0.01 && crossing.bearing && close(*crossing.bearing,90),
          "short date-line path and true bearing");
    check(!rfk::great_circle({42,-75}, {42,-75}).bearing, "undefined same bearing");
    check(!rfk::great_circle({0,0}, {0,180}).bearing, "undefined antipodal bearing");
    std::tm june{};
    june.tm_year = 126; june.tm_yday = 171; june.tm_hour = 12;
    check(rfk::solar_elevation({80,0},june) > 30, "Arctic summer day");
    check(rfk::solar_elevation({-80,0},june) < -12, "Antarctic winter night at noon");
    check(rfk::solar_elevation({0,0},june) > 65, "equatorial noon");
    check(rfk::solar_elevation({0,180},june) < -65, "antimeridian night at UTC noon");
    check(rfk::grid_from_position({44.6488, -63.566}) == "FN84FP", "Halifax grid");
    check(rfk::grid_from_position({48.1173, 11.522067}) == "JN58SC", "Munich grid");
    check(rfk::grid_from_position({0, 0}) == "JJ00AA", "zero grid");
    check(rfk::grid_from_position({0, 180}) == "AJ00AA", "antimeridian wraps");
    check(rfk::grid_from_position({90, 0}) == "JR09AX", "north pole clamps");
    // VHF/UHF gate for the J-pole and Slim Jim types mirrors frequency_mhz.
    check(rfk::vhf_mhz("145") && close(*rfk::vhf_mhz("145"), 145.0), "VHF centre");
    check(rfk::vhf_mhz("20") && rfk::vhf_mhz("470"), "VHF boundaries");
    for (auto s : {"", ".", "19.999", "470.0001", "145..2", "abc"})
        check(!rfk::vhf_mhz(s), s);
    return failures ? 1 : 0;
}

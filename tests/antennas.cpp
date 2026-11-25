// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include "antennas.hpp"
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>
#include <stdexcept>

using namespace rfk;

void check(bool passed, const char* why) {
    if (!passed) throw std::runtime_error(why);
}

bool close(double a, double b, double tol = 1e-4) { return std::fabs(a - b) < tol; }

int main() try {
    // Workshop vector (designs.test.ts: inverted-V at 14.2 MHz, factor 0.95, trim 0, 120 deg).
    const auto pin = inverted_v(14.2, 0.95, 0.0, 120.0);
    check(close(pin.leg_m, 5.0141), "workshop leg");
    check(close(pin.endpoint_span_m, 8.6847), "workshop span");
    check(close(pin.apex_drop_m, 2.5071), "workshop apex drop");

    // App defaults: same geometry with the 2.5 percent cut-long margin.
    const auto app = inverted_v(14.2);
    check(close(app.leg_m, 5.1395), "default leg");
    check(close(app.endpoint_span_m, 8.9019), "default span");
    check(close(app.apex_drop_m, 2.5697), "default drop");

    bool threw = false;
    try { (void)inverted_v(14.2, 0.95, 0.0, 20.0); } catch (const std::domain_error&) { threw = true; }
    check(threw, "angle below range throws");
    threw = false;
    try { (void)inverted_v(14.2, 1.3, 0.0, 120.0); } catch (const std::domain_error&) { threw = true; }
    check(threw, "factor above range throws");

    // Workshop vector (designs.test.ts: EFHW 7.1 MHz horizontal, 49:1, 2450 ohm).
    const auto ef = efhw(7.1);
    check(close(ef.radiator_m, 20.5580), "EFHW radiator");  // 20.557951 to 6dp
    check(close(ef.design_radiator_m, 20.0565), "EFHW design length");
    check(close(ef.transformed_ohms, 50.0, 1e-6), "EFHW transformed impedance");
    check(close(ef.turns_ratio, 7.0, 1e-9), "EFHW turns ratio");

    // Workshop vector: square loop 7.1 MHz, factor 1.02, trim 0.
    const auto sq_vec = square_loop(7.1, 1.02, 0.0);
    check(close(sq_vec.perimeter_m, 43.068776, 1e-5), "square loop workshop pin");
    // App defaults carry the 2.5 percent cut-long margin.
    const auto sq = square_loop(7.1);
    check(close(sq.perimeter_m, 44.1455), "square loop default perimeter");
    check(close(sq.primary_dimension_m, sq.perimeter_m / 4.0, 1e-9), "square side");

    // Delta defaults: apex-up, horizontal-side midpoint feed at half the perimeter.
    const auto dl = delta_loop(14.2);
    check(close(dl.perimeter_m, 22.0727), "delta perimeter");
    check(close(dl.leg_m, dl.perimeter_m / 3.0, 1e-9), "delta leg");
    check(close(dl.feed_from_apex_m, dl.perimeter_m / 2.0, 1e-9), "delta feed from apex");

    threw = false;
    try { (void)efhw(7.1, 3.0); } catch (const std::domain_error&) { threw = true; }
    check(threw, "EFHW ratio below range throws");

    // Workshop vector: ground-mounted vertical 7.1 MHz, 32 radials at x1.0.
    const auto v = vertical(7.1, 0.95, 2.5, 32, 1.0);
    check(close(v.radiator_m, 10.2790), "vertical radiator");
    check(close(v.radial_m, 10.0283), "vertical radial at resonant length");
    check(close(v.total_radial_wire_m, v.radial_m * 32.0, 1e-9), "vertical total radial wire");
    check(v.radial_count == 32, "vertical radial count");
    // App defaults carry the same 32 radial schedule.
    const auto vd = vertical(14.2);
    check(vd.radial_count == 32 && close(vd.radial_m, 5.0141), "vertical defaults");

    threw = false;
    try { (void)vertical(7.1, 0.95, 2.5, 3, 1.0); } catch (const std::domain_error&) { threw = true; }
    check(threw, "vertical radial count below range throws");
    threw = false;
    try { (void)vertical(7.1, 0.95, 2.5, 32, 2.5); } catch (const std::domain_error&) { threw = true; }
    check(threw, "vertical radial multiplier out of range throws");

    // Workshop vectors: J-pole and Slim Jim at 146 MHz (factor 0.83, trim 2.5).
    const auto jp = j_pole(146);
    check(close(jp.long_element_m, 1.3102), "J-pole long element");
    check(close(jp.matching_stub_m, jp.long_element_m / 3.0, 1e-9), "J-pole stub");
    check(close(jp.feed_from_short_m, jp.matching_stub_m * 0.075, 1e-9), "J-pole tap");
    const auto sj = slim_jim(146);
    check(close(sj.overall_height_m, 1.3102), "Slim Jim overall height");
    check(close(sj.gap_m, sj.matching_stub_m * 0.05, 1e-9), "Slim Jim gap");
    check(close(sj.upper_folded_m + sj.gap_m + sj.matching_stub_m, sj.overall_height_m, 1e-9),
          "Slim Jim coherent segments");

    threw = false;
    try { (void)j_pole(146, 30.0); } catch (const std::domain_error&) { threw = true; }
    check(threw, "J-pole tap out of range throws");
    threw = false;
    try { (void)slim_jim(146, 7.5, 20.0); } catch (const std::domain_error&) { threw = true; }
    check(threw, "Slim Jim gap out of range throws");

    // Pinned POTA/SOTA sets (descending frequency, inner segment first).
    {
        const auto ld = linked_dipole(LinkedSet::forty_twenty);
        check(ld.segment_cut_sides_m.size() == 2 && close(ld.segment_cut_sides_m[0], 5.1395, 1e-3) &&
              close(ld.segment_cut_sides_m[1], 4.9967, 1e-3), "40/20 segments");
        check(close(ld.cut_leg_m, 10.1362, 1e-3) && close(ld.finished_leg_m, 9.8890, 1e-3), "40/20 legs");
    }
    {
        const auto ld = linked_dipole(LinkedSet::forty_twenty_fifteen);
        check(ld.segment_cut_sides_m.size() == 3 && close(ld.segment_cut_sides_m[0], 3.4425, 1e-3) &&
              close(ld.segment_cut_sides_m[1], 1.6970, 1e-3) && close(ld.segment_cut_sides_m[2], 4.9967, 1e-3),
              "40/20/15 segments");
    }
    {
        const auto ld = linked_dipole(LinkedSet::forty_thirty_twenty);
        check(ld.segment_cut_sides_m.size() == 3 && close(ld.segment_cut_sides_m[0], 5.1395, 1e-3) &&
              close(ld.segment_cut_sides_m[1], 2.0720, 1e-3) && close(ld.segment_cut_sides_m[2], 2.9247, 1e-3),
              "40/30/20 segments");
    }
    {
        const auto ld = linked_dipole(LinkedSet::thirty_seventeen);
        check(ld.segment_cut_sides_m.size() == 2 && close(ld.segment_cut_sides_m[0], 4.0254, 1e-3) &&
              close(ld.segment_cut_sides_m[1], 3.1861, 1e-3), "30/17 segments");
    }
    check(std::string_view(linked_set_name(LinkedSet::forty_twenty)) == "Linked 40/20", "set name 1");
    check(std::string_view(linked_set_name(LinkedSet::forty_twenty_fifteen)) == "Linked 40/20/15", "set name 2");
    check(std::string_view(linked_set_name(LinkedSet::forty_thirty_twenty)) == "Linked 40/30/20", "set name 3");
    check(std::string_view(linked_set_name(LinkedSet::thirty_seventeen)) == "Linked 30/17", "set name 4");
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}

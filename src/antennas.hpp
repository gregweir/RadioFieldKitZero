// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#pragma once
#include <vector>
// Field antenna cut models ported one-for-one from the RadioBook Antenna
// Workshop calculators (read-only source of truth). Starting-point geometry
// only: never a guarantee of match, gain, or installed performance.

namespace rfk {

// Workshop commonLengths: resonant = wavelength x fraction x factor, and
// cut = resonant x (1 + trim%/100). Frequency in MHz.
struct Cut {
    double wavelength_m, resonant_m, cut_m;
};
Cut common_cut(double freq_mhz, double wavelength_fraction, double factor, double trim_percent);

struct InvertedV {
    double leg_m, endpoint_span_m, apex_drop_m;
};
// Defaults below: factor 0.95, trim 2.5 %, 120 deg included angle.
InvertedV inverted_v(double freq_mhz, double factor = 0.95, double trim_percent = 2.5,
                     double included_angle_degrees = 120.0);

// EFHW horizontal, half-wave radiator fed by the pinned transformer; 49:1
// across 2450 ohm of end impedance. Ratio 4-100, end impedance 200-20000.
struct Efhw {
    double radiator_m, design_radiator_m, transformed_ohms, turns_ratio;
};
Efhw efhw(double freq_mhz, double transformer_ratio = 49.0, double end_impedance_ohms = 2450.0);

// Full-wave loop, factor 1.02 (workshop default); square = side /4.
struct Loop {
    double perimeter_m, primary_dimension_m;
};
Loop square_loop(double freq_mhz, double factor = 1.02, double trim_percent = 2.5);

// Delta loop apex-up, fed at the midpoint of the horizontal side (half the
// perimeter from the apex; horizontal-polarization starting arrangement).
struct Delta {
    double perimeter_m, leg_m, feed_from_apex_m;
};
Delta delta_loop(double freq_mhz, double factor = 1.02, double trim_percent = 2.5);

// Ground-mounted quarter-wave vertical with its radial field. 32 radials at
// the resonant length (workshop radialLengthMultiplier 1.0); radial count
// 4-240, multiplier 0.25-2.
struct Vertical {
    double radiator_m, radial_m, total_radial_wire_m;
    int radial_count;
};
Vertical vertical(double freq_mhz, double factor = 0.95, double trim_percent = 2.5,
                  int radial_count = 32, double radial_length_multiplier = 1.0);

// VHF twin-lead J-pole (workshop factor 0.83); the matching stub is a quarter
// wave and the long element three; the feed taps `tap_percent` up the stub.
struct Jpole {
    double long_element_m, matching_stub_m, feed_from_short_m, resonant_stub_m;
};
Jpole j_pole(double freq_mhz, double tap_percent = 7.5);

// VHF Slim Jim: a folded doublet with the workshop gap arrangement; the gap
// and tap sit at `gap_percent` / `tap_percent` of the matching stub.
struct SlimJim {
    double overall_height_m, matching_stub_m, upper_folded_m, gap_m, feed_from_short_m;
};
SlimJim slim_jim(double freq_mhz, double tap_percent = 7.5, double gap_percent = 5.0);

// Linked multiband dipole over four pinned POTA/SOTA band sets (workshop
// model, factor 0.95, trim 2.5 %, 50 mm link gap, 130 deg angle). Segments
// run inner (highest design frequency) outward as per-side cut lengths.
enum class LinkedSet { forty_twenty, forty_twenty_fifteen, forty_thirty_twenty, thirty_seventeen };
struct LinkedDipole {
    double finished_leg_m, cut_leg_m;          // the full (lowest band) element
    std::vector<double> segment_cut_sides_m;
};
LinkedDipole linked_dipole(LinkedSet set);
const char* linked_set_name(LinkedSet set);  // "Linked 40/20", "Linked 40/20/15", ...

} // namespace rfk

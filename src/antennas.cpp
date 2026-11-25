// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include "antennas.hpp"
#include "field.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace rfk {
namespace {
// Workshop designs.ts commonLengths bounds.
void check_factor(double factor) {
    if (!(factor > 0.0) || factor > 1.2) throw std::domain_error("Length correction factor must be greater than zero and no more than 1.2.");
}
void check_trim(double trim_percent) {
    if (!(trim_percent >= 0.0) || trim_percent > 25.0) throw std::domain_error("Trim allowance must be between zero and 25 percent.");
}
} // namespace

Cut common_cut(double freq_mhz, double wavelength_fraction, double factor, double trim_percent) {
    if (!(freq_mhz > 0.0)) throw std::domain_error("Frequency must be greater than zero.");
    check_factor(factor);
    check_trim(trim_percent);
    const double wavelength_m = speed_m_per_us / freq_mhz;
    const double resonant_m = wavelength_m * wavelength_fraction * factor;
    return {wavelength_m, resonant_m, resonant_m * (1.0 + trim_percent / 100.0)};
}

InvertedV inverted_v(double freq_mhz, double factor, double trim_percent, double included_angle_degrees) {
    if (!(included_angle_degrees >= 60.0) || included_angle_degrees > 180.0)
        throw std::domain_error("Included angle must be between 60 and 180 degrees.");
    const Cut cut = common_cut(freq_mhz, 0.5, factor, trim_percent);
    const double leg_m = cut.cut_m / 2.0;
    const double half_angle = included_angle_degrees / 2.0 * 3.14159265358979323846 / 180.0;
    return {leg_m, 2.0 * leg_m * std::sin(half_angle), leg_m * std::cos(half_angle)};
}

Efhw efhw(double freq_mhz, double transformer_ratio, double end_impedance_ohms) {
    if (!(transformer_ratio >= 4.0) || transformer_ratio > 100.0)
        throw std::domain_error("Transformer impedance ratio must be between 4:1 and 100:1.");
    if (!(end_impedance_ohms >= 200.0) || end_impedance_ohms > 20000.0)
        throw std::domain_error("Estimated end impedance must be between 200 and 20,000 ohms.");
    const Cut cut = common_cut(freq_mhz, 0.5, 0.95, 2.5);
    return {cut.cut_m, cut.resonant_m, end_impedance_ohms / transformer_ratio, std::sqrt(transformer_ratio)};
}

Loop square_loop(double freq_mhz, double factor, double trim_percent) {
    const Cut cut = common_cut(freq_mhz, 1.0, factor, trim_percent);
    return {cut.cut_m, cut.cut_m / 4.0};
}

Delta delta_loop(double freq_mhz, double factor, double trim_percent) {
    // Workshop default arrangement: apex-up, fed at the midpoint of the
    // horizontal side (0.5 of the perimeter from the apex).
    const Cut cut = common_cut(freq_mhz, 1.0, factor, trim_percent);
    return {cut.cut_m, cut.cut_m / 3.0, cut.cut_m * 0.5};
}

Vertical vertical(double freq_mhz, double factor, double trim_percent, int radial_count,
                  double radial_length_multiplier) {
    if (radial_count < 4 || radial_count > 240)
        throw std::domain_error("Radial count must be a whole number between 4 and 240.");
    if (!(radial_length_multiplier >= 0.25) || radial_length_multiplier > 2.0)
        throw std::domain_error("Radial length multiplier must be between 0.25 and 2.");
    const Cut cut = common_cut(freq_mhz, 0.25, factor, trim_percent);
    const double radial_m = cut.resonant_m * radial_length_multiplier;
    return {cut.cut_m, radial_m, radial_m * radial_count, radial_count};
}

Jpole j_pole(double freq_mhz, double tap_percent) {
    if (!(tap_percent >= 1.0) || tap_percent > 25.0)
        throw std::domain_error("Feedpoint tap must be between one and 25 percent of the matching stub.");
    const Cut stub = common_cut(freq_mhz, 0.25, 0.83, 2.5);
    return {stub.cut_m * 3.0, stub.cut_m, stub.cut_m * tap_percent / 100.0, stub.resonant_m};
}

SlimJim slim_jim(double freq_mhz, double tap_percent, double gap_percent) {
    if (!(tap_percent >= 1.0) || tap_percent > 25.0)
        throw std::domain_error("Feedpoint tap must be between one and 25 percent of the matching stub.");
    if (!(gap_percent >= 1.0) || gap_percent > 15.0)
        throw std::domain_error("Interruption gap must be between one and 15 percent of the matching stub.");
    const Cut stub = common_cut(freq_mhz, 0.25, 0.83, 2.5);
    const double gap_m = stub.cut_m * gap_percent / 100.0;
    const double overall_m = stub.cut_m * 3.0;
    return {overall_m, stub.cut_m, overall_m - stub.cut_m - gap_m, gap_m,
            stub.cut_m * tap_percent / 100.0};
}

namespace {
// Design frequencies per pinned set; the segment schedule follows the
// workshop's linked-dipole construction (inner segment reaches the highest
// design frequency). Link gap 50 mm and a 130 deg included angle are pocket
// defaults from the workshop-linked-dipole input space.
const LinkedDipole linked_schedule(const double* freqs, std::size_t count) {
    // Per-side finished/cut length of the element reaching f MHz.
    auto legs = [](double f) {
        const Cut cut = common_cut(f, 0.5, 0.95, 2.5);
        return std::pair<double, double>{cut.resonant_m / 2.0, cut.cut_m / 2.0};
    };
    std::vector<std::pair<double, double>> bands;
    for (std::size_t i = 0; i < count; ++i) bands.push_back(legs(freqs[i]));
    LinkedDipole out;
    const auto lowest = bands.back();
    out.finished_leg_m = lowest.first;
    out.cut_leg_m = lowest.second;
    for (std::size_t i = 0; i < bands.size(); ++i) {
        const double inner = i == 0 ? 0.0 : bands[i - 1].second;
        out.segment_cut_sides_m.push_back(bands[i].second - inner);
    }
    return out;
}
} // namespace

LinkedDipole linked_dipole(LinkedSet set) {
    switch (set) {
    case LinkedSet::forty_twenty: {
        static constexpr double f[] = {14.2, 7.2};
        return linked_schedule(f, 2);
    }
    case LinkedSet::forty_twenty_fifteen: {
        static constexpr double f[] = {21.2, 14.2, 7.2};
        return linked_schedule(f, 3);
    }
    case LinkedSet::forty_thirty_twenty: {
        static constexpr double f[] = {14.2, 10.12, 7.2};
        return linked_schedule(f, 3);
    }
    case LinkedSet::thirty_seventeen: {
        static constexpr double f[] = {18.13, 10.12};
        return linked_schedule(f, 2);
    }
    }
    throw std::domain_error("Unsupported linked set.");
}

const char* linked_set_name(LinkedSet set) {
    switch (set) {
    case LinkedSet::forty_twenty: return "Linked 40/20";
    case LinkedSet::forty_twenty_fifteen: return "Linked 40/20/15";
    case LinkedSet::forty_thirty_twenty: return "Linked 40/30/20";
    case LinkedSet::thirty_seventeen: return "Linked 30/17";
    }
    return "Linked";
}

} // namespace rfk

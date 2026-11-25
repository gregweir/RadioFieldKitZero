// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include "antennas.hpp"
#include "field.hpp"
#include "gnss.hpp"
#include "weather.hpp"
#include "lvgl.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if RFK_DEVICE
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#else
#include <SDL.h>
#endif

namespace {
constexpr int kWidth = 320, kHeight = 170;
enum Key { key_none, key_quit, key_enter, key_tab, key_mode, key_units, key_back, key_refresh, key_gps };

// The A-key cycle on the antenna screen. Pinned POTA/SOTA linked sets are
// ordinary entries (A is the cycle key; a sub-cycle would never escape it).
enum AntennaType {
    ant_dipole, ant_inverted_v, ant_efhw, ant_loop, ant_delta, ant_vertical,
    ant_linked_40_20, ant_linked_40_20_15, ant_linked_40_30_20, ant_linked_30_17,
    ant_jpole, ant_slimjim, ant_count
};
const char* antenna_name(AntennaType t) {
    switch (t) {
    case ant_dipole: return "Dipole";
    case ant_inverted_v: return "Inverted-V";
    case ant_efhw: return "EFHW";
    case ant_loop: return "Loop";
    case ant_delta: return "Delta";
    case ant_vertical: return "Vertical";
    case ant_linked_40_20: return rfk::linked_set_name(rfk::LinkedSet::forty_twenty);
    case ant_linked_40_20_15: return rfk::linked_set_name(rfk::LinkedSet::forty_twenty_fifteen);
    case ant_linked_40_30_20: return rfk::linked_set_name(rfk::LinkedSet::forty_thirty_twenty);
    case ant_linked_30_17: return rfk::linked_set_name(rfk::LinkedSet::thirty_seventeen);
    case ant_jpole: return "J-pole";
    case ant_slimjim: return "Slim Jim";
    default: return "?";
    }
}
bool antenna_linked(AntennaType t) { return t >= ant_linked_40_20 && t <= ant_linked_30_17; }
bool antenna_vhf(AntennaType t) { return t == ant_jpole || t == ant_slimjim; }
rfk::LinkedSet antenna_set(AntennaType t) {
    return static_cast<rfk::LinkedSet>(t - ant_linked_40_20);
}

std::uint32_t ticks() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

class App {
public:
    explicit App(std::optional<std::time_t> fixed_time = {}) : fixed_time_(fixed_time), weather_(fixed_time ? rfk::Weather{} : rfk::load_weather()) {
        for (auto& f : antenna_freqs_) f = "14.200";
        antenna_freqs_[ant_jpole] = "145.000";
        antenna_freqs_[ant_slimjim] = "145.000";
        auto* root = lv_screen_active();
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(root, lv_color_hex(0x101D2B), 0);
        brand_ = label(7, 5, &lv_font_montserrat_14, 0x7BDAD2);
        lv_label_set_text(brand_, "Radio Field Kit Zero");
        page_ = label(7, 28, &lv_font_montserrat_20, 0xF6F9FD);
        input_ = label(7, 58, &lv_font_montserrat_14, 0xF2DF99);
        caret_ = lv_obj_create(root);
        lv_obj_remove_style_all(caret_);
        lv_obj_remove_flag(caret_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(caret_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(caret_, 3, 14);
        lv_obj_set_style_bg_color(caret_, lv_color_hex(0x7BDAD2), 0);
        lv_obj_set_style_bg_opa(caret_, LV_OPA_COVER, 0);
        result_ = label(7, 86, &lv_font_montserrat_20, 0xF6F9FD);
        hint_ = label(7, 113, &lv_font_montserrat_12, 0xBFD3E5);
        keys_ = label(7, 144, &lv_font_montserrat_12, 0x7BDAD2);
        render();
    }
    bool running = true;
    void tick(std::uint32_t now = ticks()) {
        if (refresh_.valid() && refresh_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try { weather_ = refresh_.get(); } catch (const std::exception&) {}
            if (mode_ == 2) render();
        }
        if (gps_.has_value() && gps_->running()) {
            const auto outcome = gps_->poll();
            if (outcome != rfk::GpsOutcome::running) {
                if (outcome == rfk::GpsOutcome::fix) {
                    from_ = rfk::grid_from_position(*gps_->position());
                    gps_last_ = outcome;
                } else gps_last_ = outcome;
                render();
            }
        }
        if (mode_ == 2 && now - last_clock_ >= 1000) render();
        if (mode_ == 0 && !antenna_linked(antenna_) && now - caret_epoch_ >= 500) {
            lv_obj_set_flag(caret_, LV_OBJ_FLAG_HIDDEN, !lv_obj_has_flag(caret_, LV_OBJ_FLAG_HIDDEN));
            caret_epoch_ = now;
        }
    }
    std::string result() const { return lv_label_get_text(result_); }
    std::string input_text() const { return lv_label_get_text(input_); }
    std::string hint_text() const { return lv_label_get_text(hint_); }
    std::string page_text() const { return lv_label_get_text(page_); }
    // Test hook: scripted hooks replace the real serial port and clock.
    void set_gps(rfk::GpsHooks hooks) {
        if (gps_ && gps_->running()) gps_->cancel();
        gps_hooks_ = std::move(hooks);
        gps_last_.reset();
        if (mode_ == 1) render();
    }
    void sample_weather_for_smoke(std::time_t now) {
        weather_.flux = rfk::Reading{108.0, now - 37 * 3600};
        weather_.kp = rfk::Reading{2.33, now - 3 * 3600};
    }
    int caret_x() const { return lv_obj_get_x(caret_); }
    int caret_y() const { return lv_obj_get_y(caret_); }
    bool caret_visible() const { return !lv_obj_has_flag(caret_, LV_OBJ_FLAG_HIDDEN); }
    std::uint32_t caret_epoch() const { return caret_epoch_; }

    bool labels_fit() {
        lv_obj_update_layout(lv_screen_active());
        const std::array<lv_obj_t*,6> labels{{brand_, page_, input_, result_, hint_, keys_}};
        for (std::size_t i = 0; i < labels.size(); ++i) {
            lv_area_t bounds{}; lv_obj_get_coords(labels[i], &bounds);
            const int lower = i + 1 < labels.size() ? lv_obj_get_y(labels[i + 1]) : kHeight;
            if (bounds.x1 < 0 || bounds.x2 >= kWidth || bounds.y1 < 0 || bounds.y2 >= lower) return false;
        }
        return true;
    }
    void key(Key k) {
        switch (k) {
        case key_quit: running = false; return;
        case key_tab:
            if (gps_ && gps_->running()) {  // leaving Maidenhead cancels a wait
                gps_->cancel();
                gps_last_ = rfk::GpsOutcome::cancelled;
            } else gps_last_.reset();
            mode_ = (mode_ + 1) % 3; second_ = false; replace_ = true; break;
        case key_enter:
            gps_last_.reset();
            if (mode_ == 1) { second_ = !second_; replace_ = true; }
            else replace_ = true;
            break;
        case key_refresh:
            if (mode_ != 2 || refresh_.valid()) return;
            try { refresh_ = std::async(std::launch::async, rfk::refresh_weather, weather_); }
            catch (const std::exception&) {}
            break;
        case key_mode:
            gps_last_.reset();
            if (mode_ == 0) {
                if (!antenna_linked(antenna_)) antenna_freqs_[antenna_] = freq_;
                antenna_ = static_cast<AntennaType>((antenna_ + 1) % ant_count);
                if (!antenna_linked(antenna_)) freq_ = antenna_freqs_[antenna_];
                replace_ = false;
            }
            break;
        case key_units: gps_last_.reset(); if (mode_ == 0) feet_ = !feet_; break;
        case key_back:
            gps_last_.reset();
            if (mode_ == 2) return;
            if (!field().empty()) field().pop_back();
            replace_ = false; break;
        case key_gps:
            if (mode_ != 1) return;
            if (gps_ && gps_->running()) {
                gps_->cancel();
                gps_last_ = rfk::GpsOutcome::cancelled;
            } else {
                gps_last_.reset();
                gps_.emplace(gps_hooks_);
                gps_->start();
                if (!gps_->running()) gps_last_ = gps_->state();  // busy or no port
            }
            break;
        default: return;
        }
        render();
    }
    void type(char ch) {
        if (mode_ == 2) return;
        gps_last_.reset();
        if (mode_ == 0 && antenna_linked(antenna_)) return;  // linked: the set is pinned

        if (mode_ == 1 && ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if ((ch < '0' || ch > '9') &&
            !(mode_ == 1 ? ch >= 'A' && ch <= 'Z' : ch == '.')) return;
        auto& target = field();
        if (replace_) { target.clear(); replace_ = false; }
        if (target.size() < (mode_ == 1 ? 8u : 9u)) target += ch;
        render();
    }
private:
    int mode_ = 0;
    std::uint32_t last_clock_ = 0, caret_epoch_ = 0;
    std::optional<std::time_t> fixed_time_;
    rfk::Weather weather_;
    std::future<rfk::Weather> refresh_;
    rfk::GpsHooks gps_hooks_;
    std::optional<rfk::GpsFetch> gps_;
    std::optional<rfk::GpsOutcome> gps_last_;  // terminal fetch outcome, shown until input
    bool second_ = false, feet_ = false, replace_ = true;
    AntennaType antenna_ = ant_dipole;
    std::array<std::string, ant_count> antenna_freqs_{};
    std::string freq_ = "14.200", from_, to_;
    lv_obj_t *brand_{}, *page_{}, *input_{}, *caret_{}, *result_{}, *hint_{}, *keys_{};

    static lv_obj_t* label(int x, int y, const lv_font_t* font, unsigned color) {
        auto* obj = lv_label_create(lv_screen_active());
        lv_obj_set_pos(obj, x, y);
        lv_obj_set_width(obj, kWidth - 2 * x);
        lv_obj_set_style_text_font(obj, font, 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
        return obj;
    }
    std::string& field() { return mode_ == 0 ? freq_ : second_ ? to_ : from_; }
    void render() {
        lv_obj_set_flag(caret_, LV_OBJ_FLAG_HIDDEN, mode_ != 0 || antenna_linked(antenna_));
        if (mode_ == 2) {
            last_clock_ = ticks();
            lv_label_set_text(page_, "HF conditions");
            const auto now = fixed_time_.value_or(std::time(nullptr));
            const auto* utc = std::gmtime(&now);
            if (utc && utc->tm_year >= 120) {
                const auto home = rfk::grid_center(from_);
                lv_label_set_text_fmt(input_, "%02d:%02d UTC   Grid: %s",
                    utc->tm_hour, utc->tm_min, home ? from_.c_str() : "-");
                if (home) {
                    const double elev = rfk::solar_elevation(*home, *utc);
                    lv_label_set_text_fmt(result_, "%s  (%d deg)", elev >= -0.833 ? "Daylight" :
                        elev >= -6 ? "Civil twilight" : "Night", static_cast<int>(std::lround(elev)));
                } else {
                    lv_label_set_text(result_, "Set your From grid");
                }
            } else {
                lv_label_set_text(input_, "UTC clock needs setting");
                lv_label_set_text(result_, "Check device time");

            }
            const auto flux = rfk::weather_line(rfk::Index::flux, weather_.flux, now);
            const auto kp = rfk::weather_line(rfk::Index::kp, weather_.kp, now);
            lv_label_set_text_fmt(hint_, "%s\n%s", flux.c_str(), kp.c_str());
            lv_label_set_text(keys_, refresh_.valid() ?
                "Checking NOAA...   Tab antenna   Esc quit" :
                "R NOAA refresh   Tab antenna   Esc quit");
            return;
        }
        if (mode_ == 1) {
            lv_label_set_text(page_, "Maidenhead path");
            lv_label_set_text_fmt(input_, "%sFrom: %s     %sTo: %s",
                second_ ? " " : ">", from_.empty() ? "-" : from_.c_str(),
                second_ ? ">" : " ", to_.empty() ? "-" : to_.c_str());
            const auto from = rfk::grid_center(from_), to = rfk::grid_center(to_);
            if (from && to) {
                const auto path = rfk::great_circle(*from, *to);
                const int distance = static_cast<int>(std::lround(path.km));
                if (path.bearing)
                    lv_label_set_text_fmt(result_, "%d km   %03d deg T",
                        distance, (static_cast<int>(*path.bearing + 0.5)) % 360);
                else lv_label_set_text_fmt(result_, "%d km   Bearing: -", distance);
                lv_label_set_text(hint_, "Short path between grid centres.");
            } else {
                lv_label_set_text(result_, "Enter two grids");
                lv_label_set_text(hint_, "Try a 4-, 6- or 8-character grid.");
            }
            const char* keys_line = "Enter next field   Tab HF   Esc quit";
            if (gps_ && gps_->running()) {
                lv_label_set_text(hint_, "Use GPS: waiting for a fix");
                keys_line = "Space cancel   Enter next field   Tab HF";
            } else if (gps_last_) {
                switch (*gps_last_) {
                case rfk::GpsOutcome::timeout:   lv_label_set_text(hint_, "GPS: no fix. Manual grid kept"); break;
                case rfk::GpsOutcome::cancelled: lv_label_set_text(hint_, "GPS: cancelled. Manual grid kept"); break;
                case rfk::GpsOutcome::busy:      lv_label_set_text(hint_, "GPS: port busy"); break;
                case rfk::GpsOutcome::missing:   lv_label_set_text(hint_, "GPS: no port. Cap on ExtPort power?"); break;
                case rfk::GpsOutcome::fix:       lv_label_set_text(hint_, "GPS grid filled"); break;
                default: break;
                }
            }
            lv_label_set_text(keys_, keys_line);
            return;
        }
        lv_label_set_text(page_, antenna_name(antenna_));
        constexpr std::string_view prefix = "Frequency: ";
        if (antenna_linked(antenna_)) {
            switch (antenna_) {
            case ant_linked_40_20: lv_label_set_text(input_, "Bands: 7.2 / 14.2 MHz"); break;
            case ant_linked_40_20_15: lv_label_set_text(input_, "Bands: 7.2 / 14.2 / 21.2 MHz"); break;
            case ant_linked_40_30_20: lv_label_set_text(input_, "Bands: 7.2 / 10.12 / 14.2 MHz"); break;
            default: lv_label_set_text(input_, "Bands: 10.12 / 18.13 MHz"); break;
            }
        } else {
            lv_label_set_text_fmt(input_, "%s%s  MHz", prefix.data(), freq_.c_str());
            // LVGL applies positions and text dimensions lazily. Measure only
            // after layout, including on the first frame of a fresh launch.
            lv_obj_update_layout(input_);
            lv_point_t at{};
            lv_label_get_letter_pos(input_, prefix.size() + freq_.size(), &at);
            lv_obj_set_pos(caret_, lv_obj_get_x(input_) + at.x, lv_obj_get_y(input_) + at.y + 2);
            caret_epoch_ = ticks();
        }
        // One screen value in cm/ft hundredths, shared by every type.
        auto length_text = [this](double metres) {
            const double v = metres * (feet_ ? rfk::feet_per_m : 1.0);
            const int hundredths = static_cast<int>(std::lround(v * 100.0));
            char buf[20];
            std::snprintf(buf, sizeof buf, "%d.%02d %s", hundredths / 100, hundredths % 100,
                          feet_ ? "ft" : "m");
            return std::string(buf);
        };
        const char* invalid_prompt = antenna_vhf(antenna_) ? "Enter 20 to 470 MHz" : "Enter 1.8 to 30 MHz";
        switch (antenna_) {
        case ant_dipole:
            if (const auto f = rfk::frequency_mhz(freq_)) {
                const auto value = rfk::starting_length_m(*f) * (feet_ ? rfk::feet_per_m : 1);
                const int hundredths = static_cast<int>(std::lround(value * 100));
                lv_label_set_text_fmt(result_, "%d.%02d %s per leg", hundredths / 100,
                                      hundredths % 100, feet_ ? "ft" : "m");
            } else {
                lv_label_set_text(result_, invalid_prompt);
            }
            lv_label_set_text(hint_, replace_ ?
                "Type a frequency to replace it.\nCut long, trim and check." :
                "Enter to replace. Backspace to edit.\nCut long, trim and check.");
            break;
        case ant_vertical:
            if (const auto f = rfk::frequency_mhz(freq_)) {
                const auto value = rfk::starting_length_m(*f) * (feet_ ? rfk::feet_per_m : 1);
                const int hundredths = static_cast<int>(std::lround(value * 100));
                lv_label_set_text_fmt(result_, "%d.%02d %s radiator", hundredths / 100,
                                      hundredths % 100, feet_ ? "ft" : "m");
                lv_label_set_text_fmt(hint_, "32 radials of %s. Cut long, trim.",
                                      length_text(rfk::vertical(*f).radial_m).c_str());
            } else {
                lv_label_set_text(result_, invalid_prompt);
                lv_label_set_text(hint_, "32 radials. Cut long, trim.");
            }
            break;
        case ant_inverted_v:
        case ant_efhw:
        case ant_loop:
        case ant_delta:
        case ant_jpole:
        case ant_slimjim: {
            const auto f = antenna_vhf(antenna_) ? rfk::vhf_mhz(freq_) : rfk::frequency_mhz(freq_);
            if (!f) {
                lv_label_set_text(result_, invalid_prompt);
                lv_label_set_text(hint_, "Cut long, trim.");
                break;
            }
            switch (antenna_) {
            case ant_inverted_v: {
                const auto iv = rfk::inverted_v(*f);
                lv_label_set_text_fmt(result_, "2 x %s legs", length_text(iv.leg_m).c_str());
                lv_label_set_text_fmt(hint_, "Span %s  Drop %s. Cut long, trim.",
                                      length_text(iv.endpoint_span_m).c_str(),
                                      length_text(iv.apex_drop_m).c_str());
                break;
            }
            case ant_efhw:
                lv_label_set_text_fmt(result_, "%s radiator",
                                      length_text(rfk::efhw(*f).radiator_m).c_str());
                lv_label_set_text(hint_, "49:1 transformer. Cut long, trim.");
                break;
            case ant_loop: {
                const auto sq = rfk::square_loop(*f);
                lv_label_set_text_fmt(result_, "%s sides", length_text(sq.primary_dimension_m).c_str());
                lv_label_set_text_fmt(hint_, "%s loop. Cut long, trim.",
                                      length_text(sq.perimeter_m).c_str());
                break;
            }
            case ant_delta: {
                const auto dl = rfk::delta_loop(*f);
                lv_label_set_text_fmt(result_, "%s legs", length_text(dl.leg_m).c_str());
                lv_label_set_text_fmt(hint_, "Mid-side feed %s from apex. Cut long, trim.",
                                      length_text(dl.feed_from_apex_m).c_str());
                break;
            }
            case ant_jpole: {
                const auto jp = rfk::j_pole(*f);
                lv_label_set_text_fmt(result_, "%s + %s stub",
                                      length_text(jp.long_element_m).c_str(),
                                      length_text(jp.matching_stub_m).c_str());
                lv_label_set_text_fmt(hint_, "Tap %s from short end. 20-470 MHz.",
                                      length_text(jp.feed_from_short_m).c_str());
                break;
            }
            default: {  // Slim Jim
                const auto sj = rfk::slim_jim(*f);
                lv_label_set_text_fmt(result_, "%s tall  gap %s",
                                      length_text(sj.overall_height_m).c_str(),
                                      length_text(sj.gap_m).c_str());
                lv_label_set_text_fmt(hint_, "Feed %s from short end. 20-470 MHz.",
                                      length_text(sj.feed_from_short_m).c_str());
                break;
            }
            }
            break;
        }
        default: {  // the four pinned linked sets; values are already cut long
            const auto ld = rfk::linked_dipole(antenna_set(antenna_));
            std::string sides;
            for (std::size_t i = 0; i < ld.segment_cut_sides_m.size(); ++i) {
                if (i) sides += " + ";
                const double v = ld.segment_cut_sides_m[i] * (feet_ ? rfk::feet_per_m : 1.0);
                const int hundredths = static_cast<int>(std::lround(v * 100.0));
                char num[16];
                std::snprintf(num, sizeof num, "%d.%02d", hundredths / 100, hundredths % 100);
                sides += num;
            }
            lv_label_set_text_fmt(result_, "%s %s sides", sides.c_str(), feet_ ? "ft" : "m");
            // Link states: opening a link leaves only the inner (short)
            // segment, resonant on the HIGHEST design band; every link
            // closed restores the full (lowest band) element.
            const char* link_guide = nullptr;
            switch (antenna_) {
            case ant_linked_40_20: link_guide = "Open: 14.2 MHz  Close: 7.2 MHz. Trim."; break;
            case ant_linked_40_20_15: link_guide = "Open: 21.2  Close 1: 14.2  All: 7.2. Trim."; break;
            case ant_linked_40_30_20: link_guide = "Open: 14.2  Close 1: 10.12  All: 7.2. Trim."; break;
            default: link_guide = "Open: 18.13  Close: 10.12. Trim."; break;
            }
            lv_label_set_text_fmt(hint_, "%s\nNever change links while transmitting.", link_guide);
            break;
        }
        }
        lv_label_set_text(keys_, "A antenna   U units   Tab grids   Esc quit");
    }
};

#if RFK_DEVICE
Key map_key(unsigned code) {
    switch (code) {
    case KEY_ESC: return key_quit;
    case KEY_ENTER: return key_enter;
    case KEY_TAB: return key_tab;
    case KEY_BACKSPACE: return key_back;
    default: return key_none;
    }
}

char keycode_char(unsigned code) {
    // The built-in TCA8418 keyboard reports Sym+B (point) as raw code 91.
    // Do not calculate ASCII from KEY_A: Linux evdev letters are not sequential.
    if (code == 91 || code == KEY_KPDOT) return '.';
    if (code == KEY_0) return '0';
    if (code >= KEY_1 && code <= KEY_9) return '1' + (code - KEY_1);
    constexpr std::array<unsigned,26> codes{{KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
        KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
        KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z}};
    for (std::size_t i = 0; i < codes.size(); ++i) if (code == codes[i]) return 'a' + i;
    return 0;
}
#endif

void input(App& app, Key key, char character = 0) {
    if (key != key_none) app.key(key);
    else if (character == 'a') {
        // When editing grids, "A" and "U" are data, not shortcuts.
        app.type('a');
        app.key(key_mode);
    } else if (character == 'u') {
        app.type('u');
        app.key(key_units);
    } else if (character == 'r') {
        app.type('r');
        app.key(key_refresh);
    } else if (character) app.type(character);
}

#if RFK_DEVICE
class DeviceKeyboard {
public:
    explicit DeviceKeyboard(const std::string& named) {
        if (!named.empty()) { open_candidate(named.c_str()); return; }
        const auto* dir_path = "/dev/input";
        auto* dir = opendir(dir_path);
        if (!dir) return;
        while (auto* entry = readdir(dir)) {
            if (std::strncmp(entry->d_name, "event", 5) != 0) continue;
            if (open_candidate((std::string(dir_path) + "/" + entry->d_name).c_str()))
                break;
        }
        closedir(dir);
    }
    ~DeviceKeyboard() { if (fd_ >= 0) close(fd_); }
    bool valid() const { return fd_ >= 0; }
    void dispatch(App& app) {
        input_event event{};
        while (::read(fd_, &event, sizeof(event)) == sizeof(event)) {
            if (event.type != EV_KEY || event.value != 1) continue;
            if (event.code == KEY_SPACE) { input(app, key_gps); continue; }
            const Key key = map_key(event.code);
            input(app, key, keycode_char(event.code));
        }
    }
private:
    int fd_ = -1;
    bool open_candidate(const char* path) {
        const auto fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return false;
        constexpr auto bits = sizeof(unsigned long) * 8;
        std::array<unsigned long, KEY_MAX / bits + 1> keys{};
        const auto has = [&](unsigned n){ return keys[n / bits] & (1UL << (n % bits)); };
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0 ||
            !has(KEY_K) || !has(KEY_M) || !has(KEY_ENTER) || !has(KEY_SPACE)) {
            close(fd); return false;
        }
        fd_ = fd;
        return true;
    }
};

#else
class Desktop {
public:
    explicit Desktop(bool hidden) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) fail();
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        window_ = SDL_CreateWindow("Radio Field Kit Zero",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            kWidth * 2, kHeight * 2, hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
        if (!window_) fail();
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer_) fail();
        SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight);
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565,
            SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
        if (!texture_) fail();
        display_ = lv_display_create(kWidth, kHeight);
        if (!display_) throw std::runtime_error("LVGL display creation failed");
        lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
        lv_display_set_user_data(display_, this);
        lv_display_set_buffers(display_, buffer_.data(), nullptr,
            sizeof(buffer_), LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display_,
            [](lv_display_t* display, const lv_area_t*, std::uint8_t* px){
                auto& ui = *static_cast<Desktop*>(lv_display_get_user_data(display));
                SDL_UpdateTexture(ui.texture_, nullptr, px, kWidth * 2);
                SDL_RenderClear(ui.renderer_);
                SDL_RenderCopy(ui.renderer_, ui.texture_, nullptr, nullptr);
                SDL_RenderPresent(ui.renderer_);
                lv_display_flush_ready(display);
            });
    }

    ~Desktop() {
        if (display_) lv_display_delete(display_);
        if (texture_) SDL_DestroyTexture(texture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    std::uint16_t pixel(int x, int y) const {
        return buffer_.at(y * kWidth + x);
    }

    void dispatch(App& app) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) app.key(key_quit);
            if (event.type != SDL_KEYDOWN || event.key.repeat) continue;
            const SDL_Keycode sym = event.key.keysym.sym;
            switch (sym) {
            case SDLK_ESCAPE: input(app, key_quit); break;
            case SDLK_RETURN: case SDLK_KP_ENTER: input(app, key_enter); break;
            case SDLK_TAB: input(app, key_tab); break;
            case SDLK_SPACE: input(app, key_gps); break;
            case SDLK_BACKSPACE: input(app, key_back); break;
            default: if (sym >= 32 && sym <= 126) input(app, key_none, static_cast<char>(sym));
            }
        }
    }

private:
    [[noreturn]] static void fail() { throw std::runtime_error(SDL_GetError()); }
    alignas(64) std::array<std::uint16_t, kWidth * kHeight> buffer_{};
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
    lv_display_t* display_{};
};

#endif
#if !RFK_DEVICE
void gps_smoke(App& app);

void smoke(App& app, const Desktop& desktop) {
    auto check = [&](std::string_view wanted) {
        lv_refr_now(nullptr);
        if (!app.labels_fit()) throw std::runtime_error("Screen labels overlap or clip");
        if (app.result() != wanted)
            throw std::runtime_error("Expected " + std::string(wanted) + ", got " + app.result());
    };
    auto press = [&](char c) { input(app, key_none, c); };
    auto keys = [&](std::string_view s) { for (char c : s) press(c); };
    check("5.01 m per leg");
    const auto blink_start = app.caret_epoch();
    if (!app.caret_visible()) throw std::runtime_error("Frequency caret is missing");
    const auto x = app.caret_x() + 1, y = app.caret_y() + 7;
    if (x < 110 || x > 162 || y < 60 || y > 76)
        throw std::runtime_error("Caret is not beside the starting frequency");
    const auto lit = desktop.pixel(x,y);
    app.tick(blink_start + 500);
    lv_refr_now(nullptr);
    if (desktop.pixel(x,y) == lit) throw std::runtime_error("Caret not painted in framebuffer");
    if (app.caret_visible()) throw std::runtime_error("Frequency caret did not blink off");
    app.tick(blink_start + 1000);
    lv_refr_now(nullptr);
    if (desktop.pixel(x,y) != lit) throw std::runtime_error("Caret did not repaint");
    if (!app.caret_visible()) throw std::runtime_error("Frequency caret did not blink on");
    keys("7.100"); check("10.03 m per leg"); // No Backspace or Enter on launch.
    input(app, key_back); check("10.03 m per leg");
    input(app, key_enter); keys("14.200"); check("5.01 m per leg");
    press('u'); check("16.45 ft per leg");
    for (int i = 0; i < 5; ++i) press('a');  // Dipole through to Vertical, feet on
    check("16.45 ft radiator");
    input(app, key_enter); keys("140"); check("Enter 1.8 to 30 MHz");
    input(app, key_enter); keys("14.200"); check("16.45 ft radiator");
    press('u'); check("5.01 m radiator");
    for (int i = 0; i < 7; ++i) press('a');  // wrap back to Dipole
    check("5.01 m per leg");
    // Full A-key cycle: every type at its default frequency, in metres.
    auto cycle = [&](const char* page_wanted, const char* result_wanted, const char* input_wanted,
                     const char* hint_wanted = nullptr) {
        press('a');
        lv_refr_now(nullptr);
        if (!app.labels_fit()) throw std::runtime_error("Cut-set screen overlapped or clipped");
        if (app.page_text() != std::string(page_wanted))
            throw std::runtime_error(std::string("Expected page ") + page_wanted + ", got " + app.page_text());
        if (app.result() != std::string(result_wanted))
            throw std::runtime_error(std::string("Expected ") + result_wanted + ", got " + app.result());
        if (input_wanted && app.input_text() != std::string(input_wanted))
            throw std::runtime_error(std::string("Expected input ") + input_wanted + ", got " + app.input_text());
        if (hint_wanted && app.hint_text() != std::string(hint_wanted))
            throw std::runtime_error(std::string("Expected hint ") + hint_wanted + ", got " + app.hint_text());
    };
    cycle("Inverted-V", "2 x 5.14 m legs", "Frequency: 14.200  MHz");
    cycle("EFHW", "10.28 m radiator", "Frequency: 14.200  MHz");
    cycle("Loop", "5.52 m sides", "Frequency: 14.200  MHz");
    cycle("Delta", "7.36 m legs", "Frequency: 14.200  MHz");
    cycle("Vertical", "5.01 m radiator", "Frequency: 14.200  MHz");
    input(app, key_enter); keys("7.100"); check("10.03 m radiator");  // left for the tail
    cycle("Linked 40/20", "5.14 + 5.00 m sides", "Bands: 7.2 / 14.2 MHz",
          "Open: 14.2 MHz  Close: 7.2 MHz. Trim.\nNever change links while transmitting.");
    press('7'); press('1'); press('0');  // typing is inert on a pinned set
    check("5.14 + 5.00 m sides");
    input(app, key_back);                // and so is editing
    check("5.14 + 5.00 m sides");
    cycle("Linked 40/20/15", "3.44 + 1.70 + 5.00 m sides", "Bands: 7.2 / 14.2 / 21.2 MHz",
          "Open: 21.2  Close 1: 14.2  All: 7.2. Trim.\nNever change links while transmitting.");
    press('u'); check("11.29 + 5.57 + 16.39 ft sides");  // the widest line fits in feet too
    press('u'); check("3.44 + 1.70 + 5.00 m sides");
    cycle("Linked 40/30/20", "5.14 + 2.07 + 2.92 m sides", "Bands: 7.2 / 10.12 / 14.2 MHz",
          "Open: 14.2  Close 1: 10.12  All: 7.2. Trim.\nNever change links while transmitting.");
    cycle("Linked 30/17", "4.03 + 3.19 m sides", "Bands: 10.12 / 18.13 MHz",
          "Open: 18.13  Close: 10.12. Trim.\nNever change links while transmitting.");
    cycle("J-pole", "1.32 m + 0.44 m stub", "Frequency: 145.000  MHz");
    press('u'); check("4.33 ft + 1.44 ft stub");
    press('u'); check("1.32 m + 0.44 m stub");
    input(app, key_enter); keys("480"); check("Enter 20 to 470 MHz");
    press('a');                          // Slim Jim
    for (int i = 0; i < 11; ++i) press('a');  // full wrap back to J-pole
    check("Enter 20 to 470 MHz");        // the invalid entry survives the round trip
    if (app.input_text() != std::string("Frequency: 480  MHz"))
        throw std::runtime_error(std::string("Expected input kept, got ") + app.input_text());
    input(app, key_enter); keys("145.000"); check("1.32 m + 0.44 m stub");
    cycle("Slim Jim", "1.32 m tall  gap 0.02 m", "Frequency: 145.000  MHz");
    cycle("Dipole", "5.01 m per leg", "Frequency: 14.200  MHz");  // per-type memory holds
    input(app, key_tab);
    if (app.caret_visible()) throw std::runtime_error("Frequency caret leaked onto the grid page");
    keys("aa00"); input(app, key_enter); keys("aa00");
    check("0 km   Bearing: -");
    app.sample_weather_for_smoke(1782043200);
    input(app, key_tab); check("Night  (-24 deg)");
    input(app, key_tab); check("5.01 m per leg");
    input(app, key_enter); keys("7.100"); check("10.03 m per leg");
    press('u'); check("32.90 ft per leg");
    for (int i = 0; i < 5; ++i) press('a');  // back to Vertical at the same frequency
    check("32.90 ft radiator");
    press('u'); check("10.03 m radiator");
    input(app, key_tab); keys("jj00aa"); input(app, key_enter); keys("ii48au");
    check("1341 km   264 deg T");

    gps_smoke(app);
    input(app, key_quit);
    if (app.running) throw std::runtime_error("Esc did not quit");
}

// One-shot scripted GNSS port plus a controllable fake clock, so the smoke
// drives the real Use GPS flow without any serial hardware.
struct HookFeed {
    mutable std::deque<std::string> chunks;
    ssize_t operator()(char* out, std::size_t size) const {
        if (chunks.empty()) return 0;
        auto& chunk = chunks.front();
        const ssize_t n = static_cast<ssize_t>(std::min(size, chunk.size()));
        std::memcpy(out, chunk.data(), static_cast<std::size_t>(n));
        if (static_cast<std::size_t>(n) == chunk.size()) chunks.pop_front();
        else chunk.erase(0, static_cast<std::size_t>(n));
        return n;
    }
};

std::string nmea_line(const char* body) {
    std::uint8_t x = 0;
    for (const char* c = body; *c; ++c) x = static_cast<std::uint8_t>(x ^ *c);
    char cs[4];
    std::snprintf(cs, sizeof cs, "*%02X", x);
    return "$" + std::string(body) + cs + "\r\n";
}

void gps_smoke(App& app) {
    auto gpss = [&](const char* input_wanted, const char* hint_wanted, const char* result_wanted) {
        lv_refr_now(nullptr);
        if (!app.labels_fit()) throw std::runtime_error("GPS state overlapped or clipped");
        if (app.input_text() != input_wanted)
            throw std::runtime_error(std::string("Expected input ") + input_wanted + ", got " + app.input_text());
        if (app.hint_text() != hint_wanted)
            throw std::runtime_error(std::string("Expected hint ") + hint_wanted + ", got " + app.hint_text());
        if (result_wanted && app.result() != result_wanted)
            throw std::runtime_error(std::string("Expected result ") + result_wanted + ", got " + app.result());
    };
    std::int64_t fake_ms = 1000;
    const char* gga = "GPGGA,123519,4807.038,N,01131.324,E,1,08,0.9,545.4,M,46.9,M,,";
    auto set_fix = [&] {
        rfk::GpsHooks h;
        h.mono = [&] { return fake_ms; };
        h.open = [gga] {
            auto feed = std::make_shared<HookFeed>();
            feed->chunks.emplace_back(nmea_line(gga));
            return rfk::PortOpen{rfk::ReadFn([feed](char* out, std::size_t size) {
                return (*feed)(out, size);
            }), false};
        };
        app.set_gps(h);
    };
    auto set_silent = [&] {
        rfk::GpsHooks h;
        h.mono = [&] { return fake_ms; };
        h.open = [] {
            auto feed = std::make_shared<HookFeed>();
            return rfk::PortOpen{rfk::ReadFn([feed](char* out, std::size_t size) {
                return (*feed)(out, size);
            }), false};
        };
        app.set_gps(h);
    };
    auto set_busy = [&] {
        rfk::GpsHooks h;
        h.mono = [&] { return fake_ms; };
        h.open = [] { return rfk::PortOpen{rfk::ReadFn(), true}; };
        app.set_gps(h);
    };

    // Baseline: clear To and return to editing From so every expected string
    // below is deterministic regardless of the earlier smoke steps.
    for (int i = 0; i < 6; ++i) input(app, key_back);
    input(app, key_enter);  // To: - -> editing From

    // 1. A fresh, valid fix fills From only.
    set_fix();
    input(app, key_gps);
    app.tick();
    app.tick();
    gpss(">From: JN58SC      To: -", "GPS grid filled", "Enter two grids");

    // 2. A fix while To is being edited leaves the typed To alone.
    input(app, key_enter);
    for (char c : std::string_view("jj05")) input(app, key_none, c);
    set_fix();
    input(app, key_gps);
    app.tick();
    app.tick();
    gpss(" From: JN58SC     >To: JJ05", "GPS grid filled", "4842 km   195 deg T");

    // 3. A busy port changes nothing.
    set_busy();
    input(app, key_gps);
    app.tick();
    gpss(" From: JN58SC     >To: JJ05", "GPS: port busy", "4842 km   195 deg T");

    // 4. Dead air ends in no-fix, and Space cancels a later wait.
    set_silent();
    input(app, key_gps);
    app.tick();
    gpss(" From: JN58SC     >To: JJ05", "Use GPS: waiting for a fix", "4842 km   195 deg T");
    fake_ms += 30001;
    app.tick();
    gpss(" From: JN58SC     >To: JJ05", "GPS: no fix. Manual grid kept", "4842 km   195 deg T");
    input(app, key_gps);
    app.tick();
    gpss(" From: JN58SC     >To: JJ05", "Use GPS: waiting for a fix", "4842 km   195 deg T");
    input(app, key_gps);
    gpss(" From: JN58SC     >To: JJ05", "GPS: cancelled. Manual grid kept", "4842 km   195 deg T");

    // 5. Mid-typed text in From is replaced wholesale by the fix.
    input(app, key_enter);
    for (char c : std::string_view("jj00aa")) input(app, key_none, c);
    set_fix();
    input(app, key_gps);
    app.tick();
    app.tick();
    gpss(">From: JN58SC      To: JJ05", "GPS grid filled", "4842 km   195 deg T");

    // 6. Space means nothing on the HF and antenna screens.
    input(app, key_tab);
    const std::string conditions = app.result();
    input(app, key_gps);
    app.tick();
    if (app.result() != conditions)
        throw std::runtime_error(std::string("Space disturbed HF conditions: got ") + app.result());
    input(app, key_tab);
    if (app.result() != std::string("10.03 m radiator"))
        throw std::runtime_error(std::string("Expected result 10.03 m radiator, got ") + app.result());
    input(app, key_gps);
    app.tick();
    if (app.result() != std::string("10.03 m radiator"))
        throw std::runtime_error(std::string("Space disturbed the antenna result: got ") + app.result());
    input(app, key_tab);
    gpss(">From: JN58SC      To: JJ05", "Short path between grid centres.", nullptr);
}
#endif
}

int main(int argc, char** argv) try {
    lv_init();
    lv_tick_set_cb(ticks);
#if RFK_DEVICE
    if (argc != 1 && !(argc == 3 && std::string_view(argv[1]) == "--keyboard")) return 2;
    DeviceKeyboard keyboard(argc == 3 ? argv[2] : "");
    if (!keyboard.valid()) { std::cerr << "No readable Cardputer Zero keyboard.\n"; return 1; }
    auto* display = lv_linux_fbdev_create();
    if (!display || lv_linux_fbdev_set_file(display, "/dev/fb0") != LV_RESULT_OK ||
        lv_display_get_horizontal_resolution(display) != kWidth ||
        lv_display_get_vertical_resolution(display) != kHeight) {
        std::cerr << "Expected the Cardputer Zero 320 x 170 framebuffer.\n";
        return 1;
    }
    App app;
    while (app.running) {
        keyboard.dispatch(app); app.tick(); lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
    }
    lv_display_delete(display);
#else
    const bool testing = argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (argc != 1 && !testing) return 2;
    {
        Desktop desktop(testing);
        App app(testing ? std::optional<std::time_t>(1782043200) : std::nullopt);
        if (testing) smoke(app, desktop);
        while (app.running) {
            desktop.dispatch(app); app.tick(); lv_timer_handler();
            std::this_thread::sleep_for(std::chrono::milliseconds(6));
        }
    }
#endif
    lv_deinit();
    return 0;
} catch (const std::exception& e) {
    std::cerr << "Radio Field Kit Zero: " << e.what() << '\n';
    return 1;
}

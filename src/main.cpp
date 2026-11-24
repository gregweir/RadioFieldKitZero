// Developed for Tartanleaf.com by Greg Weir VA1GW. SPDX-License-Identifier: MIT
#include "field.hpp"
#include "lvgl.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
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
enum Key { key_none, key_quit, key_enter, key_tab, key_mode, key_units, key_back };

std::uint32_t ticks() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

class App {
public:
    explicit App(std::optional<std::time_t> fixed_time = {}) : fixed_time_(fixed_time) {
        auto* root = lv_screen_active();
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(root, lv_color_hex(0x101D2B), 0);
        brand_ = label(7, 5, &lv_font_montserrat_14, 0x7BDAD2);
        lv_label_set_text(brand_, "Radio Field Kit Zero");
        page_ = label(7, 28, &lv_font_montserrat_20, 0xF6F9FD);
        input_ = label(7, 58, &lv_font_montserrat_14, 0xF2DF99);
        result_ = label(7, 86, &lv_font_montserrat_20, 0xF6F9FD);
        hint_ = label(7, 113, &lv_font_montserrat_12, 0xBFD3E5);
        keys_ = label(7, 144, &lv_font_montserrat_12, 0x7BDAD2);
        render();
    }
    bool running = true;
    void tick() {
        if (mode_ == 2 && static_cast<uint32_t>(ticks() - last_clock_) >= 1000) render();
    }
    std::string result() const { return lv_label_get_text(result_); }

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
        case key_tab: mode_ = (mode_ + 1) % 3; second_ = false; replace_ = true; break;
        case key_enter:
            if (mode_ == 1) { second_ = !second_; replace_ = true; }
            else replace_ = true;
            break;
        case key_mode: if (mode_ == 0) vertical_ = !vertical_; break;
        case key_units: if (mode_ == 0) feet_ = !feet_; break;
        case key_back: if (mode_ == 2) return;
            if (!field().empty()) field().pop_back();
            replace_ = false; break;
        default: return;
        }
        render();
    }
    void type(char ch) {
        if (mode_ == 2) return;
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
    uint32_t last_clock_ = 0;
    std::optional<std::time_t> fixed_time_;
    bool second_ = false, feet_ = false, vertical_ = false, replace_ = true;
    std::string freq_ = "14.200", from_, to_;
    lv_obj_t *brand_{}, *page_{}, *input_{}, *result_{}, *hint_{}, *keys_{};

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
                    lv_label_set_text(result_, elev >= -0.833 ? "Daylight" :
                        elev >= -6 ? "Civil twilight" : "Night");
                    lv_label_set_text_fmt(hint_, "Sun %d deg at grid centre. Not a forecast.",
                        static_cast<int>(std::lround(elev)));
                } else {
                    lv_label_set_text(result_, "Set your From grid");
                    lv_label_set_text(hint_, "Grid daylight works without an internet link.");
                }
            } else {
                lv_label_set_text(input_, "UTC clock needs setting");
                lv_label_set_text(result_, "Check device time");
                lv_label_set_text(hint_, "Daylight uses the device clock.");
            }
            lv_label_set_text(keys_, "Tab antenna   Esc quit");
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
            lv_label_set_text(keys_, "Enter next field   Tab HF   Esc quit");
            return;
        }
        lv_label_set_text(page_, vertical_ ? "Quarter-wave vertical" : "Dipole");
        lv_label_set_text_fmt(input_, "Frequency: %s MHz", freq_.c_str());
        if (const auto f = rfk::frequency_mhz(freq_)) {
            const auto length = rfk::starting_length_m(*f) * (feet_ ? rfk::feet_per_m : 1);
            const int hundredths = static_cast<int>(std::lround(length * 100));
            lv_label_set_text_fmt(result_, "%d.%02d %s %s", hundredths / 100,
                                  hundredths % 100, feet_ ? "ft" : "m",
                                  vertical_ ? "radiator" : "per leg");
        } else {
            lv_label_set_text(result_, "Enter 1.8 to 30 MHz");
        }
        lv_label_set_text(hint_, "Starting length. Cut long, trim and check.");
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
void smoke(App& app) {
    auto check = [&](std::string_view wanted) {
        lv_refr_now(nullptr);
        if (!app.labels_fit()) throw std::runtime_error("Screen labels overlap or clip");
        if (app.result() != wanted)
            throw std::runtime_error("Expected " + std::string(wanted) + ", got " + app.result());
    };
    auto press = [&](char c) { input(app, key_none, c); };
    auto keys = [&](std::string_view s) { for (char c : s) press(c); };
    check("5.01 m per leg");
    press('u'); check("16.45 ft per leg");
    press('a'); check("16.45 ft radiator");
    keys("140"); check("Enter 1.8 to 30 MHz");
    input(app, key_enter); keys("14.200"); check("16.45 ft radiator");
    input(app, key_tab); keys("aa00"); input(app, key_enter); keys("aa00");
    check("0 km   Bearing: -");
    input(app, key_tab); check("Night");
    input(app, key_tab); check("16.45 ft radiator");
    input(app, key_quit);
    if (app.running) throw std::runtime_error("Esc did not quit");
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
        if (testing) smoke(app);
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

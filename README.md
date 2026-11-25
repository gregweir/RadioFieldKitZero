# Radio Field Kit Zero

Pocket tools for portable amateur radio on the Linux ARM64 M5Stack Cardputer Zero,
not the ESP32 Cardputer or Cardputer ADV. Antenna lengths, manual Maidenhead paths
and UTC/daylight work offline and without a cap.

## Use the field tools

- On the antenna screen, type a frequency. **A** cycles the antenna types, including
  dipole, inverted-V, EFHW, loop, delta, vertical, linked dipoles, J-pole and Slim Jim;
  **U** switches metres/feet. HF types accept 1.8–30 MHz, and the J-pole and Slim Jim
  models accept 20–470 MHz. Linked dipoles show cut lengths for preset band combinations.
- The dipole result is **per leg**. All results are starting points: construction and
  surroundings can change the length needed. Cut long, trim and check before use.
- **Tab** opens the Maidenhead calculator. Enter a 4-, 6- or 8-character grid; **Enter**
  moves to the other grid and **Backspace** edits. The short path is measured between
  grid centres and shows an initial **true** bearing, not a propagation prediction.
- On the Maidenhead screen, **Space** can fill the From grid from a fresh GNSS fix if
  a compatible cap is attached and its ExtPort is powered. Press Space again to cancel.
  No fix leaves manual entry available and retains your grids. The app does not switch
  cap power, log or send your coordinates, or transmit over LoRa.
- Press **Tab** again for HF Conditions: UTC and approximate daylight at the From grid
  using [NOAA’s solar equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF), plus
  solar flux and estimated Kp. **R** refreshes these two readings from NOAA over the
  internet without sending a grid. Saved readings carry their own UTC source times.
  Flux older than 36 hours or Kp older than six hours is marked “old”.
- Press **Esc** to exit.

## Build and test on Linux

Host dependencies: a C++17 compiler, CMake ≥3.25, SDL2 development
libraries, and Git for the pinned LVGL and nlohmann JSON checkouts. Refresh
requires curl and a system certificate bundle; the offline tools do not use them.

```sh
cmake -S . -B build/desktop
cmake --build build/desktop --parallel
ctest --test-dir build/desktop --output-on-failure
./build/desktop/radiofieldkitzero
```

Set `-DRFK_UI=OFF` in a fresh build directory to test just the core without
SDL/LVGL. Or pass `-DRFK_LVGL_SOURCE=/path/to/pinned/lvgl` to use a locally
cached v9.5.0 checkout. See [device setup and tests](docs/DEVICE-TEST.md).

Developed for Tartanleaf.com by Greg Weir VA1GW. [MIT licence](LICENSE).

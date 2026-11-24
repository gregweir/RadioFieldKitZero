# Radio Field Kit Zero

Pocket tools for portable amateur radio on M5Stack Cardputer Zero. **Early
prototype:** antenna lengths, manually entered Maidenhead paths, and
UTC/daylight at the selected starting grid work without a network or any cap. The first physical device build has not been tested.
This targets the Linux ARM64 Cardputer Zero, not the ESP32 Cardputer or ADV.

## Try the antenna and location tools

- Type an HF frequency (1.8–30 MHz). The dipole value is **per leg** and the
  quarter-wave vertical value is **radiator only**, not a radial design.
- **A** switches antenna type, **U** switches metres/feet, and **Tab** opens
  the grid calculator. Enter a 4-, 6- or 8-character grid, then **Enter** moves
  to the other grid. **Backspace** edits and **Esc** exits. Press **Tab** again
  to see daylight at the From grid.
- All heights, materials, feed points, and surroundings can change the length
  needed. Cut long, trim and check before use. The estimate is 0.95 × wavelength
  / 4, not a resonant-length measurement or a band authorization.
- Paths use the great-circle short route between grid **centres** and show an
  initial **true** bearing. Coincident and antipodal points have no single
  bearing. The geographic line is not a radio propagation prediction.

## What comes next

GPS from the optional LoRa-1262 cap will save typing and won't be a prerequisite.
The conditions view shows UTC and approximate sun elevation using [NOAA’s solar
position equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF) and the
device clock. Dated space-weather readings are planned. These readings will not predict a contact. No LoRa transmission or CC1101 feature
is planned for the first release. See [release gates](docs/RELEASING.md).

## Build and test on Linux

Host dependencies: a C++17 compiler, CMake ≥3.25, SDL2 development
libraries, and Git for the pinned LVGL checkout.

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

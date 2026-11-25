# Device build and acceptance

Start with a stock M5Stack Cardputer Zero and the GNU ARM64 cross tools.
Use the pinned [v0.0.4 BSP](https://github.com/CardputerZero/launcher/releases/tag/v0.0.4)
from the project-isolated builder. The expected archive SHA-256 is
`e51b6eb803ed08f450e459efbfe62dd0341440846f3be9d01da861fe6cfdebb0`;
verify it before extracting. The project does not change the OS, install a
background service, or alter framebuffer or input-device permissions.

```sh
cmake -S . -B build/cardputerzero \
  -DCMAKE_TOOLCHAIN_FILE=cmake/cardputerzero-aarch64.cmake \
  -DCM0_SDK_ROOT=/path/to/verified/bsp -DRFK_DEVICE=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/cardputerzero --parallel
```

The first release used GNU ARM64 GCC 13.3, the pinned BSP above, and its
tested release profile (no C release flags, C++ release flags unchanged,
and explicit pthread linkage). To reproduce the filmed executable with the
same pinned toolchain, configure with `-DCMAKE_C_FLAGS_RELEASE:STRING=`,
`-DCMAKE_HAVE_LIBC_PTHREAD:INTERNAL=0`,
`-DCMAKE_HAVE_PTHREADS_CREATE:INTERNAL=0`, and
`-DCMAKE_HAVE_PTHREAD_CREATE:INTERNAL=1`.

For packaging, use the included 256 × 256 8-bit `packaging/icon.png` and reconfigure
the same build with `-DRFK_ENABLE_DEB=ON`,
`-DRFK_APP_ICON=/path/to/generated/icon.png`, and
`-DRFK_DEB_MAINTAINER="Name <approved-address>"`. Then build and run
`cpack --config build/cardputerzero/CPackConfig.cmake -B build/package -G DEB`.
Check `dpkg-deb -I`, `dpkg-deb -c`, the launcher paths, and the binary's ELF
machine/dependencies before installing. Obtain approval before installing a
candidate or touching any other app.

## Antenna cut-set checks (added after localtest6)

On the antenna screen, every `A` press cycles the type: Dipole, Inverted-V,
EFHW, Loop, Delta, Vertical, Linked 40/20, Linked 40/20/15, Linked 40/30/20,
Linked 30/17, J-pole, Slim Jim, back to Dipole. Each type keeps its own
last-typed frequency in the session (HF types start at 14.200 MHz; J-pole and
Slim Jim at 145.000). `U` still switches metres/feet and converts every number
on the screen.

1. Cycle at the defaults: dipole 5.01 m per leg at 14.200; EFHW 10.28 m
   radiator; loop 5.52 m sides; delta 7.36 m legs with the mid-side feed
   distance; vertical radiator plus the 32-radial length; J-pole 1.32 m +
   0.44 m stub; Slim Jim 1.32 m tall with the 0.02 m gap. The linked
   entries show per-side segment lengths and the link instruction; typing
   digits there must change nothing.
2. Spot-check one cut against a manual workshop calculation at the entered
   frequency (e.g. 7.100 MHz dipole → 10.03 m per leg). Values are
   cut-long starting points: trim to taste.
3. J-pole/Slim Jim accept 20–470 MHz; entering 480 shows
   `Enter 20 to 470 MHz`. HF types keep the 1.8–30 MHz gate.
4. Verify U converts every number (e.g. J-pole → 4.33 ft + 1.44 ft stub)
   and that A cycling never overlaps or clips any label.
5. Linked entries never transmit anything and change nothing beyond the
   schedule; their hint reminds: never change links while transmitting.

## Initial physical smoke

On the `0.1.0-1~localtest1` ARM64 candidate, a physical device with a cap
attached opened at 14.200 MHz, switched feet/metres, accepted two six-character
grids and exited with Esc. Cap features were not used.

## Use GPS and cut-set acceptance — 2026-11-25, package 0.1.0-1~localtest8
(SHA-256 bb98bb19…), owner-installed over an earlier candidate with the cap
attached and ExtPort powered

All checks in the Use GPS and Antenna cut-set sections above passed on the
physical device: the full A-key 12-entry cycle with default values, per-type
frequency memory, feet conversion, the 1.8–30 and 20–470 MHz prompts, an
indoor no-fix ending and manual-grid retention, an outdoor fix filling From
with the local grid (To untouched, redacted on capture), Space cancelling a
wait, and a repeat GPS attempt succeeding after the first. Linked entries
ignored typing and Backspace; no other app or the launcher was disturbed.

## Outstanding physical checks

- Open from APPLaunch without a cap and without Wi-Fi. All labels must fit.
- On launch, look for the blinking cursor beside 14.200 and the prompt to type
  a frequency. With Dipole selected, type 7.100 MHz (Sym+B for the decimal)
  without using Backspace or Enter first. Expect 10.03 m or 32.90 ft per leg.
  This also works as a demo without station data.
- Use Backspace to edit the last digit, then press Enter and type 14.200 to
  replace the whole value. Confirm 5.01 m and 16.45 ft per leg. Switch to
  radiator with A, then enter an out-of-range value.
- Press Tab, enter JJ00AA, press Enter, and enter II48AU. Expect 1341 km and
  264° true (a rounded grid-centre calculation). This checks that A and U
  are text in grid fields. If it differs, capture both inputs and the result.
  Return to Antenna and verify the earlier result is retained in the session.
- Exit with Esc, relaunch, and confirm other apps/launcher are unaffected.
- On HF conditions, press R with Wi-Fi on. Verify solar flux and estimated Kp
  have separate observation timestamps in UTC (Z), and the grid/daylight stays
  visible. Refresh should not block Tab or editing the other screens.
- Leave the app, turn Wi-Fi off, and reopen. The two readings should still be
  shown with the same source times. Press R: saved or old readings must remain.
  Do not clear an owner’s cache to test a first launch.
- On a clean image, install with dependency resolution and verify curl and
  ca-certificates are available.

## Use GPS on the Maidenhead screen (cap GNSS)

The app never switches ExtPort power, never elevates privileges, never transmits,
and never logs or sends a coordinate. Manual entry and the existing grids are
always kept on a failed or cancelled attempt.

1. With the cap on and ExtPort already powered, open the app, press Tab to the
   Maidenhead screen, and press Space. The hint shows
   `Use GPS: waiting for a fix`, and `Space cancel` appears in the key row.
2. Outdoors, expect `GPS grid filled` within about 30 s and a new
   6-character From grid (e.g. `From: JN58SC`). The To grid is untouched.
   The path result updates with the manual To grid as before.
3. Indoors, after 30 s expect `GPS: no fix. Manual grid kept` with both grids
   and the path result unchanged. Pressing Space again any time before the
   30 s elapses shows `GPS: cancelled. Manual grid kept`.
4. With the cap off but the port present, expect the same no-fix message.
   If the port disappears, expect `GPS: no port. Cap on ExtPort power?`.
5. While the wait is showing, Tab away and back, and type edits: the UI must
   stay responsive and a Tab cancels the wait without touching the grids.
6. `GPS: port busy` means another flock-holding process claims /dev/ttyS0.
   A reader that does not take the lock (e.g. cardputer-APRSZero) instead
   garbles the stream and shows the no-fix message.
7. If filmed with a fix, redact the From grid before export (RELEASING.md
   gate 6).

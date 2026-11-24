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

For packaging, render `packaging/icon.svg` to a 256 × 256 PNG and reconfigure
the same build with `-DRFK_ENABLE_DEB=ON`,
`-DRFK_APP_ICON=/path/to/generated/icon.png`, and
`-DRFK_DEB_MAINTAINER="Name <approved-address>"`. Then build and run
`cpack --config build/cardputerzero/CPackConfig.cmake -B build/package -G DEB`.
Check `dpkg-deb -I`, `dpkg-deb -c`, the launcher paths, and the binary's ELF
machine/dependencies before installing. Obtain approval before installing a
candidate or touching any other app.

## Outstanding physical checks

- Open from APPLaunch without a cap and without Wi-Fi. All labels must fit.
- Edit 14.200 MHz using the physical decimal key (Sym+B); confirm 5.01 m and
  16.45 ft per leg. Switch to radiator with A, then enter an out-of-range value.
- Press Tab and enter two grids, including A and U as letters, not shortcuts.
  Use Enter to move between fields, check a nonzero bearing and distance, edit,
  return to Antenna, and verify the earlier result is retained in the session.
- Exit with Esc, relaunch, and confirm other apps/launcher are unaffected.
- Add real GPS and stale/offline conditions checks when those features land.

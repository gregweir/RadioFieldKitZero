# Release and store checklist

The project starts private. These are gates, not a publication announcement.

1. **Core experience:** Validate cut lengths and path math independently, then
   test the edit flow with both successful and invalid input on the actual
   keyboard and 320 × 170 screen. Retain the familiar APPLaunch launch/quit.
2. **Conditions and optional cap:** Add local UTC and daylight for manual grids;
   handle polar day/night rather than fabricating sunrise or sunset. Use a
   verified NOAA SWPC source for solar flux and Kp. Distinguish observed,
   forecast and cached values with timestamps, including no-cache/offline and
   stale-cache behaviour. Store data locally, never include the user's grid
   in a weather request, and don't route lookups through a new service.
   Then verify cap GNSS on real hardware: explicit Use GPS (Space on the
   Maidenhead screen), no fix, real fix, manual fallback, missing cap and
   process exit. No LoRa transmission.
3. **Public export:** Audit the tracked tree *and all history* for local
   addresses/coordinates, credentials, generated binaries, private planning,
   automation tooling, personal media, and stale attribution. Use a clean
   allowlisted export to a single root if private history ever enters Git.
   Keep MIT and all third-party notices. Confirm the approved maintainer
   separately. Do not publish or submit without a specific release decision.
4. **Release build:** Pin build dependencies. Build the ARM64 .deb, inspect
   paths and permissions, verify the runtime ELF, and record source commit
   and SHA-256. Install that exact package on stock hardware for acceptance.
   Use a GitHub tag, source, installer and SHA256SUMS only once approved.
5. **Store package:** Read the current [publishing guide](https://github.com/CardputerZero/packages/blob/main/docs/PUBLISHING.md)
   and [App Builder](https://dev.cardputer.cc/) requirements again at release.
   Proposed slug: `radiofieldkitzero`; proposed name: **Radio Field Kit Zero**;
   proposed share code: **RFK0** (not reserved). Author: **VA1GW Greg Weir**.
   Use a 256 × 256 icon and review 1–6 320 × 170 screenshots made from current
   behaviour and synthetic grids. Declare network access for conditions
   refresh and optional GNSS hardware if the final binary uses them. No
   separate background service; don't claim a no-network permission for a build
   with network refresh. Update the store manifest after the feature set freezes.
6. **Store review:** Record the approved build launching through APPLaunch,
   demonstrate an antenna result, manual grid and conditions, then show the
   app exiting. If filmed with a GNSS fix, redact personal coordinates and
   document redactions. Pair the video with the exact package checksum and
   leave it out of Git. Check the upload size limit before submission; retain
   the original and create a smaller review copy if needed. Verify the review
   PR and device listing after approval before announcing availability.

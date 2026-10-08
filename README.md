<p align="center"><img src="buildroot/share/pixmaps/logo/marlin-outrun-nf-500.png" height="180" alt="Marlin Firmware logo" /></p>

<h1 align="center">Marlin 2.0.9.10 — MKS Robin Nano V3.1 (Enhanced &amp; Tested)</h1>

<p align="center">
  <a href="/LICENSE"><img alt="GPL-3.0 License" src="https://img.shields.io/badge/license-GPL--3.0-blue.svg"></a>
  <img alt="Marlin 2.0.9.10" src="https://img.shields.io/badge/Marlin-2.0.9.10-brightgreen.svg">
  <img alt="Board" src="https://img.shields.io/badge/board-MKS%20Robin%20Nano%20V3.1-orange.svg">
  <img alt="UI" src="https://img.shields.io/badge/UI-TFT__LVGL__UI%20(MKS%20TS35)-informational.svg">
  <img alt="Printer" src="https://img.shields.io/badge/printer-Creality%20CR--10-lightgrey.svg">
</p>

A working, flashed-and-printed-on build of **Marlin 2.0.9.10** for the **MKS Robin Nano V3.1**
with the **MKS TS35 touchscreen**, TMC2209 drivers, BLTouch and UBL — set up for a
**Creality CR-10** running the Robin Nano as a board upgrade.

Stock Marlin runs on this board, but several things on the MKS LVGL touchscreen are either
missing or dead-on-arrival: the **Level** button does nothing under UBL, there is no way to set
the Z probe offset from the screen, there is no mesh display, and out of the box **every print
aborts a few milliseconds after it starts** with "Power Outage / PRINTER HALTED" on boards with
no PW_DET module fitted. This fork fixes those and adds the screens that were missing.

Everything in here was built, flashed and used on real hardware — not a config dump.

---

## What's different from stock Marlin 2.0.9.10

### New touchscreen features

| Added | Where on the screen |
|---|---|
| **BLTouch settings screen** — live Z-offset babystepping (`M290`), reset &amp; probe, selectable 0.01 / 0.05 / 0.1 mm step, re-probe, `M500` save | `Settings > Machine Para > Leveling > BLTouch` |
| **Mesh visualizer** — UBL mesh heatmap, Build Mesh, Save Mesh | `… > BLTouch > Mesh`, and `Advanced > Mesh` |
| **Editable auto-level command** — on-screen keyboard, stored in SPI flash, used by the Tool **Level** button | `Settings > Machine Para > Leveling > Auto-level command` |
| **Working Level button** — injects a real UBL phase sequence instead of a bare `G29` | `Tool > Level` |
| **Paginated Advanced settings** — second page for Z Offset Wizard, BLTouch, Mesh | `Settings > Machine Para > Advanced` |
| **Full Mesh + View** — runs the full OctoPrint-style UBL script (heat bed, home, probe, save, activate) and opens the mesh view, which fills in live as points are probed | `Settings > Machine Para > Leveling` page 2 |
| **Custom menu items on the touchscreen** — `CUSTOM_MENU_MAIN` G-code macros surfaced in the LVGL UI, incl. a **Full UBL Mesh** button | `Tool > More` |
| **Paginated Leveling settings** — second page for Full Mesh + View | `Settings > Machine Para > Leveling` |

### Fixes

- **`POWER_LOSS_PIN -1`** — the board header defines `PA13` (PW_DET) whether or not a detect
  module is installed. PA13 is SWDIO and floats HIGH, which matches `POWER_LOSS_STATE`, so three
  polls into any print Marlin kills the heaters and halts. Disabling the pin keeps power-loss
  recovery working in journalled mode. Only set it back after an MKS PW_DET module is wired in.
- **UBL-aware level command** — a bare `G29` does nothing under UBL. The stored default is now
  `G28 / G29 P1 / G29 P3 / G29 S1 / M500 / G29 A`, and an old `G29N\nM500` string left in SPI
  flash is upgraded once on boot.
- **`RESTORE_LEVELING_AFTER_G28`** on, so a slicer's `G28` start G-code can't silently drop
  the mesh.
- Multi-line G-code is injected with `queue.enqueue_now_P()` instead of
  `queue.enqueue_one_now()`, which does not split on `\n` and silently ran only the first line.

### Tuned configuration

TMC2209 UART on all four axes, UBL 9×9 (81 points), S-curve acceleration, power-loss recovery,
filament runout, `MULTI_VOLUME` (onboard SD + USB drive), EEPROM on the board's I2C chip.
Full list in **[FIRMWARE_REFERENCE.md](FIRMWARE_REFERENCE.md)**, including a table of every
option deliberately left **off** and why.

---

## Tested hardware

| | |
|---|---|
| Board | MKS Robin Nano V3.1 (`BOARD_MKS_ROBIN_NANO_V3_1`, STM32F407VG) |
| Screen | MKS TS35 V2.0, 480×320, `TFT_LVGL_UI` + `TOUCH_SCREEN` |
| Drivers | TMC2209 ×4, UART mode (X / Y / Z / E0) |
| Probe | BLTouch, `NOZZLE_TO_PROBE_OFFSET { -15, -10, -1 }` |
| Leveling | `AUTO_BED_LEVELING_UBL`, 9×9 grid, `MULTIPLE_PROBING 2` |
| Printer | Creality CR-10 (MKS Robin Nano board upgrade) |
| Build volume | 310 × 310 × 400 mm |
| Extruder | 1, `TEMP_SENSOR_0 1`, max 275 °C |
| Bed | `TEMP_SENSOR_BED 1`, max 150 °C |
| Steps/mm | `{ 80, 80, 400, 430.38554 }` |
| Media | onboard SD + USB flash drive (`MULTI_VOLUME`) |
| Host serial | `SERIAL_PORT_2 3` @ 115200 |

Adjust steps/mm, bed size, probe offset and PID for your own machine before printing.

---

## Build

PlatformIO, with the default environment already set in `platformio.ini`:

```sh
pio run -e mks_robin_nano_v3_1_usb_flash_drive_msc
```

Output: `.pio/build/mks_robin_nano_v3_1_usb_flash_drive_msc/Robin_nano_v3.bin`

VS Code + the PlatformIO IDE extension works too — open the folder and hit Build.

## Flash

Put these at the root of a FAT32 SD card:

| What | From |
|---|---|
| `Robin_nano_v3.bin` | the build output above |
| `assets/` (140 `.bin` files, incl. `FontUNIGBK.bin`) | MKS's `Mks-Robin-Nano-Marlin2.0-Firmware-master/Firmware/assets` |

Insert the card, power cycle. The screen flashes the firmware, then copies the pictures and font
to SPI flash and renames `assets` to `_assets`. Only re-copy `assets` when the picture set
changes — firmware-only updates need just the `.bin`.

> The `MKS-Robin-Nano-V3.X-main` pack is **not** a drop-in substitute — it is missing
> `bmp_sd*.bin`, `bmp_usb_disk*.bin`, `bmp_lcd_sd*.bin` and `bmp_init_state.bin`. With
> `MULTI_VOLUME` on, the media-select buttons come up blank without them. Details in
> [FIRMWARE_REFERENCE.md](FIRMWARE_REFERENCE.md).

## First leveling run

1. `Settings > Machine Para > Leveling > BLTouch`
2. **Reset &amp; Probe** — zeroes the probe offset, homes, parks at bed centre at Z0.3
   (soft endstops are off while this screen is open).
3. Paper-gap the nozzle with **Z Offset + / −**, then **Save Offset** (`M500`).
4. **Mesh > Build Mesh** — runs the stored auto-level command (81 points, a few minutes).
5. **Save Mesh** if the command did not already save.

Or, in one button: `Settings > Machine Para > Leveling` → **next** → **Full Mesh + View**.
That runs `UBL_FULL_MESH_GCODE` — heat the bed to 97 °C, home, `G29 P1`/`P3`, save to
mesh slot 1, activate, `M500` — and opens the heatmap while it probes. Same sequence is
on `Tool > More` as **Full UBL Mesh**. It is the OctoPrint bed-level script with the
`;` comments and the `@BEDLEVELVISUALIZER` plugin marker removed; edit it in
`Configuration_adv.h`. Note `M140` does not wait for the bed — see
[FIRMWARE_REFERENCE.md](FIRMWARE_REFERENCE.md) for how to make it.

Remaining-time on the print screen comes from the slicer's `M73` lines, not an estimate —
enable M73 output in your slicer or the field stays blank.

---

## Documentation

- **[FIRMWARE_REFERENCE.md](FIRMWARE_REFERENCE.md)** — screen map, LVGL gotchas (4-row limit,
  picture lookup, queue injection limits), the full power-loss analysis, enabled/disabled
  feature tables, and the picture audit script.
- [docs/README_upstream_marlin.md](docs/README_upstream_marlin.md) — the original Marlin README.
- [marlinfw.org](https://marlinfw.org/) — upstream documentation and G-code reference.

## Contributing

Issues and PRs welcome, especially other Robin Nano V3.x variants, TS35 screen quirks and
LVGL screen additions. Please say which board revision, screen and probe you are on.

## Credits &amp; license

Based on [Marlin Firmware](https://github.com/MarlinFirmware/Marlin) 2.0.9.10 by Scott Lahteine
and the Marlin contributors, with the MKS LVGL UI from
[makerbase-mks](https://github.com/makerbase-mks). Licensed **GPL-3.0** — see [LICENSE](LICENSE).
Marlin is free software; keep it free by publishing your changes.

**No warranty.** 3D printers get hot and move fast. Check your thermistor types, thermal
protection, endstops and probe offset before leaving a print unattended.

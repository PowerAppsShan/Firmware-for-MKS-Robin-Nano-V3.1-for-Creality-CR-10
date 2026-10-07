# MKS Robin Nano V3.1 — Marlin 2.0.9.10 build reference

Board `BOARD_MKS_ROBIN_NANO_V3_1`, MKS TS35 (480x320) with `TFT_LVGL_UI`,
TMC2209 UART on X/Y/Z/E0, BLTouch, UBL.

PlatformIO env: `mks_robin_nano_v3_1_usb_flash_drive_msc`
Output: `.pio/build/mks_robin_nano_v3_1_usb_flash_drive_msc/Robin_nano_v3.bin`

```
~/.platformio/penv/bin/pio run -e mks_robin_nano_v3_1_usb_flash_drive_msc
```

## Flashing

SD card root needs:

| What | Where from |
|---|---|
| `Robin_nano_v3.bin` | the build output above |
| `assets/` (140 `.bin` files incl. `FontUNIGBK.bin`) | `Mks-Robin-Nano-Marlin2.0-Firmware-master/Firmware/assets` |

Power cycle. The screen flashes the firmware, then the pictures and font;
`assets` is renamed to `_assets` when done. Only re-copy `assets` when the
picture set changes — not on every firmware update.

The `MKS-Robin-Nano-V3.X-main` pack is **not** a complete substitute: it is
byte-identical for 131 files but is missing `bmp_sd.bin`, `bmp_sd_sel.bin`,
`bmp_usb_disk.bin`, `bmp_usb_disk_sel.bin`, `bmp_lcd_sd.bin`,
`bmp_lcd_sd_sel.bin` and `bmp_init_state.bin`. `MULTI_VOLUME` is enabled, so
the media-select screen needs `bmp_sd` and `bmp_usb_disk` or those buttons
come up blank. Its `bmp_enable.bin` / `bmp_disable.bin` are newer and are
worth copying over the old pack's.

## Screen map (LVGL)

```
Tool
 ├─ Preheat / Extrude / Move / Home
 ├─ Level ............... injects the stored auto-level command (see below)
 ├─ Filament change
 └─ More ................ G-code entry + CUSTOM_MENU_MAIN items 1,2,3,4,5

Settings > Machine Para > Leveling
 ├─ Tramming position
 ├─ Auto-level command ......... on-screen keyboard, stored in SPI flash
 ├─ Nozzle-to-probe offsets
 └─ BLTouch .................... homes and parks over bed centre on entry
      ├─ Z Offset + / -  ....... M290, babysteps the nozzle live
      ├─ Reset & Probe ......... M851 Z0, then home + park
      ├─ Mesh .................. heatmap + Build Mesh + Save Mesh
      ├─ step size ............. 0.01 / 0.05 / 0.1 mm
      ├─ Re-Probe .............. home + park again
      └─ Save Offset ........... M500, then G28 X Y

Settings > Machine Para > Advanced (two pages)
 page 1: Pause position, Filament settings, WiFi, Encoder
 page 2: Z Offset Wizard, BLTouch settings, Mesh
```

A PARA-style screen holds **four rows maximum** — `lv_screen_menu_item()`
indexes `line_points[]`, which has four entries. A fifth row reads past the
end of that array. Paginate with `uiCfg.para_ui_page` as
`draw_advance_settings.cpp` / `draw_acceleration_settings.cpp` do.

## Leveling workflow

1. `Settings > Machine Para > Leveling > BLTouch`
2. **Reset & Probe** — zeroes the probe offset, homes, parks at bed centre at
   Z0.3. Soft endstops are off while this screen is open so Z can go there.
3. Paper-gap the nozzle with **Z Offset + / -**, then **Save Offset** (M500).
4. **Mesh > Build Mesh** — runs the stored auto-level command.
5. **Save Mesh** (M500) if the command did not already save.

`DEFAULT_AUTO_LEVEL_GCODE` (`draw_ui.h`) is the default and the fallback:

```
G28
G29 P1      ; probe the 81 reachable points
G29 P3      ; infer the unreachable ones
G29 S1      ; store to mesh slot 1
M500
G29 A       ; activate
```

A bare `G29` does nothing under UBL — it needs a phase. The pre-UBL stock
default was `G29N\nM500`, which is why the Level button used to look dead.
`gCfgItems_init()` upgrades that stored string once on boot; a hand-written
command that names a phase is left alone. Edit it any time from
`Leveling > Auto-level command`.

`RESTORE_LEVELING_AFTER_G28` is on, so a slicer's `G28` start G-code cannot
silently switch the mesh back off.

## Power-loss recovery

`POWER_LOSS_RECOVERY` with `PLR_ENABLED_DEFAULT true`. No `POWER_LOSS_PIN` on
this board, so progress is journalled to the print media every
`POWER_LOSS_MIN_Z_CHANGE` (0.05 mm) instead of on a power-fail interrupt.

On the next boot `tft_lvgl_configuration.cpp` calls `recovery.load()`; a valid
recovery file puts the UI straight on the printing screen in `REPRINTING`
state. Press resume: the hotend and bed are brought back to the saved targets,
then `recovery.resume()` continues the file. Starting a new print instead
discards the recovery data.

### `POWER_LOSS_PIN` must be -1 on this board

`pins_MKS_ROBIN_NANO_V3_common.h:163` defines `POWER_LOSS_PIN PA13` (the PW_DET
header) whether or not a detect module is fitted. Leaving it at that default
with no module attached halts every print a few milliseconds after it starts:

- `POWER_LOSS_STATE` defaults to `HIGH` (`powerloss.h:41`)
- with neither `POWER_LOSS_PULLUP` nor `POWER_LOSS_PULLDOWN` set, the pin is
  `SET_INPUT(PA13)` — floating, no pull resistor (`powerloss.h:156`)
- PA13 is SWDIO and sits pulled up in its debug default, so it floats `HIGH`,
  which matches `POWER_LOSS_STATE`
- `MarlinCore.cpp:810` polls `recovery.outage()` **only while printing**, which
  is why the board idles happily and only dies once a print is confirmed
- three consecutive reads (`OUTAGE_THRESHOLD`, `powerloss.h:192`) call
  `_outage()`: `disable_all_heaters()` then
  `kill(GET_TEXT_F(MSG_OUTAGE_RECOVERY))`
- the kill screen reads "Power Outage" / "PRINTER HALTED" / "Please Reset", and
  nothing ever heats up because the heaters were switched off on the way out

Setting `POWER_LOSS_PIN -1` disables the sensor path and keeps recovery working
in journalled mode. Only set it back to `PA13` after an MKS PW_DET module is
actually wired in, and set `POWER_LOSS_STATE` / the pull direction to match it.

Toggle at runtime with `M413 S0` / `M413 S1` + `M500`.

## Enabled features

Motion: `S_CURVE_ACCELERATION`, `ADAPTIVE_STEP_SMOOTHING`, `ARC_SUPPORT`,
`BABYSTEPPING` + `BABYSTEP_ZPROBE_OFFSET` + `BABYSTEP_ALWAYS_AVAILABLE`.

Probing / leveling: `BLTOUCH`, `AUTO_BED_LEVELING_UBL` 9x9 (81 points),
`MULTIPLE_PROBING 2`, `Z_SAFE_HOMING`, `PROBING_MARGIN`,
`ENABLE_LEVELING_FADE_HEIGHT`, `UBL_SAVE_ACTIVE_ON_M500`,
`RESTORE_LEVELING_AFTER_G28`, `G26_MESH_VALIDATION`, `PROBE_OFFSET_WIZARD`.

Printing: `POWER_LOSS_RECOVERY`, `ADVANCED_PAUSE_FEATURE` +
`PARK_HEAD_ON_PAUSE`, `NOZZLE_PARK_FEATURE`, `FILAMENT_RUNOUT_SENSOR`,
`FILAMENT_LOAD_UNLOAD_GCODES` (M701/M702), `FWRETRACT` (G10/G11),
`ADAPTIVE_FAN_SLOWING`, `PRINTCOUNTER` (M78),
`LCD_SET_PROGRESS_MANUALLY` + `SHOW_REMAINING_TIME` +
`USE_M73_REMAINING_TIME`, `SDCARD_SORT_ALPHA`, `SD_ABORT_ON_ENDSTOP_HIT`,
`MULTI_VOLUME` (onboard SD + USB drive), `LONG_FILENAME_HOST_SUPPORT`,
`BINARY_FILE_TRANSFER`.

`draw_printing.cpp:247` only shows a remaining-time field when
`LCD_SET_PROGRESS_MANUALLY` and `USE_M73_REMAINING_TIME` are both on, so the
figure comes from the slicer's `M73` lines, not from an estimate. Slice with
M73 output enabled or the field stays blank.

Machine: `ENDSTOP_INTERRUPTS_FEATURE`, `ASSISTED_TRAMMING` (`G35`),
`GCODE_MACROS` (M810-M819), `GCODE_REPEAT_MARKERS` (M808),
`SOFT_RESET_VIA_SERIAL`.

TMC2209: `STEALTHCHOP_XY/Z/E`, `HYBRID_THRESHOLD`, `MONITOR_DRIVER_STATUS` +
`STOP_ON_ERROR`, `SQUARE_WAVE_STEPPING`, `TMC_DEBUG` (`M122`).

Host: `EMERGENCY_PARSER`, `HOST_ACTION_COMMANDS` + `HOST_PROMPT_SUPPORT`,
`AUTO_REPORT_TEMPERATURES`, `AUTO_REPORT_POSITION`, `REPORT_FAN_CHANGE`,
`EXTENDED_CAPABILITIES_REPORT`, `M114_DETAIL`.

Storage: `EEPROM_SETTINGS` on the board's 4K I2C EEPROM, `EEPROM_AUTO_INIT`.
A 9x9 mesh is 324 bytes per slot, so several UBL slots fit.

## Deliberately left off

| Option | Why |
|---|---|
| `LCD_BED_LEVELING`, `MESH_EDIT_MENU`, `LEVEL_BED_CORNERS`, `PID_EDIT_MENU`, `PID_AUTOTUNE_MENU`, `SOUND_MENU_ITEM`, `INDIVIDUAL_AXIS_HOMING_MENU`, `UBL_MESH_WIZARD` | all gated on `HAS_LCD_MENU`, which `TFT_LVGL_UI` never defines — no effect on this screen |
| `LIN_ADVANCE` | SanityCheck rejects it alongside `S_CURVE_ACCELERATION` unless `EXPERIMENTAL_SCURVE` is set |
| `PREHEAT_BEFORE_PROBING` | would heat and wait on every `G28`, and the BLTouch screen homes often |
| `SENSORLESS_HOMING` | needs DIAG wiring and per-axis stall tuning |
| `PIDTEMPBED` | needs `M303 E-1` autotune values first |
| `BLTOUCH_SET_5V_MODE` | depends on how the probe is wired; wrong setting can damage the board |
| `BLTOUCH_HS_MODE` | faster probing, but trades away the repeatability the mesh is for |
| `NOZZLE_CLEAN_FEATURE` | `G12` wipe coordinates are machine-specific; stock ones may be off the bed |
| `CALIBRATION_GCODE` | `G425` needs a conductive calibration cube on the bed |
| `Z_STEPPER_AUTO_ALIGN` | one Z stepper on this machine |
| `PROBE_TEMP_COMPENSATION` | its block needs `TEMP_SENSOR_PROBE`, which is 0 — a thermistor in the probe |
| `NO_MOTION_BEFORE_HOMING`, `HOME_AFTER_DEACTIVATE` | safer, but they block the Move screen until a home has run |
| `MEATPACK_ON_SERIAL_PORT_1` | only helps with a host that speaks MeatPack |

`CUSTOM_MENU_MAIN` is **not** MarlinUI-only here: `draw_more.cpp` injects
`MAIN_MENU_ITEM_n_GCODE`, so those items appear on the More screen.

## Gotchas

- Changing the `assets[]` list in `pic_manager.cpp` only matters during the
  SD-to-flash copy; at runtime `lv_get_pic_addr()` looks pictures up by name
  in the flash directory. A picture not in `assets[]` never reaches flash and
  draws as nothing — `bmp_init_state.bin` is in MKS's list but not upstream's.
- Big buttons want a 100x100 picture (32765 bytes). The `*_state` pictures are
  45x45 (4055 bytes) and are meant for small status icons.
- `queue.enqueue_one_now()` does not split on `\n` — it enqueues the whole
  string as one command and only the first G-code runs. Use
  `queue.enqueue_now_P()` (splits) or `queue.inject()` (64-char SRAM buffer).
- `queue.inject(const char*)` truncates at 63 characters; the stored
  auto-level command field is 100.
- 81 points at `MULTIPLE_PROBING 2` is 162 probe strokes. Drop
  `GRID_MAX_POINTS_X` to 7 if that is too slow — `MULTIPLE_PROBING 1` does not
  build, `SanityCheck.h:1772` requires 2 or more. Comment it out instead.
- Picture names are matched with `strcasecmp`, both in `arrayFindStr()` during
  the SD copy and in `lv_get_pic_addr()` at runtime, so the case mismatch
  between the code's `bmp_mov_changeSpeed.bin` and the list's
  `bmp_mov_changespeed.bin` is harmless.

## Picture audit

Every `"F:/*.bin"` in `mks_ui/*.cpp` was checked against `assets[]` in
`pic_manager.cpp` and against the files in the pack:

- 114 distinct pictures referenced by the screens
- all 114 appear in `assets[]`, so all of them reach SPI flash
- all 114 files exist in the `Mks-Robin-Nano-Marlin2.0-Firmware-master` pack
- the conditional entries line up with the build: `HAS_LANG_SELECT_SCREEN`,
  `HAS_LOGO_IN_FLASH` and `HAS_GCODE_DEFAULT_VIEW_IN_FLASH` all default to 1
  for this board, `MULTI_VOLUME` is on, and the `HAS_MULTI_EXTRUDER` pictures
  are gated the same way on both sides with `EXTRUDERS 1`

Re-run it after adding a screen:

```sh
cd Marlin/src/lcd/extui/mks_ui
grep -rhoE '"F:/[A-Za-z0-9_]+\.bin"' *.cpp | tr -d '"' | sed 's|F:/||' \
  | tr 'A-Z' 'a-z' | sort -u > /tmp/ref.txt
grep -hoE '[a-zA-Z0-9_]+\.bin' pic_manager.cpp | tr 'A-Z' 'a-z' | sort -u > /tmp/list.txt
comm -23 /tmp/ref.txt /tmp/list.txt   # anything printed never reaches flash
```

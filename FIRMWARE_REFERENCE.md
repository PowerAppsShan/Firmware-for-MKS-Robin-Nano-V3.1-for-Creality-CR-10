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
 └─ More ................ G-code entry + CUSTOM_MENU_MAIN items 1..6
                          item 6 = Full UBL Mesh (UBL_FULL_MESH_GCODE)

Settings > Machine Para > Leveling (two pages)
 page 1
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
 page 2
 └─ Full Mesh + View .......... runs UBL_FULL_MESH_GCODE, opens the mesh view

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

### Full Mesh + View

`UBL_FULL_MESH_GCODE` (`Configuration_adv.h`) is the OctoPrint bed-level
script, ported to run on the printer. Two entry points share the one macro:

- `Settings > Machine Para > Leveling` page 2 > **Full Mesh + View** — injects
  the sequence and immediately opens the mesh view, which repaints on the LVGL
  timer, so the heatmap fills in cell by cell as the points are probed.
- `Tool > More` > **Full UBL Mesh** — `MAIN_MENU_ITEM_6`, the same sequence
  without the mesh view. Needs `bmp_custom6.bin` in SPI flash or the button
  draws blank.

```
M140 S97        ; heat the bed (does NOT wait -- see below)
M117 Homing all axes
G28
M420 S0         ; leveling off before probing
M300 S1000 P500
M117 Creating UBL mesh
M155 S30        ; slow temp reports while probing
G29 P1          ; probe the reachable points
G29 P3          ; infer the rest
G29 S1          ; save to slot 1
G29 A           ; activate UBL
M420 S1 V       ; enable leveling, report the mesh
G29 L1          ; load slot 1 back
M500            ; mesh + fade + active slot to EEPROM
M155 S3
M117 UBL Mesh Complete
M300 S440 P200
M300 S660 P250
M300 S880 P300
```

Three things differ from the OctoPrint original, all because
`queue.inject()` hands each line straight to the parser:

- `;` comments and the leading indentation are gone — the injector does not
  strip either.
- `@BEDLEVELVISUALIZER` is gone. It is an OctoPrint plugin marker, not G-code,
  and the firmware's own mesh view replaces what it was for.
- `M140` does not block, so probing starts on a bed that is still heating, same
  as in OctoPrint. Change it to `M190 R` STRINGIFY(UBL_FULL_MESH_BED_TEMP) to
  wait for temperature first — it adds several minutes before the first probe.

`UBL_FULL_MESH_BED_TEMP` (97) and `UBL_FULL_MESH_SLOT` (1) are separate defines
next to the macro. The beeps need a buzzer on EXP1 — `BEEPER_PIN` is defined
for this board whenever a TFT is enabled, so `M300` compiles and runs either
way, but a bare TS35 with nothing on EXP1 is silent.

Injection uses `queue.inject(F(...))`, which stores a flash pointer and drains
it a line at a time, so the length of the sequence is not a problem. It does
replace any injected queue already in flight, so do not start it on top of
another macro.

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

## Printing from the computer

The screen follows a print it did not start. `printer_state_polling()` adopts
the job and draws the printing screen with the temperatures, Z, live speed,
elapsed time, progress bar and a working Pause / Stop, which before this only
happened for a print picked from the file list, from the WiFi module, or resumed
after a power loss.

Two kinds of job come in over USB:

| Started by | Detected from | Progress | Pause | Stop |
|---|---|---|---|---|
| `M23` + `M24` — host prints a file off the printer's own SD / USB drive | `IS_SD_PRINTING()` | file byte index | as from the file list | as from the file list |
| streamed G-code — host pushes the file line by line | `print_job_timer.isRunning()` | `M73 P` from the slicer | `M25` | `//action:cancel` + local abort |

`uiCfg.host_printing` marks the second kind, because almost everything the
printing screen does has to work differently for it.

**Detection.** A streamed job has no file on this side, so `print_job_timer` is
the only thing that knows one is running. With `PRINTJOB_TIMER_AUTOSTART` only
`M109` and `M190` start that timer — `M104` and `M140` can stop it but never
start it (`Temperature::auto_job_check_timer()`) — so it starts at the
wait-for-temperature that opens every slicer's start G-code. A preheat does not
trip it: hosts use `M104` / `M140`, and the Preheat screen calls
`setTargetHotend()` / `setTargetBed()` without going through G-code at all.
Typing `M109` or `M190` into the G-code screen *does* trip it, and lands on the
printing screen with nothing printing; Stop clears that.

**Progress** comes from the slicer's `M73 P` lines, read straight out of
`ui.progress_override` — `MarlinUI::_get_progress()` cannot be called because it
is compiled behind `HAS_DISPLAY`. PrusaSlicer and SuperSlicer emit `M73`
natively, Cura needs a plugin; without it the bar stays at 0% and everything
else on the screen still works. `M115` now advertises `BUILD_PERCENT`.

**Pause** injects `M25`, which with `PARK_HEAD_ON_PAUSE` is `M125`: it parks the
head and then blocks in `wait_for_confirmation()`. Because `loop()` is stuck
inside that call, `queue.advance()` stops running and the host's stream backs up
behind a line that is never acknowledged — that, not the host's cooperation, is
what actually halts a streamed job. `M125` also sends `//action:paused` and puts
the Advanced Pause dialogs up; their Confirm clears `wait_for_user`, and `M125`
unparks, restarts the job timer and returns on its own. So the screen takes
`print_state` back from Marlin rather than driving it, which also keeps it right
when the *host* sends `M25` / `M24` / `M108`.

**Stop** sends `//action:cancel` before the local abort. Clearing the queue only
drops the lines already received; nothing on the printer can stop a host from
sending more, so a host that ignores action commands will keep streaming at a
printer whose heaters have just been turned off. OctoPrint, Pronterface and
PrusaSlicer's host upload all act on it.

**End of print.** There is no `MF_SD_COMPLETE` for a streamed job, so the end is
a stopped job timer with the planner drained: the end G-code's `M104 S0` /
`M140 S0` stops the timer, as does `M77`. A Stop goes through
`printer_abort_print()`, which clears `host_printing` before the timer stops, so
the finish dialog only appears for a job that ran to its end. A host that cools
the nozzle mid-print would also look like an end of print, and one that leaves
the heaters on at the end leaves the screen in print mode until Stop is pressed.

The title row reads `Printing:Host`, since `list_file.long_name[sel_id]` holds
whichever file was browsed last and would be a lie rather than a blank.

### Two fixes this needed

- A finished media print never left `print_state == WORKING`. Nothing reset it,
  so `filament_check()` went on running on the Ready screen, and
  `printer_state_polling()` — which only adopts a job from `IDLE` — could not
  pick up a *second* print started over serial. `setProBarRate()` now sets
  `IDLE` along with the finish dialog.
- `Operation > Filament change` and the MKS filament-detect switch both set
  `print_state = PAUSING` directly. That state is only ever left by the media
  path, which waits on `card.getIndex() > MIN_FILE_PRINTED`, so on a streamed
  job it stuck there with Pause and Resume both dead. Both now call
  `printer_pause_print()`.

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
`ADAPTIVE_FAN_SLOWING`, `PRINTCOUNTER` (M78), `LCD_SET_PROGRESS_MANUALLY`
(M73), `SDCARD_SORT_ALPHA`, `SD_ABORT_ON_ENDSTOP_HIT`,
`MULTI_VOLUME` (onboard SD + USB drive), `LONG_FILENAME_HOST_SUPPORT`,
`BINARY_FILE_TRANSFER`.

`LCD_SET_PROGRESS_MANUALLY` is defined for `HAS_TFT_LVGL_UI` in a block of its
own, because stock Marlin only offers it inside `#if HAS_DISPLAY` and
`HAS_DISPLAY` does not cover this UI. `SanityCheck.h` had to learn about
`HAS_TFT_LVGL_UI` as well. It is on for the progress bar during a print streamed
from the computer, which has no file on the media to measure.

`SHOW_REMAINING_TIME` / `USE_M73_REMAINING_TIME` are **off** — they are gated on
display families this UI is not one of, and turning them on fails to link: they
pull in `MarlinUI::_calculated_remaining_time()`, which calls
`MarlinUI::_get_progress()`, and that one is compiled only under `HAS_DISPLAY`.
So the time field on the printing screen is elapsed time (`HH:MM:SS`), not
remaining time, and `draw_printing.cpp`'s `USE_M73_REMAINING_TIME` branch is
dead code in this build.

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

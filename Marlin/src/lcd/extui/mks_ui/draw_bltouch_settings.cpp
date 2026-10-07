/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "../../../inc/MarlinConfigPre.h"

#if BOTH(HAS_TFT_LVGL_UI, HAS_BED_PROBE)

#include "draw_ui.h"
#include <lv_conf.h>

#include "../../../gcode/queue.h"
#include "../../../gcode/gcode.h"
#include "../../../module/probe.h"
#include "../../../module/motion.h"
#include "../../../module/temperature.h"
#include "../../../inc/MarlinConfig.h"

#if ENABLED(EEPROM_SETTINGS)
  #include "../../../module/settings.h"
#endif

extern lv_group_t *g;
static lv_obj_t *scr;
static lv_obj_t *labelV, *buttonV, *zOffsetText, *tempText;

static float bltouch_step_dist = 0.01;

enum {
  ID_BLTOUCH_INIT = 1,
  ID_BLTOUCH_ZOFFSETPOS,
  ID_BLTOUCH_ZOFFSETNEG,
  ID_BLTOUCH_SAVE,
  ID_BLTOUCH_TEST,
  ID_BLTOUCH_MESH,
  ID_BLTOUCH_STEPS,
  ID_BLTOUCH_RETURN
};

// Home, lift, move to the middle of the bed and drop to just above it, which
// is the position all the Z offset tweaking is done from.
static void bltouch_probe_center(char * const buf, const size_t len) {
  snprintf_P(buf, len, PSTR("G28\nG1 Z10 F2400\nG1 X%d Y%d\nG0 Z0.3"),
             int((X_BED_SIZE) / 2), int((Y_BED_SIZE) / 2));
}

void bltouch_do_init(const bool resetZoffset) {
  char cmd[64];
  // Z0.3 is below the soft min once the probe offset is zeroed, so the move
  // would be clamped away and the carriage would appear to do nothing.
  TERN_(HAS_SOFTWARE_ENDSTOPS, soft_endstop._enabled = false);
  queue.clear();
  if (resetZoffset) {
    // Start from a zero probe offset so the following tweaks are absolute.
    queue.enqueue_now(F("M851 Z0"));
    probe.offset.z = 0;
  }
  bltouch_probe_center(cmd, sizeof(cmd));
  // enqueue_now_P splits on '\n'. enqueue_one_now would push the whole
  // multi-line string in as one command, so only the G28 would ever run.
  queue.enqueue_now_P(cmd);
}

static void event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event != LV_EVENT_RELEASED) return;
  char cmd[64];
  char str_1[16];
  switch (obj->mks_obj_id) {
    case ID_BLTOUCH_INIT:
      bltouch_do_init(true);
      break;
    // With BABYSTEP_ZPROBE_OFFSET, M290 Z moves the nozzle and shifts
    // probe.offset.z by the same amount, so M500 is all that's needed to save.
    case ID_BLTOUCH_ZOFFSETPOS:
      sprintf_P(cmd, PSTR("M290 Z%s"), dtostrf(bltouch_step_dist, 1, 3, str_1));
      gcode.process_subcommands_now(cmd);
      disp_bltouch_z_offset_value();
      break;
    case ID_BLTOUCH_ZOFFSETNEG:
      sprintf_P(cmd, PSTR("M290 Z%s"), dtostrf(-bltouch_step_dist, 1, 3, str_1));
      gcode.process_subcommands_now(cmd);
      disp_bltouch_z_offset_value();
      break;
    case ID_BLTOUCH_SAVE:
      if (!queue.ring_buffer.full(2)) {
        TERN_(EEPROM_SETTINGS, (void)settings.save());
        queue.enqueue_now(F("G28 X Y"));
      }
      break;
    case ID_BLTOUCH_TEST:
      if (queue.ring_buffer.empty()) {
        bltouch_probe_center(cmd, sizeof(cmd));
        queue.enqueue_now_P(cmd);
      }
      break;
    #if HAS_MESH
      case ID_BLTOUCH_MESH:
        lv_clear_bltouch_settings();
        lv_draw_mesh_visualizer();
        return;   // the screen is gone, don't touch its widgets below
    #endif
    case ID_BLTOUCH_STEPS:
      if (ABS(int(100 * bltouch_step_dist)) == 1)
        bltouch_step_dist = 0.05;
      else if (ABS(int(100 * bltouch_step_dist)) == 5)
        bltouch_step_dist = 0.1;
      else
        bltouch_step_dist = 0.01;
      disp_bltouch_step_dist();
      break;
    case ID_BLTOUCH_RETURN:
      goto_previous_ui();
      queue.inject(F("G28 X Y"));
      return;
  }
}

void lv_draw_bltouch_settings() {
  scr = lv_screen_create(BLTOUCH_UI, machine_menu.BLTouchLevelingConfTitle);

  // Row 1: Z+, probe init, mesh view, Z-
  lv_big_button_create(scr, "F:/bmp_Add.bin", machine_menu.BLTouchOffsetpos, INTERVAL_V, titleHeight, event_handler, ID_BLTOUCH_ZOFFSETPOS);
  lv_big_button_create(scr, "F:/bmp_zeroAll.bin", machine_menu.BLTouchInit, BTN_X_PIXEL + INTERVAL_V * 2, titleHeight, event_handler, ID_BLTOUCH_INIT);
  #if HAS_MESH
    lv_big_button_create(scr, "F:/bmp_leveling.bin", machine_menu.MeshViewTitle, BTN_X_PIXEL * 2 + INTERVAL_V * 3, titleHeight, event_handler, ID_BLTOUCH_MESH);
  #endif
  lv_big_button_create(scr, "F:/bmp_Dec.bin", machine_menu.BLTouchOffsetneg, BTN_X_PIXEL * 3 + INTERVAL_V * 4, titleHeight, event_handler, ID_BLTOUCH_ZOFFSETNEG);

  // Row 2: step size, re-probe test, save, back
  buttonV = lv_imgbtn_create(scr, nullptr, INTERVAL_V, BTN_Y_PIXEL + INTERVAL_H + titleHeight, event_handler, ID_BLTOUCH_STEPS);
  labelV = lv_label_create_empty(buttonV);
  #if HAS_ROTARY_ENCODER
    if (gCfgItems.encoder_enable) lv_group_add_obj(g, buttonV);
  #endif

  lv_big_button_create(scr, "F:/bmp_in.bin", machine_menu.BLTouchTest, BTN_X_PIXEL + INTERVAL_V * 2, BTN_Y_PIXEL + INTERVAL_H + titleHeight, event_handler, ID_BLTOUCH_TEST);
  lv_big_button_create(scr, "F:/bmp_set.bin", machine_menu.BLTouchSave, BTN_X_PIXEL * 2 + INTERVAL_V * 3, BTN_Y_PIXEL + INTERVAL_H + titleHeight, event_handler, ID_BLTOUCH_SAVE);
  lv_big_button_create(scr, "F:/bmp_return.bin", common_menu.text_back, BTN_X_PIXEL * 3 + INTERVAL_V * 4, BTN_Y_PIXEL + INTERVAL_H + titleHeight, event_handler, ID_BLTOUCH_RETURN);

  // Live readouts share the title row
  zOffsetText = lv_label_create(scr, 160, TITLE_YPOS, nullptr);
  tempText = lv_label_create(scr, 290, TITLE_YPOS, nullptr);

  disp_bltouch_step_dist();
  disp_bltouch_z_offset_value();
}

void disp_bltouch_step_dist() {
  if (int(100 * bltouch_step_dist) == 1)
    lv_imgbtn_set_src_both(buttonV, "F:/bmp_baby_move0_01.bin");
  else if (int(100 * bltouch_step_dist) == 5)
    lv_imgbtn_set_src_both(buttonV, "F:/bmp_baby_move0_05.bin");
  else
    lv_imgbtn_set_src_both(buttonV, "F:/bmp_baby_move0_1.bin");

  if (gCfgItems.multiple_language) {
    if (int(100 * bltouch_step_dist) == 1)
      lv_label_set_text(labelV, move_menu.step_001mm);
    else if (int(100 * bltouch_step_dist) == 5)
      lv_label_set_text(labelV, move_menu.step_005mm);
    else
      lv_label_set_text(labelV, move_menu.step_01mm);
    lv_obj_align(labelV, buttonV, LV_ALIGN_IN_BOTTOM_MID, 0, BUTTON_TEXT_Y_OFFSET);
  }
}

void disp_bltouch_z_offset_value() {
  char buf[32];
  char str_1[16];
  sprintf_P(buf, PSTR("Z: %s mm"), dtostrf(probe.offset.z, 1, 3, str_1));
  lv_label_set_text(zOffsetText, buf);

  #if HAS_HOTEND
    sprintf_P(buf, PSTR("E %d/%d  B %d/%d"),
      thermalManager.wholeDegHotend(0), thermalManager.degTargetHotend(0),
      TERN0(HAS_HEATED_BED, thermalManager.wholeDegBed()),
      TERN0(HAS_HEATED_BED, thermalManager.degTargetBed()));
    lv_label_set_text(tempText, buf);
  #endif
}

void lv_clear_bltouch_settings() {
  // Every way out of this screen goes through here, so the soft endstops
  // bltouch_do_init() switched off are always put back.
  TERN_(HAS_SOFTWARE_ENDSTOPS, soft_endstop._enabled = true);
  #if HAS_ROTARY_ENCODER
    if (gCfgItems.encoder_enable) lv_group_remove_all_objs(g);
  #endif
  lv_obj_del(scr);
}

#endif // HAS_TFT_LVGL_UI && HAS_BED_PROBE

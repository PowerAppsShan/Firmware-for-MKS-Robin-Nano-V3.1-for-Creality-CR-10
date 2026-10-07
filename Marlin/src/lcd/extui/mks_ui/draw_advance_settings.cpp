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

#if HAS_TFT_LVGL_UI

#include "draw_ui.h"
#include <lv_conf.h>

#include "../../../inc/MarlinConfig.h"

extern lv_group_t *g;
static lv_obj_t *scr;

// A PARA screen fits four rows: lv_screen_menu_item() indexes line_points[],
// which only holds four separators, so a fifth row reads past the end of it.
#define ADV_ITEMS_PER_PAGE 4

enum {
  ID_ADVANCE_RETURN = 1,
  ID_PAUSE_POS,
  ID_WIFI_PARA,
  ID_FILAMENT_SETTINGS,
  ID_ENCODER_SETTINGS,
  ID_ADV_Z_OFFSET_WIZARD,
  ID_ADV_BLTOUCH,
  ID_ADV_MESH,
  ID_ADV_UP,
  ID_ADV_DOWN
};

static void event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event != LV_EVENT_RELEASED) return;
  switch (obj->mks_obj_id) {
    case ID_ADVANCE_RETURN:
      uiCfg.para_ui_page = false;
      lv_clear_advance_settings();
      draw_return_ui();
      break;
    case ID_PAUSE_POS:
      lv_clear_advance_settings();
      lv_draw_pause_position();
      break;
    case ID_FILAMENT_SETTINGS:
      lv_clear_advance_settings();
      lv_draw_filament_settings();
      break;
    #if ENABLED(MKS_WIFI_MODULE)
      case ID_WIFI_PARA:
        lv_clear_advance_settings();
        lv_draw_wifi_settings();
        break;
    #endif
    #if HAS_ROTARY_ENCODER
      case ID_ENCODER_SETTINGS:
        lv_clear_advance_settings();
        lv_draw_encoder_settings();
        break;
    #endif
    #if ENABLED(PROBE_OFFSET_WIZARD)
      case ID_ADV_Z_OFFSET_WIZARD:
        lv_clear_advance_settings();
        lv_draw_z_offset_wizard();
        break;
    #endif
    #if HAS_BED_PROBE
      case ID_ADV_BLTOUCH:
        lv_clear_advance_settings();
        bltouch_do_init(false);
        lv_draw_bltouch_settings();
        break;
    #endif
    #if HAS_MESH
      case ID_ADV_MESH:
        lv_clear_advance_settings();
        lv_draw_mesh_visualizer();
        break;
    #endif
    case ID_ADV_UP:
      uiCfg.para_ui_page = false;
      lv_clear_advance_settings();
      lv_draw_advance_settings();
      break;
    case ID_ADV_DOWN:
      uiCfg.para_ui_page = true;
      lv_clear_advance_settings();
      lv_draw_advance_settings();
      break;
  }
}

void lv_draw_advance_settings() {
  scr = lv_screen_create(ADVANCED_UI, machine_menu.AdvancedConfTitle);

  // Built as a table so the rows stay correctly numbered whichever of the
  // optional entries are compiled in.
  struct { const char *text; int id; } const items[] = {
    { machine_menu.PausePosition, ID_PAUSE_POS },
    { machine_menu.FilamentConf,  ID_FILAMENT_SETTINGS },
    #if ENABLED(MKS_WIFI_MODULE)
      { machine_menu.WifiSettings, ID_WIFI_PARA },
    #endif
    #if HAS_ROTARY_ENCODER
      { machine_menu.EncoderSettings, ID_ENCODER_SETTINGS },
    #endif
    #if ENABLED(PROBE_OFFSET_WIZARD)
      { machine_menu.LevelingZoffsetTitle, ID_ADV_Z_OFFSET_WIZARD },
    #endif
    #if HAS_BED_PROBE
      { machine_menu.BLTouchLevelingConf, ID_ADV_BLTOUCH },
    #endif
    #if HAS_MESH
      { machine_menu.MeshViewTitle, ID_ADV_MESH },
    #endif
  };
  constexpr uint8_t item_count = COUNT(items);

  const uint8_t first = uiCfg.para_ui_page ? ADV_ITEMS_PER_PAGE : 0;
  lv_coord_t y = PARA_UI_POS_Y;
  for (uint8_t i = 0; i < ADV_ITEMS_PER_PAGE && first + i < item_count; i++) {
    lv_screen_menu_item(scr, items[first + i].text, PARA_UI_POS_X, y, event_handler, items[first + i].id, i);
    y += PARA_UI_POS_Y;
  }

  if (item_count > ADV_ITEMS_PER_PAGE) {
    const bool on_page_2 = uiCfg.para_ui_page;
    lv_big_button_create(scr, "F:/bmp_back70x40.bin",
      on_page_2 ? machine_menu.previous : machine_menu.next,
      PARA_UI_TURN_PAGE_POS_X, PARA_UI_TURN_PAGE_POS_Y,
      event_handler, on_page_2 ? ID_ADV_UP : ID_ADV_DOWN, true);
  }

  lv_big_button_create(scr, "F:/bmp_back70x40.bin", common_menu.text_back, PARA_UI_BACK_POS_X + 10, PARA_UI_BACK_POS_Y, event_handler, ID_ADVANCE_RETURN, true);
}

void lv_clear_advance_settings() {
  #if HAS_ROTARY_ENCODER
    if (gCfgItems.encoder_enable) lv_group_remove_all_objs(g);
  #endif
  lv_obj_del(scr);
}

#endif // HAS_TFT_LVGL_UI

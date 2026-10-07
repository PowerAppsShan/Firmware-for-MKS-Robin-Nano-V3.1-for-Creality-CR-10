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

#if BOTH(HAS_TFT_LVGL_UI, HAS_MESH)

#include "draw_ui.h"
#include <lv_conf.h>

#include "../../../gcode/queue.h"
#include "../../../feature/bedlevel/bedlevel.h"
#if DISABLED(AUTO_BED_LEVELING_UBL)
  extern bed_mesh_t z_values;
#endif
#include "../../../inc/MarlinConfig.h"

// Mesh heatmap, drawn into an indexed (16-colour) canvas so the whole
// GRID_MAX_POINTS_X * GRID_MAX_POINTS_Y grid costs one LVGL object instead of
// one per cell -- LV_MEM_SIZE is only 16K and a per-cell object grid overflows it.
#define MESH_VIEW_X       10
#define MESH_VIEW_Y       40
#define MESH_CELL_W       (260 / (GRID_MAX_POINTS_X))
#define MESH_CELL_H       (130 / (GRID_MAX_POINTS_Y))
#define MESH_VIEW_W       (MESH_CELL_W * (GRID_MAX_POINTS_X))
#define MESH_VIEW_H       (MESH_CELL_H * (GRID_MAX_POINTS_Y))

// Palette slots
#define MESH_COL_BG       0   // outside/behind the grid
#define MESH_COL_GRID     1   // cell separator
#define MESH_COL_UNSET    2   // point that has never been probed
#define MESH_COL_FIRST    3   // first heatmap entry
#define MESH_COL_LAST     15  // last heatmap entry
#define MESH_COL_STEPS    (MESH_COL_LAST - MESH_COL_FIRST + 1)

extern lv_group_t *g;
static lv_obj_t *scr, *canvas, *labelRange, *labelMinMax;

static uint8_t mesh_canvas_buf[LV_CANVAS_BUF_SIZE_INDEXED_4BIT(MESH_VIEW_W, MESH_VIEW_H)];

enum {
  ID_MESH_BUILD = 1,
  ID_MESH_SAVE,
  ID_MESH_RETURN
};

static void event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event != LV_EVENT_RELEASED) return;
  switch (obj->mks_obj_id) {
    case ID_MESH_BUILD:
      // Same command the Tool > Level button runs, editable from
      // Settings > Machine Para > Leveling > Auto Level Command.
      get_gcode_command(AUTO_LEVELING_COMMAND_ADDR, (uint8_t *)public_buf_m);
      public_buf_m[sizeof(public_buf_m) - 1] = 0;
      if (public_buf_m[0] >= ' ')
        queue.inject(public_buf_m);
      else
        queue.inject(F(DEFAULT_AUTO_LEVEL_GCODE));
      break;
    case ID_MESH_SAVE:
      queue.inject(F("M500"));
      break;
    case ID_MESH_RETURN:
      goto_previous_ui();
      break;
  }
}

// Write one pixel straight into our own buffer. lv_canvas_set_px() invalidates
// the object on every call, which is far too slow for a full repaint.
static void mesh_set_px(const lv_coord_t x, const lv_coord_t y, const uint8_t idx) {
  uint8_t *p = mesh_canvas_buf + sizeof(lv_color32_t) * 16;   // skip palette
  const uint8_t shift = (x & 0x1) ? 0 : 4;
  p += (((MESH_VIEW_W) + 1) >> 1) * y + (x >> 1);
  *p = (*p & ~(0xF << shift)) | ((idx & 0xF) << shift);
}

static void mesh_fill_cell(const uint8_t cx, const uint8_t cy, const uint8_t idx) {
  const lv_coord_t x0 = cx * (MESH_CELL_W), y0 = cy * (MESH_CELL_H);
  for (lv_coord_t y = 0; y < (MESH_CELL_H); y++)
    for (lv_coord_t x = 0; x < (MESH_CELL_W); x++) {
      // 1px separator along the top/left of each cell
      const bool edge = (x == 0 || y == 0);
      mesh_set_px(x0 + x, y0 + y, edge ? MESH_COL_GRID : idx);
    }
}

void disp_mesh_visualizer() {
  // UBL keeps the grid inside its instance; bilinear and MBL use a global.
  #if ENABLED(AUTO_BED_LEVELING_UBL)
    const bed_mesh_t &mesh = bedlevel.z_values;
  #else
    const bed_mesh_t &mesh = z_values;
  #endif

  // Range of the probed points, ignoring points UBL has not measured yet.
  float lo = 0, hi = 0;
  bool any = false;
  GRID_LOOP(x, y) {
    const float v = mesh[x][y];
    if (isnan(v)) continue;
    if (!any) { lo = hi = v; any = true; }
    else { NOLESS(hi, v); NOMORE(lo, v); }
  }

  const float span = hi - lo;
  GRID_LOOP(x, y) {
    const float v = mesh[x][y];
    uint8_t idx = MESH_COL_UNSET;
    if (!isnan(v)) {
      const uint8_t step = span > 0.0001f
        ? uint8_t((v - lo) / span * ((MESH_COL_STEPS) - 1) + 0.5f)
        : (MESH_COL_STEPS) / 2;
      idx = (MESH_COL_FIRST) + step;
    }
    // Front of the bed is the bottom of the screen, so flip Y.
    mesh_fill_cell(x, (GRID_MAX_POINTS_Y) - 1 - y, idx);
  }
  lv_obj_invalidate(canvas);

  char str_1[16], str_2[16], buf[32];
  if (any) {
    sprintf_P(buf, PSTR("Min %s\nMax %s"), dtostrf(lo, 1, 3, str_1), dtostrf(hi, 1, 3, str_2));
    lv_label_set_text(labelMinMax, buf);
    sprintf_P(buf, PSTR("Range\n%s mm"), dtostrf(span, 1, 3, str_1));
    lv_label_set_text(labelRange, buf);
  }
  else {
    lv_label_set_text(labelMinMax, "No mesh");
    lv_label_set_text(labelRange, "Build it");
  }
}

void lv_draw_mesh_visualizer() {
  scr = lv_screen_create(MESH_VIEW_UI, machine_menu.MeshViewTitle);

  canvas = lv_canvas_create(scr, nullptr);
  lv_canvas_set_buffer(canvas, mesh_canvas_buf, MESH_VIEW_W, MESH_VIEW_H, LV_IMG_CF_INDEXED_4BIT);
  lv_obj_set_pos(canvas, MESH_VIEW_X, MESH_VIEW_Y);

  lv_canvas_set_palette(canvas, MESH_COL_BG,    lv_color_hex(0x000000));
  lv_canvas_set_palette(canvas, MESH_COL_GRID,  lv_color_hex(0x303030));
  lv_canvas_set_palette(canvas, MESH_COL_UNSET, lv_color_hex(0x606060));
  // Low (blue) through mid (green) to high (red)
  static const uint32_t heat[MESH_COL_STEPS] = {
    0x0000C0, 0x0040E0, 0x0080FF, 0x00C0D0, 0x00E0A0,
    0x00C040, 0x20C000, 0x80D000, 0xC0C000, 0xE09000,
    0xF06000, 0xF02000, 0xC00000
  };
  for (uint8_t i = 0; i < (MESH_COL_STEPS); i++)
    lv_canvas_set_palette(canvas, (MESH_COL_FIRST) + i, lv_color_hex(heat[i]));

  labelMinMax = lv_label_create(scr, MESH_VIEW_X + (MESH_VIEW_W) + 10, MESH_VIEW_Y, nullptr);
  labelRange  = lv_label_create(scr, MESH_VIEW_X + (MESH_VIEW_W) + 10, MESH_VIEW_Y + 60, nullptr);

  lv_big_button_create(scr, "F:/bmp_leveling.bin", machine_menu.MeshBuild, INTERVAL_V, BTN_Y_PIXEL + INTERVAL_H + titleHeight, event_handler, ID_MESH_BUILD);
  lv_big_button_create(scr, "F:/bmp_set.bin", machine_menu.MeshSave, BTN_X_PIXEL + INTERVAL_V * 2, BTN_Y_PIXEL + INTERVAL_H + titleHeight, event_handler, ID_MESH_SAVE);
  lv_big_button_create(scr, "F:/bmp_return.bin", common_menu.text_back, BTN_X_PIXEL * 3 + INTERVAL_V * 4, BTN_Y_PIXEL + INTERVAL_H + titleHeight, event_handler, ID_MESH_RETURN);

  disp_mesh_visualizer();
}

void lv_clear_mesh_visualizer() {
  #if HAS_ROTARY_ENCODER
    if (gCfgItems.encoder_enable) lv_group_remove_all_objs(g);
  #endif
  lv_obj_del(scr);
}

#endif // HAS_TFT_LVGL_UI && HAS_MESH

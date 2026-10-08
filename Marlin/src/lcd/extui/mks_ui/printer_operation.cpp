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

#include "../../marlinui.h"   // ui.set_progress()
#include "../../../gcode/gcode.h"
#include "../../../module/planner.h"
#include "../../../module/motion.h"
#include "../../../module/printcounter.h"
#include "../../../sd/cardreader.h"
#include "../../../inc/MarlinConfig.h"
#include "../../../MarlinCore.h"
#include "../../../gcode/queue.h"

#if ENABLED(POWER_LOSS_RECOVERY)
  #include "../../../feature/powerloss.h"
#endif

#if ENABLED(HOST_ACTION_COMMANDS)
  #include "../../../feature/host_actions.h"
#endif

#if ENABLED(ADVANCED_PAUSE_FEATURE)
  #include "../../../feature/pause.h"   // for did_pause_print
#endif

extern uint32_t To_pre_view;
extern bool flash_preview_begin, default_preview_flg, gcode_preview_over;
extern bool once_flag;
extern uint8_t sel_id;

// Set once a streamed job has really been seen paused. printer_pause_print()
// moves print_state to PAUSED as soon as M25 is injected, but M25 does not run
// until queue.advance() gets to it, so for that gap Marlin still reports a
// running job -- and without this latch the sync below would read that as "the
// pause ended" and put the screen straight back to WORKING.
static bool host_pause_seen; // = false

void printer_state_polling() {
  char str_1[16];

  // Follow a print this UI did not start itself. Only the file dialog, the WiFi
  // module and power-loss recovery ever moved this UI into PRINTING_UI, so a job
  // started from the computer left the screen on Ready for its whole duration,
  // with no progress, no temperatures and no reachable Pause / Stop.
  //
  // Two kinds of print arrive over the serial port:
  //
  //  - A media print (M23 then M24): the host tells the printer to read a file
  //    from its own SD card or USB drive. The byte index gives progress and the
  //    buttons act on the job exactly as they do for a print started from the
  //    file list. IS_SD_PRINTING() is false while an abort is pending, so a Stop
  //    cannot be undone by this.
  //
  //  - A streamed print: the host pushes G-code line by line and there is no
  //    file on this side at all, so print_job_timer is the only thing that knows
  //    a job is running. With PRINTJOB_TIMER_AUTOSTART only M109 and M190 start
  //    it -- M104 and M140 can stop it but never start it, see
  //    Temperature::auto_job_check_timer() -- so it starts at the
  //    wait-for-temperature that opens every slicer's start G-code. A preheat
  //    does not trip it: hosts use M104 / M140 for that, and this UI's Preheat
  //    screen calls setTargetHotend() / setTargetBed() without going through
  //    G-code at all. Typing M109 or M190 into the G-code screen does trip it.
  if (uiCfg.print_state == IDLE) {
    const bool media_job = IS_SD_PRINTING(),
               host_job  = !media_job && print_job_timer.isRunning();
    if (media_job || host_job) {
      switch (disp_state) {
        case PRINTING_UI:                                 // already there
        case DIALOG_UI: case KEYBOARD_UI: case NUMBER_KEY_UI:
          break;                                          // don't tear down a modal or an entry field
        default: {
          uiCfg.print_state   = WORKING;
          uiCfg.host_printing = host_job;
          host_pause_seen     = false;

          // Take over the per-job values the printing screen reads. Not written
          // to SPI flash: they are re-derived at every print start.
          gCfgItems.from_flash_pic = false;               // no thumbnail was extracted
          gCfgItems.curFilesize    = 0;                   // no file; setProBarRate() falls back to M73
          #if ENABLED(SDSUPPORT)
            if (media_job) {
              gCfgItems.curFilesize = card.getFileSize();
              strncpy(list_file.long_name[sel_id], card.longest_filename(), sizeof(list_file.long_name[0]) - 1);
              list_file.long_name[sel_id][sizeof(list_file.long_name[0]) - 1] = '\0';
            }
          #endif

          // M73 progress is per job and nothing else clears it for a streamed
          // print, so last job's figure would otherwise still be on the bar.
          TERN_(LCD_SET_PROGRESS_MANUALLY, ui.set_progress(0));

          // Take the elapsed time from Marlin's own job timer rather than from
          // zero, so adopting a job part-way through does not restart the clock.
          const uint32_t elapsed = print_job_timer.duration();
          print_time.hours   = elapsed / 3600;
          print_time.minutes = (elapsed / 60) % 60;
          print_time.seconds = elapsed % 60;
          print_time.ms_10   = 0;
          start_print_time();

          once_flag = false;                              // re-arm the "print finished" dialog

          // Only the UI's own print start clears this, and while it is set the
          // printing screen ignores every touch (draw_printing.cpp), the
          // progress bar stops updating, and disp_pre_gcode() keeps rendering a
          // thumbnail out of whatever file was browsed last -- which at row 200
          // would open and print that file. Make sure a previous interrupted
          // preview cannot leak into this job.
          gcode_preview_over = false;

          clear_cur_ui();
          default_preview_flg = true;                     // no file thumbnail to show
          lv_draw_printing();
        } break;
      }
    }
  }

  // A streamed print is paused, resumed and finished by Marlin and by the host,
  // not by this screen: Pause injects M25 and the Advanced Pause dialog's
  // Confirm releases it, and the host can send M25 / M24 / M108 / M77 of its
  // own. Take the state from Marlin so the buttons match what actually happened.
  if (uiCfg.host_printing) {
    const bool paused = print_job_timer.isPaused() || TERN0(ADVANCED_PAUSE_FEATURE, did_pause_print);
    if (paused) host_pause_seen = true;

    if (uiCfg.print_state == WORKING && paused) {
      uiCfg.print_state = PAUSED;
      stop_print_time();
    }
    else if (uiCfg.print_state == PAUSED && host_pause_seen && !paused && print_job_timer.isRunning()) {
      host_pause_seen = false;
      uiCfg.print_state = WORKING;
      start_print_time();
    }
    else if (uiCfg.print_state == WORKING && !paused && !print_job_timer.isRunning() && !planner.has_blocks_queued()) {
      // The job is over. There is no MF_SD_COMPLETE for a streamed print, so a
      // stopped job timer with the planner drained is the only end-of-print
      // signal there is: the end G-code's M104 S0 / M140 S0 stops the timer
      // (auto_job_check_timer again), as does an M77. A Stop goes through
      // printer_abort_print(), which clears host_printing before the timer ever
      // stops, so reaching here means the job ran to its end rather than being
      // cancelled. A host that cools the nozzle mid-print would also land here.
      uiCfg.print_state   = IDLE;
      uiCfg.host_printing = false;
      stop_print_time();
      if (disp_state == PRINTING_UI && !once_flag) {
        once_flag = true;
        flash_preview_begin = false;
        default_preview_flg = false;
        clear_cur_ui();
        lv_draw_dialog(DIALOG_TYPE_FINISH_PRINT);
      }
    }
  }
  if (uiCfg.print_state == PAUSING) {
    #if ENABLED(SDSUPPORT)
      if (!planner.has_blocks_queued() && card.getIndex() > MIN_FILE_PRINTED)
        uiCfg.waitEndMoves++;

      if (uiCfg.waitEndMoves > 20) {
        uiCfg.waitEndMoves = 0;
        planner.synchronize();

        // planner.synchronize() above and every process_subcommands_now() below
        // pump idle(), which runs the LVGL task handler and dispatches touch
        // events, so Stop can land in the middle of a pause -- a window as long
        // as the planner takes to drain. Don't go on pausing a job that is no
        // longer being paused, or this overwrites the IDLE that Stop just set
        // and leaves print_state PAUSED with nothing printing.
        if (uiCfg.print_state != PAUSING) return;

        // Pause the SD print directly instead of through M25.
        //
        // With PARK_HEAD_ON_PAUSE enabled M25 calls M125, which parks the head
        // and then blocks in wait_for_confirmation() until 'wait_for_user' is
        // cleared. M125 takes that branch here because the Pause button already
        // called card.pauseSDPrint(), so IS_SD_PRINTING() is false by the time
        // M25 runs. process_subcommands_now() would therefore never return,
        // print_state would never reach PAUSED, and the Resume button (which
        // only acts on PAUSED) would be dead. The pausePos* moves below do the
        // parking instead.
        card.pauseSDPrint();
        TERN_(POWER_LOSS_RECOVERY, if (recovery.enabled) recovery.save(true));
        print_job_timer.pause();
        #if ENABLED(HOST_ACTION_COMMANDS)
          TERN_(HOST_PROMPT_SUPPORT, hostui.prompt_open(PROMPT_PAUSE_RESUME, F("Pause SD"), F("Resume")));
          #ifdef ACTION_ON_PAUSE
            hostui.pause();
          #endif
        #endif

        // save the position
        uiCfg.current_x_position_bak = current_position.x;
        uiCfg.current_y_position_bak = current_position.y;
        uiCfg.current_z_position_bak = current_position.z;

        if (gCfgItems.pausePosZ != (float)-1) {
          sprintf_P(public_buf_l, PSTR("G91\nG1 Z%s\nG90"), dtostrf(gCfgItems.pausePosZ, 1, 1, str_1));
          gcode.process_subcommands_now(public_buf_l);
        }
        if (gCfgItems.pausePosX != (float)-1 && gCfgItems.pausePosY != (float)-1) {
          sprintf_P(public_buf_l, PSTR("G1 X%s Y%s"), dtostrf(gCfgItems.pausePosX, 1, 1, str_1), dtostrf(gCfgItems.pausePosY, 1, 1, str_1));
          gcode.process_subcommands_now(public_buf_l);
        }
        if (uiCfg.print_state != PAUSING) return;   // Stop landed during the park moves

        uiCfg.print_state = PAUSED;
        uiCfg.current_e_position_bak = current_position.e;

        gCfgItems.pause_reprint = true;
        update_spi_flash();
      }
    #endif
  }
  else
    uiCfg.waitEndMoves = 0;

  if (uiCfg.print_state == PAUSED) {
  }

  if (uiCfg.print_state == RESUMING) {
    if (IS_SD_PAUSED()) {
      if (gCfgItems.pausePosX != (float)-1 && gCfgItems.pausePosY != (float)-1) {
        sprintf_P(public_buf_m, PSTR("G1 X%s Y%s"), dtostrf(uiCfg.current_x_position_bak, 1, 1, str_1), dtostrf(uiCfg.current_y_position_bak, 1, 1, str_1));
        gcode.process_subcommands_now(public_buf_m);
      }
      if (gCfgItems.pausePosZ != (float)-1) {
        ZERO(public_buf_m);
        sprintf_P(public_buf_m, PSTR("G1 Z%s"), dtostrf(uiCfg.current_z_position_bak, 1, 1, str_1));
        gcode.process_subcommands_now(public_buf_m);
      }
      gcode.process_subcommands_now(FPSTR(M24_STR));

      if (uiCfg.print_state != RESUMING) return;  // Stop landed during the unpark moves

      uiCfg.print_state = WORKING;
      start_print_time();

      gCfgItems.pause_reprint = false;
      update_spi_flash();
    }
  }
  #if ENABLED(POWER_LOSS_RECOVERY)
    if (uiCfg.print_state == REPRINTED) {
      #if HAS_HOTEND
        HOTEND_LOOP() {
          const int16_t et = recovery.info.target_temperature[e];
          if (et) {
            #if HAS_MULTI_HOTEND
              sprintf_P(public_buf_m, PSTR("T%i"), e);
              gcode.process_subcommands_now(public_buf_m);
            #endif
            sprintf_P(public_buf_m, PSTR("M109 S%i"), et);
            gcode.process_subcommands_now(public_buf_m);
          }
        }
      #endif

      // The M109 waits above block for as long as the heatup takes, and
      // recovery.resume() runs a whole script of its own, both pumping idle()
      // the entire time, so Stop can land anywhere in here.
      if (uiCfg.print_state != REPRINTED) return;

      recovery.resume();

      if (uiCfg.print_state != REPRINTED) return;

      #if 0
        // Move back to the saved XY
        char str_1[16], str_2[16];
        sprintf_P(public_buf_m, PSTR("G1 X%s Y%s F2000"),
          dtostrf(recovery.info.current_position.x, 1, 3, str_1),
          dtostrf(recovery.info.current_position.y, 1, 3, str_2)
        );
        gcode.process_subcommands_now(public_buf_m);

        if (gCfgItems.pause_reprint && gCfgItems.pausePosZ != -1.0f) {
          sprintf_P(public_buf_l, PSTR("G91\nG1 Z-%s\nG90"), dtostrf(gCfgItems.pausePosZ, 1, 1, str_2));
          gcode.process_subcommands_now(public_buf_l);
        }
      #endif
      uiCfg.print_state = WORKING;
      start_print_time();

      gCfgItems.pause_reprint = false;
      update_spi_flash();
    }
  #endif

  if (uiCfg.print_state == WORKING)
    filament_check();

  TERN_(MKS_WIFI_MODULE, wifi_looping());
}

/**
 * Pause the running print, for the Pause button, Operation > Filament change
 * and the MKS filament-detect switch.
 *
 * A media print is paused the way this UI has always done it: stop feeding the
 * file, then let the PAUSING branch above drain the planner and park the head.
 *
 * A streamed print has no file to stop feeding, so it goes through M25 instead.
 * With PARK_HEAD_ON_PAUSE that is M125, which parks the head and then blocks in
 * wait_for_confirmation(); because loop() is stuck inside that call,
 * queue.advance() stops running and the host's stream backs up behind a line
 * that is never acknowledged, which is what actually halts a streamed job. M125
 * also emits //action:paused for hosts that act on it and puts the Advanced
 * Pause dialogs on screen, and the Confirm on those dialogs clears
 * 'wait_for_user' so M125 unparks and returns on its own. Marlin owns the whole
 * sequence from there, which is why print_state is taken back from it in
 * printer_state_polling() rather than driven from here.
 */
void printer_pause_print() {
  stop_print_time();

  if (uiCfg.host_printing) {
    host_pause_seen   = false;
    uiCfg.print_state = PAUSED;
    queue.inject(F("M25"));
  }
  else {
    TERN_(SDSUPPORT, card.pauseSDPrint());
    uiCfg.print_state = PAUSING;
  }
}

/**
 * Resume, for the Resume button on the printing screen.
 *
 * A streamed print is paused by M125 waiting on 'wait_for_user', so clearing
 * that flag is the resume: M125 then unparks, restarts the job timer and tells
 * the host to carry on. This is only reachable if the pause dialog is not the
 * screen on top -- normally it is, and its own Confirm does the same thing.
 *
 * A media print hands over to the RESUMING branch above, which moves back to the
 * saved position before M24.
 */
void printer_resume_print() {
  if (uiCfg.print_state != PAUSED) return;

  if (uiCfg.host_printing) {
    TERN_(HAS_RESUME_CONTINUE, wait_for_user = false);

    // If Marlin is not actually paused then the injected M25 never ran -- the
    // queue was replaced, or M25 is not available -- and nothing is going to put
    // the screen back to WORKING, so do it here. Otherwise leave it to the sync
    // in printer_state_polling(), which waits for M125 to unpark and return.
    if (!print_job_timer.isPaused() && !TERN0(ADVANCED_PAUSE_FEATURE, did_pause_print)) {
      host_pause_seen   = false;
      uiCfg.print_state = WORKING;
      start_print_time();
    }
  }
  else
    uiCfg.print_state = RESUMING;
}

/**
 * Stop the running print, for the Stop button and the WiFi stop command.
 *
 * card.abortFilePrintSoon() only arms the deferred abort when a file happens to
 * be open -- isFileOpen() is isMounted() && file.isOpen() -- so a stop issued
 * after the media dropped, or after the job already closed its file, leaves it
 * un-armed. loop() then never runs abortSDPrinting(), which means queue.clear(),
 * quickstop_stepper(), print_job_timer.abort() and recovery.purge() never
 * happen: the screen returns to Ready while the queued moves keep executing,
 * and the next print is started on top of the old one. Set the flag directly so
 * the abort always runs. endFilePrintNow() clears it, so it fires exactly once.
 */
void printer_abort_print() {
  stop_print_time();

  // Release anything blocking the queue, or loop() never gets to the abort.
  wait_for_heatup = false;
  TERN_(HAS_RESUME_CONTINUE, wait_for_user = false);

  #if ENABLED(HOST_ACTION_COMMANDS)
    // Nothing on this side can stop a host from feeding a streamed job: clearing
    // the queue only drops the lines already received, and the next ones arrive
    // anyway. //action:cancel is the one way to ask the host to stop, so send it
    // for a streamed job -- and only for one, since a host that is merely
    // watching a media print has no job of its own to cancel.
    if (uiCfg.host_printing) {
      #ifdef ACTION_ON_CANCEL
        hostui.cancel();
      #endif
      TERN_(HOST_PROMPT_SUPPORT, hostui.prompt_open(PROMPT_INFO, F("UI Aborted"), FPSTR(DISMISS_STR)));
    }
  #endif

  uiCfg.print_state   = IDLE;
  uiCfg.host_printing = false;
  uiCfg.waitEndMoves  = 0;

  // A stop while paused must not leave the pause bookkeeping set in flash.
  if (gCfgItems.pause_reprint) {
    gCfgItems.pause_reprint = false;
    update_spi_flash();
  }

  TERN_(SDSUPPORT, card.flag.abort_sd_printing = true);
}

void filament_pin_setup() {
  #if PIN_EXISTS(MT_DET_1)
    SET_INPUT_PULLUP(MT_DET_1_PIN);
  #endif
  #if PIN_EXISTS(MT_DET_2)
    SET_INPUT_PULLUP(MT_DET_2_PIN);
  #endif
  #if PIN_EXISTS(MT_DET_3)
    SET_INPUT_PULLUP(MT_DET_3_PIN);
  #endif
}

void filament_check() {
  #if ANY_PIN(MT_DET_1, MT_DET_2, MT_DET_3)
    const int FIL_DELAY = 20;
  #endif
  #if PIN_EXISTS(MT_DET_1)
    static int fil_det_count_1 = 0;
    if (READ(MT_DET_1_PIN) == MT_DET_PIN_STATE)
      fil_det_count_1++;
    else if (fil_det_count_1 > 0)
      fil_det_count_1--;
  #endif

  #if PIN_EXISTS(MT_DET_2)
    static int fil_det_count_2 = 0;
    if (READ(MT_DET_2_PIN) == MT_DET_PIN_STATE)
      fil_det_count_2++;
    else if (fil_det_count_2 > 0)
      fil_det_count_2--;
  #endif

  #if PIN_EXISTS(MT_DET_3)
    static int fil_det_count_3 = 0;
    if (READ(MT_DET_3_PIN) == MT_DET_PIN_STATE)
      fil_det_count_3++;
    else if (fil_det_count_3 > 0)
      fil_det_count_3--;
  #endif

  if (false
    #if PIN_EXISTS(MT_DET_1)
      || fil_det_count_1 >= FIL_DELAY
    #endif
    #if PIN_EXISTS(MT_DET_2)
      || fil_det_count_2 >= FIL_DELAY
    #endif
    #if PIN_EXISTS(MT_DET_3)
      || fil_det_count_3 >= FIL_DELAY
    #endif
  ) {
    clear_cur_ui();
    printer_pause_print();

    if (gCfgItems.from_flash_pic)
      flash_preview_begin = true;
    else
      default_preview_flg = true;

    lv_draw_printing();
  }
}

#endif // HAS_TFT_LVGL_UI

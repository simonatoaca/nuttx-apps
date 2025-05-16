/****************************************************************************
 * apps/hacktorwatch/app_base/app_timer.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/boardctl.h>
#include <nshlib/nshlib.h>
#include <hacktorwatch/context.h>
#include <hacktorwatch/common.h>

#ifdef CONFIG_GRAPHICS_LVGL
#include <lvgl/lvgl.h>
#endif

#include <nuttx/timers/timer.h>
#include <nuttx/input/buttons.h>
#include <nuttx/semaphore.h>

/****************************************************************************
* Pre-processor Definitions
****************************************************************************/

#define TIMER_STOPPED (0)
#define TIMER_RUNNING (1)

#define STOPPED_BG_COLOR  (~0xC70039)
#define RUNNING_BG_COLOR  (0x50C878)

/****************************************************************************
* Private Type Declarations
****************************************************************************/

enum timer_modes_s {
  TIMER_ACTIVITY,
  TIMER_PAUSE,
  TIMER_N_MODES,
};

struct timer_data_s {
  uint8_t magic;
  uint32_t bg_color;

  uint8_t state;         /* Running/Stopped */
  uint8_t n_mode;        /* Activity/Pause */

  /* Mode-related data */

  struct {
    uint32_t nsec;
    uint32_t elapsed_nsec;

    struct {
      uint8_t sec;
      uint8_t min;
    } timestamp;
  } mode[TIMER_N_MODES];
};

/****************************************************************************
* Private Function Prototypes
****************************************************************************/

static void timer_btn_unused(const void *ctx);
static void timer_btn_up(const void *ctx);
static void timer_btn_down(const void *ctx);
static void timer_btn_ok(const void *ctx);
static void timer_display(void *ctx);

/* Helper functions */

static void update_timer(void);
static void reset_timer(void);

/* Declare wakeup sources */

WAKEUP_SOURCE(void, timer_btn_up, PM_IDLE_DOMAIN, PM_NORMAL);
WAKEUP_SOURCE(void, timer_btn_down, PM_IDLE_DOMAIN, PM_NORMAL);
WAKEUP_SOURCE(void, timer_btn_ok, PM_IDLE_DOMAIN, PM_NORMAL);

/****************************************************************************
* Private Data
****************************************************************************/

/* Internal to a task */
RTC_BSS_ATTR static struct timer_data_s timer_data;

static const struct ctx_s timer_ctx = {
  .btn_action[BUTTON_UNUSED] = timer_btn_unused,
  .btn_action[BUTTON_OK] = WAKEUP_WRAP(timer_btn_ok),
  .btn_action[BUTTON_UP] = WAKEUP_WRAP(timer_btn_up),
  .btn_action[BUTTON_DOWN] = WAKEUP_WRAP(timer_btn_down),
  .display = timer_display,
  .data = (void *)&timer_data,
};

/****************************************************************************
* Private Functions
****************************************************************************/

static void timer_btn_unused(const void *ctx)
{
  UNUSED(ctx);
}

static void timer_btn_up(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  /* Toggle timer state, default is STOPPED */
  timer_data.state ^= 1;
}

static void timer_btn_down(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  reset_timer();
}

static void timer_btn_ok(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  rewind_ctx();
}

static void timer_display(void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  struct timer_data_s const *data = (struct timer_data_s *)g_data_ptr->ctx->data;
  update_timer();

#ifdef CONFIG_GRAPHICS_LVGL
  lv_color_t current_color = lv_obj_get_style_bg_color(lv_screen_active(), LV_PART_MAIN);
  lv_color_t wanted_color = lv_color_hex(data->bg_color);

  /* Execute only on update */
  lv_label_set_text_fmt(g_data_ptr->label, "%02d:%02d", data->mode[data->n_mode].timestamp.min,
                                                        data->mode[data->n_mode].timestamp.sec);
  if (data->n_mode == TIMER_ACTIVITY)
    lv_label_set_text_fmt(g_data_ptr->time_label, "Activity\n");
  else
    lv_label_set_text_fmt(g_data_ptr->time_label, "Pause\n");

  if (wanted_color.red != current_color.red ||
      wanted_color.green != current_color.green ||
      wanted_color.blue != current_color.blue) {
    lv_obj_set_style_bg_color(lv_screen_active(), wanted_color, LV_PART_MAIN);
  }
#else
  UNUSED(g_data_ptr);
#endif
}

static void init_local_ctx(void)
{
  int8_t i = 0;

  if (timer_data.magic == TIMER_MAGIC_NUM)
    {
      return;
    }

  timer_data.magic = TIMER_MAGIC_NUM;
  timer_data.n_mode = TIMER_ACTIVITY;
  timer_data.state = TIMER_STOPPED;

  for (i = 0; i < TIMER_N_MODES; i++) {
    timer_data.mode[i].nsec = 0;
    timer_data.mode[i].elapsed_nsec = 0;
    timer_data.mode[i].timestamp.min = 0;
    timer_data.mode[i].timestamp.sec = 0;
  }
}

static void set_timer_duration(uint8_t mode, uint64_t nsec, uint64_t nmin)
{
  timer_data.mode[mode].nsec = nsec + 60 * nmin;
  timer_data.mode[mode].timestamp.min = timer_data.mode[mode].nsec / 60;
  timer_data.mode[mode].timestamp.sec = timer_data.mode[mode].nsec - (60 * timer_data.mode[mode].timestamp.min);
}

/**
 *  !! Called only from the timer_display() method,
 *  because the timer is incremented based on the
 *  sys_timer.
 */
static void update_timer(void)
{
  int64_t remaining_nsec;
  int64_t remaining_nmin;
  uint8_t state = timer_data.state;
  uint8_t curr_mode = timer_data.n_mode;

  timer_data.bg_color = (state == TIMER_STOPPED) ? STOPPED_BG_COLOR : RUNNING_BG_COLOR;

  if (state == TIMER_STOPPED) {
    return;
  }

  /**
   *  The elapsed_nsec is incremented every 1 second,
   *  because the sys_timer sends a signal_ctx_update()
   *  every 1s and update_timer() is called in the display()
   *  method.
   */
  timer_data.mode[curr_mode].elapsed_nsec++;
  remaining_nsec = timer_data.mode[curr_mode].nsec - timer_data.mode[curr_mode].elapsed_nsec;

  remaining_nmin = remaining_nsec / 60;
  remaining_nsec -= remaining_nmin * 60;

  if (remaining_nmin < 0) {
    remaining_nmin = 0;
  }

  if (remaining_nsec < 0) {
    remaining_nsec = 0;
  }

  /* If the running/pause timer expired, check if the other can be run */
  if (remaining_nmin == 0 && remaining_nsec == 0) {
    curr_mode ^= 1;

    if (timer_data.mode[curr_mode].nsec == 0) {
      curr_mode ^= 1;
      timer_data.state = TIMER_STOPPED;
    }
  }

  timer_data.mode[curr_mode].timestamp.sec = remaining_nsec;
  timer_data.mode[curr_mode].timestamp.min = remaining_nmin;
  timer_data.n_mode = curr_mode;
}

static void reset_timer(void)
{
  uint8_t curr_mode = timer_data.n_mode;

  timer_data.state = TIMER_STOPPED;
  timer_data.mode[curr_mode].elapsed_nsec = 0;
  timer_data.mode[curr_mode].timestamp.min = timer_data.mode[curr_mode].nsec / 60;
  timer_data.mode[curr_mode].timestamp.sec = timer_data.mode[curr_mode].nsec - (60 * timer_data.mode[curr_mode].timestamp.min);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void start_timer(void)
{
  timer_data.state = TIMER_RUNNING;
}

void stop_timer(void)
{
  timer_data.state = TIMER_STOPPED;
}

void set_activity_timer_duration(uint64_t nsec, uint64_t nmin)
{
  set_timer_duration(TIMER_ACTIVITY, nsec, nmin);
}

void set_pause_timer_duration(uint64_t nsec, uint64_t nmin)
{
  set_timer_duration(TIMER_PAUSE, nsec, nmin);
}

int app_timer(int argc, char *argv[])
{
  /* Important for init */
  set_task_ctx(&timer_ctx, TIMER_ID);

  init_local_ctx();

  sem_t waiter;
  sem_init(&waiter, 0, 0);

  /* Wait infinity */
  return sem_wait(&waiter);
}
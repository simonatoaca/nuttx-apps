/****************************************************************************
 * apps/hacktorwatch/app_base/menu.c
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

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct menu_data_s {
  uint8_t magic;
  uint32_t bg_color;
  int btn_value;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void menu_btn_unused(const void *ctx);
static void menu_btn_up(const void *ctx);
static void menu_btn_down(const void *ctx);
static void menu_btn_ok(const void *ctx);
static void menu_display(void *ctx);

/* Declare wakeup sources */

WAKEUP_SOURCE(void, menu_btn_up, PM_IDLE_DOMAIN, PM_NORMAL);
WAKEUP_SOURCE(void, menu_btn_down, PM_IDLE_DOMAIN, PM_NORMAL);
WAKEUP_SOURCE(void, menu_btn_ok, PM_IDLE_DOMAIN, PM_NORMAL);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Internal to a task */
RTC_BSS_ATTR static struct menu_data_s menu_data;

static const struct ctx_s menu_ctx = {
  .btn_action[BUTTON_UNUSED] = menu_btn_unused,
  .btn_action[BUTTON_OK] = WAKEUP_WRAP(menu_btn_ok),
  .btn_action[BUTTON_UP] = WAKEUP_WRAP(menu_btn_up),
  .btn_action[BUTTON_DOWN] = WAKEUP_WRAP(menu_btn_down),
  .display = menu_display,
  .data = (void *)&menu_data,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void menu_btn_unused(const void *ctx)
{
  UNUSED(ctx);
}

static void menu_btn_up(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  menu_data.btn_value++;
  ble_svc_steps_cnt_set(menu_data.btn_value);
  signal_ctx_update();
}

static void menu_btn_down(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  menu_data.btn_value--;
  ble_svc_steps_cnt_set(menu_data.btn_value);
  signal_ctx_update();
}

static void menu_btn_ok(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  rewind_ctx();
}

static void menu_display(void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  struct menu_data_s const *data = (struct menu_data_s *)g_data_ptr->ctx->data;

#ifdef CONFIG_GRAPHICS_LVGL
  lv_color_t current_color = lv_obj_get_style_bg_color(lv_screen_active(), LV_PART_MAIN);
  lv_color_t wanted_color = lv_color_hex(data->bg_color);

  /* Execute only on update */
  lv_label_set_text(g_data_ptr->time_label, "");
  lv_label_set_text_fmt(g_data_ptr->label, "Menu: %d", data->btn_value);

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
  if (menu_data.magic == MENU_MAGIC_NUM)
    {
      return;
    }

  menu_data.magic = MENU_MAGIC_NUM;
  menu_data.bg_color = ~0x003a57;
  menu_data.btn_value = 0;
}

int menu(int argc, char *argv[])
{
  /* Important for init */
  set_task_ctx(&menu_ctx, MENU_ID);

  init_local_ctx();

  sem_t waiter;
  sem_init(&waiter, 0, 0);

  /* Wait infinity */
  return sem_wait(&waiter);
}
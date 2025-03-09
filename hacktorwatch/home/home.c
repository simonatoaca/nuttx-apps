/****************************************************************************
 * apps/hacktorwatch/home/home.c
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

#include <lvgl/lvgl.h>
#include <nuttx/timers/timer.h>
#include <nuttx/input/buttons.h>
#include <nuttx/semaphore.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct home_data_s {
  uint32_t bg_color;
  int btn_value;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void home_btn_unused(void *ctx);
static void home_btn_up(void *ctx);
static void home_btn_down(void *ctx);
static void home_btn_ok(void *ctx);
static void home_display(void *ctx);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Internal to a task */
static struct home_data_s home_data = {
  .bg_color = 0x003a57,
  .btn_value = 0,
};

static const struct ctx_s home_ctx = {
  .btn_action[BUTTON_UNUSED] = home_btn_unused,
  .btn_action[BUTTON_OK] = home_btn_ok,
  .btn_action[BUTTON_UP] = home_btn_up,
  .btn_action[BUTTON_DOWN] = home_btn_down,
  .display = home_display,
  .data = (void *)&home_data,
  .data_size = sizeof(home_data)
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void home_btn_unused(void *ctx)
{
  UNUSED(ctx);
}

static void home_btn_up(void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  home_data.btn_value++;
  signal_ctx_update();
}

static void home_btn_down(void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  home_data.btn_value--;
  signal_ctx_update();
}

static void home_btn_ok(void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();

  set_ctx(g_data_ptr->tasks[MENU_ID].ctx);
  signal_ctx_update();
}

static void home_display(void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();

  /* Execute only on update */
  lv_label_set_text_fmt(g_data_ptr->label, "Home: %d", ((struct home_data_s *)g_data_ptr->ctx->data)->btn_value);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(((struct home_data_s *)g_data_ptr->ctx->data)->bg_color), LV_PART_MAIN);
}

int home(int argc, char *argv[])
{
  set_task_ctx(&home_ctx, HOME_ID);

  sem_t waiter;
  sem_init(&waiter, 0, 0);

  /* Wait infinity */
  return sem_wait(&waiter);
}

int main(int argc, char *argv[])
{
  return 0;
}
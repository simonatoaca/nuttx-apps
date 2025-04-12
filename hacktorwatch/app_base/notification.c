/****************************************************************************
 * apps/hacktorwatch/app_base/notification.c
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

struct notif_data_s {
  uint32_t bg_color;
  uint32_t text_color;
  char *notification;
};

/****************************************************************************
* Private Function Prototypes
****************************************************************************/

static void notif_btn_unused(const void *ctx);
static void notif_btn_up(const void *ctx);
static void notif_btn_down(const void *ctx);
static void notif_btn_ok(const void *ctx);
static void notif_display(void *ctx);

/****************************************************************************
* Private Data
****************************************************************************/

/* Internal to a task */
static struct notif_data_s notif_data = {
  .bg_color = 0x0,
  .text_color = 0xff,
  .notification = "None"
};

static const struct ctx_s notif_ctx = {
  .btn_action[BUTTON_UNUSED] = notif_btn_unused,
  .btn_action[BUTTON_OK] = notif_btn_ok,
  .btn_action[BUTTON_UP] = notif_btn_unused,
  .btn_action[BUTTON_DOWN] = notif_btn_unused,
  .display = notif_display,
  .data = (void *)&notif_data,
};

/****************************************************************************
* Private Functions
****************************************************************************/

static void notif_btn_unused(const void *ctx)
{
  UNUSED(ctx);
}

static void notif_btn_up(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);
}

static void notif_btn_down(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);
}

static void notif_btn_ok(const void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();
  UNUSED(g_data_ptr);

  rewind_ctx();
  signal_ctx_update();
}

static void notif_display(void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();

#ifdef CONFIG_GRAPHICS_LVGL
  // lv_lock();
  lv_color_t current_color = lv_obj_get_style_bg_color(lv_screen_active(), LV_PART_MAIN);
  lv_color_t wanted_color = lv_color_hex(((struct notif_data_s *)g_data_ptr->ctx->data)->bg_color);

  /* Execute only on update */
  lv_label_set_text_fmt(g_data_ptr->label, "%s\n", ((struct notif_data_s *)g_data_ptr->ctx->data)->notification);

  if (wanted_color.red != current_color.red ||
      wanted_color.green != current_color.green ||
      wanted_color.blue != current_color.blue) {
    lv_obj_set_style_bg_color(lv_screen_active(), wanted_color, LV_PART_MAIN);
  }
  // lv_unlock();
#else
  UNUSED(g_data_ptr);
#endif
}

/****************************************************************************
* Public Functions
****************************************************************************/

void set_notification(char *notification)
{
  notif_data.notification = notification;
}

int notif(int argc, char *argv[])
{
  mqd_t mq;
  struct mq_attr attr;
  int ret;
  char *notification;
  struct data_s const *g_data_ptr = get_g_data();

  /* Important for init */
  set_task_ctx(&notif_ctx, NOTIF_ID);

  attr.mq_maxmsg  = 5;
  attr.mq_msgsize = MAX_NOTIFICATION_LEN;
  attr.mq_flags   = 0;

  mq = mq_open(NOTIF_MQ_NAME, O_CREAT | O_RDONLY, 0666, &attr);

  if (mq < 0) {
    return EXIT_FAILURE;
  }

  while(1) {
    ret = mq_receive(mq, notif_data.notification, MAX_NOTIFICATION_LEN, NULL);

    if (ret < 0) {
      continue;
    }

    set_ctx(g_data_ptr->tasks[NOTIF_ID].ctx);
    signal_ctx_update();
    trigger_haptic(2);
  }
}
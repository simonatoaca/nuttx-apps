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

#define NOTIF_MAX_BUF_LEN   (256)
#define NOTIF_HAPTIC_EFFECT (3)

/****************************************************************************
* Private Type Declarations
****************************************************************************/

struct notif_data_s {
  uint8_t magic;
  uint32_t bg_color;
  uint32_t text_color;
  char notification[NOTIF_MAX_BUF_LEN];
};

struct notif_ops_s {
  CODE int (*receive)(const void *ctx);
};

enum alert_op_codes {
  TIMER_OP_START = 10,
  TIMER_SET = TIMER_OP_START,
  TIMER_START,
  TIMER_STOP,
  TIMER_RESET,
  TIMER_OP_END,
  STEP_DATA_START = TIMER_OP_END,
  STEP_DATA_RECV = STEP_DATA_START,
  STEP_DATA_END
};

typedef int (*parse_fn)(char *notif, int len);
typedef int (*alert_op)(uint8_t *data, int len);

/****************************************************************************
* Private Function Prototypes
****************************************************************************/

static void notif_btn_unused(const void *ctx);
static void notif_btn_up(const void *ctx);
static void notif_btn_down(const void *ctx);
static void notif_btn_ok(const void *ctx);
static void notif_display(void *ctx);

/* This signature's args must match the btn_behaviour so that
    it is compatible with WAKEUP_SOURCE() */
static int receive_notif(const void *ctx);

/* Parser functions */

static int parse_normal(char *notif, int len);
static int parse_als(char *notif, int len);
static int parse_curr_time(char *notif, int len);

/* Timer ops */

static int timer_set(uint8_t *data, int len);
static int timer_start(uint8_t *data, int len);
static int timer_stop(uint8_t *data, int len);
static int timer_reset(uint8_t *data, int len);

static int step_data_recv(uint8_t *data, int len);

/* Declare wakeup sources */

WAKEUP_SOURCE(void, notif_btn_ok, PM_IDLE_DOMAIN, PM_NORMAL);
WAKEUP_SOURCE(void, notif_btn_up, PM_IDLE_DOMAIN, PM_NORMAL);
WAKEUP_SOURCE(void, notif_btn_down, PM_IDLE_DOMAIN, PM_NORMAL);
WAKEUP_SOURCE(int, receive_notif, PM_IDLE_DOMAIN, PM_NORMAL);

/****************************************************************************
* Private Data
****************************************************************************/

static const char *alert_types[] = {
  "Simple Alert",
  "Email",
  "News",
  "Call",
  "Missed Call",
  "SMS/MMS",
  "Voice Mail",
  "Schedule",
  "High Prio Alert",
  "Instant Msg"
};

static struct notif_ops_s notif_ops = {
  .receive = WAKEUP_WRAP(receive_notif),
};

/* Internal to a task */
RTC_BSS_ATTR static struct notif_data_s notif_data;

static const struct ctx_s notif_ctx = {
  .btn_action[BUTTON_UNUSED] = notif_btn_unused,
  .btn_action[BUTTON_OK]     = WAKEUP_WRAP(notif_btn_ok),
  .btn_action[BUTTON_UP]     = WAKEUP_WRAP(notif_btn_up),
  .btn_action[BUTTON_DOWN]   = WAKEUP_WRAP(notif_btn_down),
  .display                   = notif_display,
  .data                      = (void *)&notif_data,
};

static const parse_fn parsers[] = {
  [NOTIF_NORMAL] = parse_normal,
  [NOTIF_ALERT]  = parse_als,
  [NOTIF_TIME]   = parse_curr_time,
};

static const alert_op alert_ops[] = {
  [TIMER_SET]   = timer_set,
  [TIMER_START] = timer_start,
  [TIMER_STOP]  = timer_stop,
  [TIMER_RESET] = timer_reset,
  [STEP_DATA_RECV] = step_data_recv,
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
}

static int parse_normal(char *notif, int len)
{
  sprintf(notif_data.notification, "%s", notif);

  return 0;
}

/**
 * Parse an Alert Service Notification
 * and populate notification field.
 *
 * ---------------------------------
 * |  1   |  1  |    len - 1     |
 * | type | new |  alert message |
 * ---------------------------------
 *
 * @return Type of Notification
 */
static int parse_als(char *notif, int len)
{
  int ret = 0;
  uint8_t type = *(uint8_t *)(notif);
  uint8_t new_alerts = *(uint8_t *)(notif + 1);
  char *message = (char *)(notif + 2);

  if (type < (sizeof(alert_types) / sizeof(alert_types[0])))
    {
      ret += sprintf(notif_data.notification, "%s\n", alert_types[type]);
    }
  else
    {
      /**
       *  Type is custom => indicates some other type of event
       *  For the mobile-watch communication, the ALS is also used
       *  to set/start/stop/reset a timer.
       */
      if (TIMER_OP_START <= type && type < TIMER_OP_END)
        {
          alert_ops[type]((uint8_t *)message, len);
          /* Return negative int so the notification is silent */
          return -1;
        }

      if (STEP_DATA_START <= type && type < STEP_DATA_END)
        {
          alert_ops[type]((uint8_t *)message, len);
          return -1;
        }

      /* For DEBUG, TODO: ERASE THIS */
      ret += sprintf(notif_data.notification, "%s\n%d", "Unknown\nAlert Type", type);

      return 1;
    }

  if (new_alerts)
    {
      ret += sprintf(notif_data.notification + ret, "#New: %d\n", new_alerts);
    }

  ret += snprintf(notif_data.notification + ret, len - 1, "%s", message);

  return type;
}

static int parse_curr_time(char *notif, int len)
{
  struct curr_time_t {
    struct {
        uint16_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hours;
        uint8_t minutes;
        uint8_t seconds;
    } __attribute__((packed)) tstmp;

    uint8_t day_of_week;
    uint8_t fractions256;
    uint8_t adjust_reason;
  } __attribute__ ((packed));

  struct curr_time_t *curr_time = (struct curr_time_t *)notif;

  set_g_data_time(curr_time);

  /* Return negative int so the notification is silent */
  return -1;
}

static int receive_notif(const void *ctx)
{
  mqd_t *mq = (mqd_t *)ctx;
  char notif[MAX_NOTIFICATION_LEN];
  unsigned int msg_prio = 0;
  int ret = 0;

  ret = mq_receive(*mq, notif, MAX_NOTIFICATION_LEN, &msg_prio);

  if (ret < 0)
    return ret;

  /**
   *  The message priority selects the type of parser used
   *  (So we can differentiate between types of messages)
   */
  ret = parsers[msg_prio](notif, ret);

  return ret;
}

static void notif_display(void *ctx)
{
  struct data_s const *g_data_ptr = get_g_data();

#ifdef CONFIG_GRAPHICS_LVGL
  lv_color_t current_color = lv_obj_get_style_bg_color(lv_screen_active(), LV_PART_MAIN);
  lv_color_t wanted_color = lv_color_hex(((struct notif_data_s *)g_data_ptr->ctx->data)->bg_color);

  /* Execute only on update */
  lv_label_set_text(g_data_ptr->time_label, "");
  lv_label_set_text_fmt(g_data_ptr->label, "%s\n", ((struct notif_data_s *)g_data_ptr->ctx->data)->notification);

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
  if (notif_data.magic == NOTIF_MAGIC_NUM)
    {
      return;
    }

  notif_data.magic = NOTIF_MAGIC_NUM;
  notif_data.bg_color = 0x0;
  notif_data.text_color = 0xff;
  sprintf(notif_data.notification, "None");
}

/**
 *  Data expected to be received:
 *
 *  activity_nmin (1 byte)
 *  pause_nmin    (1 byte)
 */
static int timer_set(uint8_t *data, int len)
{
  struct data_s const *g_data_ptr = get_g_data();

  set_activity_timer_duration(0, data[0]);
  set_pause_timer_duration(0, data[1]);

  set_ctx(g_data_ptr->tasks[TIMER_ID].ctx);

  return OK;
}

static int timer_start(uint8_t *data, int len)
{
  start_timer();

  return OK;
}

static int timer_stop(uint8_t *data, int len)
{
  stop_timer();

  return OK;
}

static int timer_reset(uint8_t *data, int len)
{
  reset_timer();

  return OK;
}

static int step_data_recv(uint8_t *data, int len)
{
  /**
   * TODO Ana: Parsare date din @data, similar cu timer-ul,
   * depinde cum te intelegi cu Miruna sa transmiteti datele.
   * In g_data_ptr poti stoca varsta, intaltimea, greutatea, etc.
   * Foloseste set_health_data(int16_t steps, uint8_t age, uint8_t height, uin8_t weight).
   */
  set_health_data(0, 0, 0, 0);

  return OK;
}

/****************************************************************************
* Public Functions
****************************************************************************/

int notif(int argc, char *argv[])
{
  mqd_t mq;
  struct mq_attr attr;
  int ret;
  struct data_s const *g_data_ptr = get_g_data();

  /* Important for init */
  set_task_ctx(&notif_ctx, NOTIF_ID);

  init_local_ctx();

  attr.mq_maxmsg  = 5;
  attr.mq_msgsize = MAX_NOTIFICATION_LEN;
  attr.mq_flags   = 0;

  mq = mq_open(NOTIF_MQ_NAME, O_CREAT | O_RDONLY, 0666, &attr);

  if (mq < 0) {
    return EXIT_FAILURE;
  }

  while(1) {
    ret = notif_ops.receive(&mq);

    if (ret < 0) {
      continue;
    }

    set_ctx(g_data_ptr->tasks[NOTIF_ID].ctx);
    trigger_haptic(NOTIF_HAPTIC_EFFECT);
  }

  /* Should never get here */
  return EXIT_FAILURE;
}
/****************************************************************************
 * apps/hacktorwatch/app_base.c
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
#include <netutils/netinit.h>
#include <hacktorwatch/context.h>
#include <hacktorwatch/common.h>
#include "utils/stack.h"

#ifdef CONFIG_GRAPHICS_LVGL
#include <lvgl/lvgl.h>
#endif

#include <nuttx/timers/timer.h>
#include <nuttx/timers/watchdog.h>
#include <nuttx/input/buttons.h>
#include <nuttx/semaphore.h>
#include <nuttx/mqueue.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WDOG_DEVNAME "/dev/watchdog0"
#define WDOG_TIMEOUT (2000)
#define BLE_DEVNAME  "bnep0"

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct wdog_data_s {
  int fd;
  char *devname;
  uint32_t timeout;
  struct watchdog_capture_s capture;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int haptic_mq_init(void);

#ifdef CONFIG_PM
static int wdog_capture(int irq, FAR void *context, FAR void *arg);
static int wdog_init(void);
#endif /* CONFIG_PM */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void *get_ctx_data(struct data_s *data);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct data_s g_data = {0};

#ifdef CONFIG_PM
static struct wdog_data_s g_wdog = {
  .fd = -1,
  .devname = WDOG_DEVNAME,
  .timeout = WDOG_TIMEOUT, /* in ms */
  .capture = {
    .newhandler = wdog_capture,
  }
};
#endif /* CONFIG_PM */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int init(void)
{
  int ret = 0;

  sem_init(&g_data.ctx_update, 1, 0);
  sem_init(&g_data.ctx_mutex, 1, 1);
  sem_init(&g_data.tasks_register, 1, -(NUM_TASKS - 1));

  ret = haptic_mq_init();

  if (ret) {
    return ret;
  }

#ifdef CONFIG_PM
  ret = wdog_init();

  if (ret) {
    return ret;
  }
#endif /* CONFIG_PM */

  /* Register and create tasks that also represent displays */

  register_task("home_task", home, HOME_ID);
  register_task("menu_task", menu, MENU_ID);
  register_task("notif_task", notif, NOTIF_ID);

  for (int i = 0; i < NUM_TASKS; i++) {
    ret = task_create(g_data.tasks[i].name, 120, 4096,
                      g_data.tasks[i].entry, NULL);
    if (ret < 0) {
      int errcode = errno;
      printf("Failed to start %s %d\n", g_data.tasks[i].name,
            errcode);
      return EXIT_FAILURE;
    }
  }

#ifdef CONFIG_NIMBLE
  /* Enable the ble network interface */
  netlib_ifup(BLE_DEVNAME);
#endif

  sem_wait(&g_data.tasks_register);

  /* Home is the default screen */
  g_data.ctx = g_data.tasks[HOME_ID].ctx;

  CTX_STACK_HEAD(g_data.ctx_stack, g_data.ctx);

  signal_ctx_update();

  return OK;
}

static int haptic_mq_init(void)
{
  struct mq_attr attr;

  attr.mq_maxmsg  = 20;
  attr.mq_msgsize = sizeof(char);
  attr.mq_flags   = 0;

  g_data.haptic_mq = mq_open(HAPTIC_MQ_NAME, O_CREAT | O_WRONLY, 0666, &attr);

  if (g_data.haptic_mq < 0) {
    return EXIT_FAILURE;
  }

  return OK;
}

#ifdef CONFIG_PM
static int wdog_init(void)
{
  int ret = 0;

  g_wdog.fd = open(g_wdog.devname, O_RDONLY);

  if (g_wdog.fd < 0) {
    return EXIT_FAILURE;
  }

  ret = ioctl(g_wdog.fd, WDIOC_SETTIMEOUT, (unsigned long)g_wdog.timeout);

  if (ret < 0) {
    return EXIT_FAILURE;
  }

  ret = ioctl(g_wdog.fd, WDIOC_CAPTURE, (unsigned long)&g_wdog.capture);

  if (ret < 0) {
    return EXIT_FAILURE;
  }

  return OK;
}

static int wdog_capture(int irq, FAR void *context, FAR void *arg)
{
  /* Enter Idle */

  relax_once(PM_IDLE_DOMAIN, PM_NORMAL);

  /**
   *  Stop wdog (we don't know how long the idle will last,
   *  why let it running?)
   */
  ioctl(g_wdog.fd, WDIOC_STOP, 0);

  return OK;
}

static int start_wdog(void)
{
  return ioctl(g_wdog.fd, WDIOC_START, 0);
}

static int ping_wdog(void)
{
  int ret;
  struct watchdog_status_s status;

  ret = ioctl(g_wdog.fd, WDIOC_GETSTATUS, &status);

  if (!ret && !(status.flags & WDFLAGS_ACTIVE)) {
    return start_wdog();
  }

  return ioctl(g_wdog.fd, WDIOC_KEEPALIVE, 0);
}
#endif /* CONFIG_PM */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int set_cpu_affinity(uint32_t core_id)
{
  int ret;
  cpu_set_t cpuset;

  sched_lock();
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  ret = sched_setaffinity(getpid(), sizeof(cpuset), &cpuset);
  sched_unlock();

  if (ret)
  {
      printf("Failed to set affinity error=%d\n", ret);
      return -1;
  }

  return OK;
}

void *get_ctx_data(struct data_s *data)
{
  return data->ctx->data;
}

/* Get a read-only copy of g_data */
struct data_s const *get_g_data(void)
{
  return &g_data;
}

/**
 * Signals to the app_base task that a context update
 * has occured (either the ctx changed, either a field
 * was updated) -> the display() function specific
 * to the new ctx is called -> the display is updated.
 *
 * Called automatically from set_ctx() and rewind_ctx().
 */
void signal_ctx_update(void)
{
  sem_post(&g_data.ctx_update);
}

/**
 *  Change the current ctx with a new one and
 *  signal the change to the app_base task.
 */
void set_ctx(const struct ctx_s *ctx)
{
  sem_wait(&g_data.ctx_mutex);

  if (!ctx) {
    sem_post(&g_data.ctx_mutex);
    return;
  }

  /* Save ctx */
  if (g_data.ctx != ctx) {
    CTX_PUSH(g_data.ctx_stack, g_data.ctx);
  }

  /* Load new ctx */
  g_data.ctx = ctx;

  signal_ctx_update();
  sem_post(&g_data.ctx_mutex);
}

/**
 *  Return to the previous ctx and
 *  signal the change to the app_base task.
 */
void rewind_ctx(void)
{
  sem_wait(&g_data.ctx_mutex);
  CTX_POP(g_data.ctx_stack, g_data.ctx);

  signal_ctx_update();
  sem_post(&g_data.ctx_mutex);
}

/**
 * Waits for a signal_ctx_update().
 */
static void wait_ctx_update(void)
{
  sem_wait(&g_data.ctx_update);
}

void register_task(char *name, main_t entry, uint8_t id)
{
  g_data.tasks[id].name = name;
  g_data.tasks[id].entry = entry;
}

void set_task_ctx(const struct ctx_s *ctx, uint8_t id)
{
  g_data.tasks[id].ctx = ctx;

  sem_post(&g_data.tasks_register);
}

void trigger_haptic(int8_t effect_id)
{
  mq_send(g_data.haptic_mq, (char *)&effect_id, sizeof(effect_id), 0);
}

int get_staycount(int domain, int state)
{
#ifdef CONFIG_PM
  struct boardioc_pm_ctrl_s pm_ctrl = {
    .domain = domain,
    .action = BOARDIOC_PM_STAYCOUNT,
    .state = state
  };

  boardctl(BOARDIOC_PM_CONTROL, (uintptr_t)&pm_ctrl);

  return pm_ctrl.count;
#else
  return 0;
#endif /* CONFIG_PM */
}

void relax(int domain, int state)
{
#ifdef CONFIG_PM
  struct boardioc_pm_ctrl_s pm_ctrl = {
    .domain = domain,
    .action = BOARDIOC_PM_RELAX,
    .state = state
  };

  /* Signal Idle can start */

  boardctl(BOARDIOC_PM_CONTROL, (uintptr_t)&pm_ctrl);
#endif /* CONFIG_PM */
}

void relax_once(int domain, int state)
{
  if (get_staycount(domain, state) == 1)
    {
      relax(domain, state);
    }
}

void stay(int domain, int state)
{
#ifdef CONFIG_PM
  struct boardioc_pm_ctrl_s pm_ctrl = {
    .domain = domain,
    .action = BOARDIOC_PM_STAY,
    .state = state
  };

  /* Signal Activity */

  boardctl(BOARDIOC_PM_CONTROL, (uintptr_t)&pm_ctrl);
#endif /* CONFIG_PM */
}

void stay_once(int domain, int state)
{
  if (get_staycount(domain, state) == 0)
    {
      stay(domain, state);
    }
}

int main(int argc, FAR char *argv[])
{
  int ret;
#ifdef CONFIG_GRAPHICS_LVGL
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
#endif

  struct sched_param param;

  /* Check the task priority that we were started with */

  sched_getparam(getpid(), &param);
  if (param.sched_priority != 120)
    {
      /* If not then set the priority to the configured priority */

      param.sched_priority = 120;
      sched_setparam(getpid(), &param);
    }

  /* Initialize the NSH library -> if app_base is the entry point */

  if (&main == CONFIG_INIT_ENTRYPOINT) {
    nsh_initialize();
  }

  ret = init();

  if (ret < 0) {
    return EXIT_FAILURE;
  }

#ifdef CONFIG_GRAPHICS_LVGL
  lv_init();
  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

  lv_nuttx_init(&info, &result);

  if (result.disp == NULL) {
    LV_LOG_ERROR("lv_demos initialization failure!");
    return 1;
  }
#endif
  /* Create a separate task for handling haptic events */
  ret = task_create("haptic_task", 120, 4096, haptic, NULL);

  if (ret < 0) {
    int errcode = errno;
    printf("main: ERROR: Failed to start haptic: %d\n",
    errcode);
    return EXIT_FAILURE;
  }

  /* Create a separate task for handling button events */
  ret = task_create("button_task", 100, 4096, button_handler,
                    NULL);
  if (ret < 0) {
    int errcode = errno;
    printf("main: ERROR: Failed to start button_task: %d\n",
           errcode);
    return EXIT_FAILURE;
  }


#ifdef CONFIG_GRAPHICS_LVGL
  /* Change the active screen's background color */
  // lv_lock();
  g_data.screen = lv_obj_create(NULL);
  lv_scr_load(g_data.screen);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);

  /* Create a white label, set its text and align it to the center */

  g_data.label = lv_label_create(lv_screen_active());
  lv_label_set_text(g_data.label, "");
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_align(g_data.label, LV_ALIGN_CENTER, -20, 0);
  // lv_unlock();
#endif

#ifdef CONFIG_NIMBLE
  nimble(0, NULL);
#endif /* CONFIG_NIMBLE */

#ifdef CONFIG_PM
  start_wdog();
#endif /* CONFIG_PM */

  while (1) {
    wait_ctx_update();

    /* Execute only on update */
    g_data.ctx->display(&g_data);

#ifdef CONFIG_GRAPHICS_LVGL
    /* Gateway thread: Called from same thread that manages lv objects
     * -> avoid race conditions as LVGL is not SMP-compatible by design
     */
    lv_timer_handler();
#endif /* CONFIG_GRAPHICS_LVGL */

#ifdef CONFIG_PM
    ping_wdog();
#endif /* CONFIG_PM */
  }

#ifdef CONFIG_GRAPHICS_LVGL
  lv_disp_remove(result.disp);
  lv_deinit();
#endif

  return 0;
}
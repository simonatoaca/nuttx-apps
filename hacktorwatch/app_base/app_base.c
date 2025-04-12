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

#ifdef CONFIG_GRAPHICS_LVGL
#include <lvgl/lvgl.h>
#endif

#include <nuttx/timers/timer.h>
#include <nuttx/input/buttons.h>
#include <nuttx/semaphore.h>
#include <nuttx/mqueue.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/
void *get_ctx_data(struct data_s *data);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct data_s g_data = {0};

static int init(void)
{
  int ret = 0;
  struct mq_attr attr;

  attr.mq_maxmsg  = 20;
  attr.mq_msgsize = sizeof(char);
  attr.mq_flags   = 0;

  sem_init(&g_data.ctx_update, 1, 0);
  sem_init(&g_data.ctx_mutex, 1, 1);
  sem_init(&g_data.tasks_register, 1, -(NUM_TASKS - 1));

  g_data.haptic_mq = mq_open(HAPTIC_MQ_NAME, O_CREAT | O_WRONLY, 0666, &attr);

  if (g_data.haptic_mq < 0) {
    return EXIT_FAILURE;
  }

  register_task("home_task", home, HOME_ID);
  register_task("menu_task", menu, MENU_ID);
  register_task("notif_task", notif, NOTIF_ID);

  for (int i = 0; i < NUM_TASKS; i++) {
    ret = task_create(g_data.tasks[i].name, 100, 4096,
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
  netlib_ifup("bnep0");
#endif

  sem_wait(&g_data.tasks_register);

  /* Home is the default screen */
  g_data.ctx = g_data.tasks[HOME_ID].ctx;

  signal_ctx_update();

  return OK;
}

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

void set_ctx(struct ctx_s *ctx)
{
  sem_wait(&g_data.ctx_mutex);

  if (!ctx) {
    sem_post(&g_data.ctx_mutex);
    return;
  }

  /* Save ctx */
  g_data.ctx_stack = g_data.ctx;

  /* Load new ctx */
  g_data.ctx = ctx;
  sem_post(&g_data.ctx_mutex);
}

void rewind_ctx(void)
{
  set_ctx(g_data.ctx_stack);
}

void signal_ctx_update(void)
{
  sem_post(&g_data.ctx_update);
}

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

void trigger_haptic(uint8_t effect_id)
{
  mq_send(g_data.haptic_mq, &effect_id, sizeof(effect_id), 0);
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
  if (param.sched_priority != CONFIG_SYSTEM_NSH_PRIORITY)
    {
      /* If not then set the priority to the configured priority */

      param.sched_priority = CONFIG_SYSTEM_NSH_PRIORITY;
      sched_setparam(getpid(), &param);
    }

  // set_cpu_affinity(0);

  /* Initialize the NSH library */

  nsh_initialize();

#ifndef CONFIG_HACKTORWATCH_DISABLE_CONSOLE
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  attr.priority  = CONFIG_INIT_PRIORITY;
  attr.stacksize = CONFIG_INIT_STACKSIZE;

  ret = task_spawn("nsh_consolemain",
                   nsh_consolemain,
                   NULL, &attr, NULL, NULL);
#endif

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
  ret = task_create("haptic_task", 110, 4096, haptic, NULL);

  if (ret < 0) {
    int errcode = errno;
    printf("main: ERROR: Failed to start haptic: %d\n",
    errcode);
    return EXIT_FAILURE;
  }

  /* Create a separate task for handling button events */
  ret = task_create("button_task", 110, 4096, button_handler,
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
#endif

#ifdef CONFIG_PM
  struct boardioc_pm_ctrl_s pm_ctrl = {
    .action = BOARDIOC_PM_RELAX,
  };

  /* Start PM */

  boardctl(BOARDIOC_PM_CONTROL, &pm_ctrl);
#endif /* CONFIG_PM */

  while (1) {
    wait_ctx_update();

    /* Execute only on update */
    g_data.ctx->display(&g_data);

    /* Workaround: Called from same thread that manages lv objects
     * -> avoid race conditions as LVGL is not SMP-compatible by design
     */
    lv_timer_handler();
  }

#ifdef CONFIG_GRAPHICS_LVGL
  lv_disp_remove(result.disp);
  lv_deinit();
#endif

  return 0;
}
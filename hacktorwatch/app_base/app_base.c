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
#include <hacktorwatch/context.h>
#include <hacktorwatch/common.h>

#include <lvgl/lvgl.h>
#include <nuttx/timers/timer.h>
#include <nuttx/input/buttons.h>
#include <nuttx/semaphore.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BUTTON_DEVNAME "/dev/buttons"
#define BUTTONS_SIGNO 31

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

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/
void *get_ctx_data(struct data_s *data);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct data_s g_data = {0};

static int button_task(int argc, char *argv[])
{
  int ret;
  int fd;
  btn_buttonset_t supported;
  btn_buttonset_t sample;
  struct btn_notify_s btnevents;

  /* Open the BUTTON driver */

  printf("button_task: Opening %s\n", BUTTON_DEVNAME);
  fd = open(BUTTON_DEVNAME, O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    int errcode = errno;
    printf("button_task: ERROR: Failed to open %s: %d\n",
           BUTTON_DEVNAME, errcode);
    goto errout;
  }

  ret = ioctl(fd, BTNIOC_SUPPORTED,
              (unsigned long)((uintptr_t)&supported));
  if (ret < 0) {
    int errcode = errno;
    printf("button_task: ERROR: ioctl(BTNIOC_SUPPORTED) failed: %d\n",
           errcode);
    goto errout_with_fd;
  }

  printf("button_task: Supported BUTTONs 0x%02x\n",
         (unsigned int)supported);

  /* Handle only the buttton press event for the supported buttons */

  btnevents.bn_press   = supported;

  btnevents.bn_event.sigev_notify = SIGEV_SIGNAL;
  btnevents.bn_event.sigev_signo  = BUTTONS_SIGNO;

  /* Register to receive a signal when buttons are pressed/released */

  ret = ioctl(fd, BTNIOC_REGISTER,
              (unsigned long)((uintptr_t)&btnevents));
  if (ret < 0) {
    int errcode = errno;
    printf("button_task: ERROR: ioctl(BTNIOC_SUPPORTED) failed: %d\n",
           errcode);
    goto errout_with_fd;
  }

  /* Ignore the default signal action */

  signal(BUTTONS_SIGNO, SIG_IGN);

  while (1) {

    struct siginfo value;
    sigset_t set;

    /* Wait for a signal */
    sigemptyset(&set);
    sigaddset(&set, BUTTONS_SIGNO);
    ret = sigwaitinfo(&set, &value);
    if (ret < 0) {
      int errcode = errno;
      printf("button_task: ERROR: sigwaitinfo() failed: %d\n",
             errcode);
      goto errout_with_fd;
    }

    sample = (btn_buttonset_t)value.si_value.sival_int;
    printf("Pushed button %d!\n", sample);

    g_data.ctx->btn_action[sample](&g_data);
  }


errout_with_fd:
  close(fd);

errout:

  return EXIT_FAILURE;
}

static int lvgl_handler(int argc, char *argv[])
{

  while (1) {
    lv_timer_handler();
    usleep(20000);
  }

  return EXIT_FAILURE;
}

static int init(void)
{
  int ret = 0;
  sem_init(&g_data.ctx_update, 1, 0);

  sem_init(&g_data.tasks_register, 1, -(NUM_TASKS - 1));

  register_task("menu_task", menu, MENU_ID);

  for (int i = 0; i < NUM_TASKS; i++) {
    ret = task_create(g_data.tasks[i].name, 100, 4096,
                      g_data.tasks[i].entry, NULL);
    if (ret < 0) {
      int errcode = errno;
      printf("main: ERROR: Failed to start %s %d\n", g_data.tasks[i].name,
            errcode);
      return EXIT_FAILURE;
    }
  }

  sem_wait(&g_data.tasks_register);

  g_data.ctx = g_data.tasks[MENU_ID].ctx;

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

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
  /* Mutex or smth */
  g_data.ctx = ctx;
}

void signal_ctx_change(void)
{
  sem_post(&g_data.ctx_update);
}

static void wait_ctx_change(void)
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

int main(int argc, FAR char *argv[])
{
  int ret;
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;

  struct sched_param param;

  /* Check the task priority that we were started with */

  sched_getparam(0, &param);
  if (param.sched_priority != CONFIG_SYSTEM_NSH_PRIORITY)
    {
      /* If not then set the priority to the configured priority */

      param.sched_priority = CONFIG_SYSTEM_NSH_PRIORITY;
      sched_setparam(0, &param);
    }

#ifndef CONFIG_HACKTORWATCH_DISABLE_CONSOLE
  /* Initialize the NSH library */

  nsh_initialize();

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

  /* Create a separate task for handling button events */
  ret = task_create("button_task", 110, 4096, button_task,
                    NULL);
  if (ret < 0) {
    int errcode = errno;
    printf("main: ERROR: Failed to start button_task: %d\n",
           errcode);
    return EXIT_FAILURE;
  }

  /* Change the active screen's background color */

  g_data.screen = lv_obj_create(NULL);
  lv_scr_load(g_data.screen);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);

  /* Create a white label, set its text and align it to the center */

  g_data.label = lv_label_create(lv_screen_active());
  lv_label_set_text(g_data.label, "");
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_align(g_data.label, LV_ALIGN_CENTER, -20, 0);

  /* Create a separate task for handling lvgl updates */
  ret = task_create("lvgl_handler", 110, 4096, lvgl_handler,
                    NULL);

  while (1) {
    wait_ctx_change();

    /* Execute only on update */
    g_data.ctx->display(&g_data);
  }

  lv_disp_remove(result.disp);
  lv_deinit();

  return 0;
}
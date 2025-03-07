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

#include <lvgl/lvgl.h>
#include <nuttx/timers/timer.h>
#include <nuttx/input/buttons.h>
#include <nuttx/semaphore.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BUTTON_DEVNAME "/dev/buttons"
#define BUTTONS_SIGNO 31

#define NUM_BTNS (3)
#define BUTTON_UNUSED (0)
#define BUTTON_OK (1)
#define BUTTON_UP (2)
#define BUTTON_DOWN (3)

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

typedef void (*btn_behaviour)(void *ctx);

struct ctx_s {
  uint32_t bg_color;
  btn_behaviour btn_action[NUM_BTNS + 1];
};

struct data_s {
  lv_obj_t *screen;
  sem_t ctx_update; // this signals a ctx update (e.g. btn increment/decrement -> update screen)
  struct ctx_s *ctx;
  int btn_value;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void default_btn_unused(void *ctx);
static void default_btn_up(void *ctx);
static void default_btn_down(void *ctx);
static void default_btn_ok(void *ctx);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct data_s g_data = {0};
static struct ctx_s default_ctx = {
  .bg_color = 0x003a57,
  .btn_action[BUTTON_UNUSED] = default_btn_unused,
  .btn_action[BUTTON_OK] = default_btn_ok,
  .btn_action[BUTTON_UP] = default_btn_up,
  .btn_action[BUTTON_DOWN] = default_btn_down
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void default_btn_unused(void *ctx)
{
  UNUSED(ctx);
}

static void default_btn_up(void *ctx)
{
  struct data_s *g_data_ptr = (struct data_s *)ctx;
  g_data_ptr->btn_value++;
  sem_post(&g_data_ptr->ctx_update);
}

static void default_btn_down(void *ctx)
{
  struct data_s *g_data_ptr = (struct data_s *)ctx;
  g_data_ptr->btn_value--;
  sem_post(&g_data_ptr->ctx_update);
}

static void default_btn_ok(void *ctx)
{
  struct data_s *g_data_ptr = (struct data_s *)ctx;

  g_data_ptr->ctx->bg_color = ~g_data_ptr->ctx->bg_color;
  sem_post(&g_data_ptr->ctx_update);
}

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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static void init_data(void)
{
  sem_init(&g_data.ctx_update, 1, 0);
  g_data.ctx = &default_ctx;
}

int main(int argc, FAR char *argv[])
{
  int ret;
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  lv_obj_t *timer_label;

  struct sched_param param;

  /* Check the task priority that we were started with */

  sched_getparam(0, &param);
  if (param.sched_priority != CONFIG_SYSTEM_NSH_PRIORITY)
    {
      /* If not then set the priority to the configured priority */

      param.sched_priority = CONFIG_SYSTEM_NSH_PRIORITY;
      sched_setparam(0, &param);
    }

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

  init_data();

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

  timer_label = lv_label_create(lv_screen_active());
  lv_label_set_text(timer_label, "");
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_align(timer_label, LV_ALIGN_CENTER, -20, 0);

  /* Create a separate task for handling lvgl updates */
  ret = task_create("lvgl_handler", 110, 4096, lvgl_handler,
                    NULL);

  while (1) {
    sem_wait(&g_data.ctx_update);

    /* Execute only on update */
    lv_label_set_text_fmt(timer_label, "Timer: %d", g_data.btn_value);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(g_data.ctx->bg_color), LV_PART_MAIN);
  }

  lv_disp_remove(result.disp);
  lv_deinit();

  return 0;
}
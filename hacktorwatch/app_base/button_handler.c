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

int button_handler(int argc, char *argv[])
{
  int ret;
  int fd;
  btn_buttonset_t supported;
  btn_buttonset_t sample;
  struct btn_notify_s btnevents;

  /* Open the BUTTON driver */

  printf("button_handler: Opening %s\n", BUTTON_DEVNAME);
  fd = open(BUTTON_DEVNAME, O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    int errcode = errno;
    printf("button_handler: ERROR: Failed to open %s: %d\n",
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

  const struct data_s *g_data_ptr = get_g_data();

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

    g_data_ptr->ctx->btn_action[sample](g_data_ptr);
  }


errout_with_fd:
  close(fd);

errout:

  return EXIT_FAILURE;
}
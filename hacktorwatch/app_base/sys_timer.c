/****************************************************************************
 * apps/hacktorwatch/app_base/sys_timer.c
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

#include <nuttx/timers/timer.h>
#include <nuttx/input/buttons.h>
#include <nuttx/semaphore.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TIMER_DEVNAME "/dev/timer0"
#define TIMER_TIMEOUT_US (1 * 1000000)
#define TIMER_SIGNO 32

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int timer_handler(int argc, char *argv[])
{
  int ret;
  int fd;
  struct timer_notify_s notify;

  /* Open the BUTTON driver */

  printf("timer_handler: Opening %s\n", TIMER_DEVNAME);
  fd = open(TIMER_DEVNAME, O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
      int errcode = errno;
      printf("timer_handler: ERROR: Failed to open %s: %d\n",
             TIMER_DEVNAME, errcode);
      goto errout;
  }

  ret = ioctl(fd, TCIOC_SETTIMEOUT, TIMER_TIMEOUT_US);
  if (ret < 0)
    {
      fprintf(stderr, "ERROR: Failed to set the timer interval: %d\n",
              errno);
      close(fd);
      return EXIT_FAILURE;
    }

  /* Ignore the default signal action */

  signal(TIMER_SIGNO, SIG_IGN);

  notify.pid      = getpid();
  notify.periodic = true;

  notify.event.sigev_notify = SIGEV_SIGNAL;
  notify.event.sigev_signo  = TIMER_SIGNO;
  notify.event.sigev_value.sival_ptr = NULL;

  ret = ioctl(fd, TCIOC_NOTIFICATION, (unsigned long)((uintptr_t)&notify));
  if (ret < 0)
    {
      fprintf(stderr, "ERROR: Failed to set the timer handler: %d\n", errno);
      close(fd);
      return EXIT_FAILURE;
    }

  ret = ioctl(fd, TCIOC_START, 0);
  if (ret < 0)
    {
      fprintf(stderr, "ERROR: Failed to start the timer: %d\n", errno);
      close(fd);
      return EXIT_FAILURE;
    }

  while (1) {
    struct siginfo value;
    sigset_t set;

    /* Wait for a signal */
    sigemptyset(&set);
    sigaddset(&set, TIMER_SIGNO);
    ret = sigwaitinfo(&set, &value);
    if (ret < 0) {
      int errcode = errno;
      printf("timer_task: ERROR: sigwaitinfo() failed: %d\n",
              errcode);
      goto errout_with_fd;
    }

    add_g_data_time(USEC2SEC(TIMER_TIMEOUT_US));
    signal_ctx_update();
  }

errout_with_fd:
  ret = ioctl(fd, TCIOC_STOP, 0);
  if (ret < 0)
    {
      fprintf(stderr, "ERROR: Failed to stop the timer: %d\n", errno);
      close(fd);
      return EXIT_FAILURE;
    }
  close(fd);

errout:
  return EXIT_FAILURE;
}
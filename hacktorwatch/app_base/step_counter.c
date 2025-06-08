/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
//  #include <math.h>

#include <nuttx/clock.h>
#include <nuttx/signal.h>

#include "StepCountingAlgo.h"
#include <nuttx/sensors/bmi085.h>

#include <hacktorwatch/context.h>
#include <hacktorwatch/common.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BMI085_DEVPATH   "/dev/bmi085"

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct steps_data_s {
  uint8_t magic;
  uint16_t init;
  uint16_t saved;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Internal to a task */
RTC_BSS_ATTR static struct steps_data_s steps_data;

static void init_local_data(void)
{
  if (steps_data.magic == STEPS_MAGIC_NUM)
    {
      steps_data.init = steps_data.saved;
      return;
    }

  steps_data.magic = STEPS_MAGIC_NUM;
  steps_data.init = 0;
  steps_data.saved = 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int step_counter(int argc, FAR char *argv[])
{
  int ret;
  int fd_bmi085;
  struct accel_gyro_st_s data;
  time_accel_t time_ms = 0;
  float time_scale = 39.0625 / 1000.0;

  init_local_data();
#ifdef CONFIG_NIMBLE
  ble_svc_steps_cnt_set(steps_data.saved);
#endif
  set_step_count(steps_data.saved);

  /* Initialize step counting algorithm */
  initAlgo();

  fd_bmi085 = open(BMI085_DEVPATH, O_RDONLY);
  if (fd_bmi085 < 0)
    {
      printf("Device %s open failure. %d\n\n", BMI085_DEVPATH, fd_bmi085);
      return -1;
    }

  /* Read initial data sample */
  ret = read(fd_bmi085, &data, sizeof(struct accel_gyro_st_s));
  if (ret != sizeof(struct accel_gyro_st_s))
    {
      fprintf(stderr, "Read failed.\n");
      return -1;
    }

  while(1) {
    usleep(5000);
    ret = read(fd_bmi085, &data, sizeof(struct accel_gyro_st_s));
    if (ret != sizeof(struct accel_gyro_st_s))
      {
        fprintf(stderr, "Read failed.\n");
        break;
      }

    /* Convert µs to ms. */
    time_ms = data.sensor_time * time_scale;

    /* Process sample with timestamp and accel data. */
    processSample(time_ms, data.accel.x, data.accel.y, data.accel.z);

    steps_data.saved = steps_data.init + getSteps();

#ifdef CONFIG_NIMBLE
    ble_svc_steps_cnt_set(steps_data.saved);
#endif
    set_step_count(steps_data.saved);

    // printf("Step count: %d\n", getSteps());
    // fflush(stdout);
  }

  close(fd_bmi085);

  return 0;
}
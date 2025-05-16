#ifndef HACKTORWATCH_COMMON_H_
#define HACKTORWATCH_COMMON_H_

#ifdef CONFIG_GRAPHICS_LVGL
#include <lvgl/lvgl.h>
#endif

#ifdef CONFIG_PM
#include "nuttx/power/pm.h"
#endif

#include <nuttx/mqueue.h>

enum task_ctx_id {
  HOME_ID,
  MENU_ID,
  NOTIF_ID,
  TIMER_ID,
  NUM_TASKS
};

/**
 *  Used to validate data stored in RTC memory
 */
enum task_ctx_magic_num {
  HOME_MAGIC_NUM = 0x50,
  MENU_MAGIC_NUM,
  NOTIF_MAGIC_NUM,
  TIMER_MAGIC_NUM,
};

#define HAPTIC_MQ_NAME "haptic"
#define NOTIF_MQ_NAME  "notif"

#define MAX_NOTIFICATION_LEN (CONFIG_MQ_MAXMSGSIZE)

#define NOTIF_NORMAL (0)
#define NOTIF_ALERT  (1)
#define NOTIF_TIME   (2)

struct task_s {
  char *name;
  main_t entry;
  const struct ctx_s *ctx;
};

/* Forward definition */
struct ctx_node_s {
  const struct ctx_s *curr;
  const struct ctx_node_s *prev;
};

struct time_t {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;

  /* -- util here was compatible with the BLE Current Time SVC -- */

  /* Reference for Deep Sleep -> compute how much time spent in sleep */
  struct timespec tp;
};

struct data_s {
#ifdef CONFIG_GRAPHICS_LVGL
  lv_obj_t *screen;
  lv_obj_t *label;
  lv_obj_t *time_label;
#endif
  sem_t tasks_register;    /* Wait for all tasks to register */
  sem_t ctx_update;        /* this signals a ctx update */
  sem_t ctx_mutex;         /* used when changing context (not updating the existing one) */
  mqd_t haptic_mq;         /* used to trigger vibration */
  mqd_t notif_mq;          /* used to push notifications */

  /* This points to data in RTC memory */
  struct time_t *time;

  struct ctx_node_s ctx_stack; /* push ctx so one can rewind to the previous */
  const struct ctx_s *ctx;     /* ctx is modified locally */
  struct task_s tasks[NUM_TASKS];
};

struct data_s const *get_g_data(void);
void set_g_data_time(void *time_data);
void add_g_data_time(uint64_t time_elapsed_seconds);
void set_ctx(const struct ctx_s *ctx);
void rewind_ctx(void);
void signal_ctx_update(void);
void register_task(char *name, main_t entry, uint8_t id);
void set_task_ctx(const struct ctx_s *ctx, uint8_t id);
void trigger_haptic(int8_t effect_id);
int set_cpu_affinity(uint32_t core_id);

/* PM - related */

void stay(int domain, int state);
void relax(int domain, int state);
int get_staycount(int domain, int state);
void relax_once(int domain, int state);
void stay_once(int domain, int state);

/* Timer app related */

void start_timer(void);
void stop_timer(void);
void set_activity_timer_duration(uint64_t nsec, uint64_t nmin);
void set_pause_timer_duration(uint64_t nsec, uint64_t nmin);

#ifdef CONFIG_PM
int ping_wdog(void);
/**
 * Create a wrapper function that calls stay_once(domain, state).
 * This helps with waking up from an Idle state.
 *
 * The wrapper function is then called using WAKEUP_WRAP(func).
 * When CONFIG_PM is not used, this does nothing.
 */
#define WAKEUP_SOURCE(ret_type, func, domain, state) \
    static ret_type wakeup_##func(const void *ctx) { stay_once(domain, state); ping_wdog(); return func(ctx); }

#define WAKEUP_WRAP(func) \
    wakeup_##func
#else
#define WAKEUP_SOURCE(ret, func, domain, state)
#define WAKEUP_WRAP(func) func
#endif

#ifdef CONFIG_PM
#define RTC_DATA_ATTR _SECTION_ATTR(".rtc.data.", __COUNTER__)
#define RTC_BSS_ATTR __attribute__((section(".rtc.bss")))
#define _SECTION_ATTR(SECTION, COUNTER)  __attribute__((section(SECTION _STRINGIFY(COUNTER))))
#define _STRINGIFY(num) #num
#else
#define RTC_DATA_ATTR
#define RTC_BSS_ATTR
#endif

#endif
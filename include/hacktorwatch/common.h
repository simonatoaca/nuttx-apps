#ifndef HACKTORWATCH_COMMON_H_
#define HACKTORWATCH_COMMON_H_

#include <lvgl/lvgl.h>
#include <nuttx/mqueue.h>

#define BUTTON_UNUSED (0)
#define BUTTON_OK (1)
#define BUTTON_UP (2)
#define BUTTON_DOWN (3)


#define NUM_TASKS (2) // TODO: Update this

#define HOME_ID (0)
#define MENU_ID (1)

#define HAPTIC_MQ_NAME "haptic"

struct task_s {
  char *name;
  main_t entry;
  struct ctx_s *ctx;
  sem_t trigger;
};

struct data_s {
  lv_obj_t *screen;
  lv_obj_t *label;
  sem_t tasks_register;    /* Wait for all tasks to register */
  sem_t ctx_update;        /* this signals a ctx update */
  sem_t ctx_mutex;         /* used when changing context (not updating the existing one) */
  mqd_t haptic_mq;         /* used to trigger vibration */

  const struct ctx_s *ctx; /* ctx is modified locally */
  struct task_s tasks[NUM_TASKS];
};

struct data_s const *get_g_data(void);
void set_ctx(struct ctx_s *ctx);
void signal_ctx_update(void);
void register_task(char *name, main_t entry, uint8_t id);
void set_task_ctx(const struct ctx_s *ctx, uint8_t id);
void trigger_haptic(uint8_t effect_id);

#endif
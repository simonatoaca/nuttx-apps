#ifndef HACKTORWATCH_COMMON_H_
#define HACKTORWATCH_COMMON_H_

#ifdef CONFIG_GRAPHICS_LVGL
#include <lvgl/lvgl.h>
#endif

#include <nuttx/mqueue.h>

#define BUTTON_UNUSED (0)
#define BUTTON_OK (1)
#define BUTTON_UP (2)
#define BUTTON_DOWN (3)


#define NUM_TASKS (3) // TODO: Update this

#define HOME_ID  (0)
#define MENU_ID  (1)
#define NOTIF_ID (2)

#define HAPTIC_MQ_NAME "haptic"
#define NOTIF_MQ_NAME "notif"

#define MAX_NOTIFICATION_LEN (CONFIG_MQ_MAXMSGSIZE)

struct task_s {
  char *name;
  main_t entry;
  struct ctx_s *ctx;
};

/* Forward definition */
struct ctx_node_s {
  const struct ctx_s *curr;
  const struct ctx_node_s *prev;
};

struct data_s {
#ifdef CONFIG_GRAPHICS_LVGL
  lv_obj_t *screen;
  lv_obj_t *label;
#endif
  sem_t tasks_register;    /* Wait for all tasks to register */
  sem_t ctx_update;        /* this signals a ctx update */
  sem_t ctx_mutex;         /* used when changing context (not updating the existing one) */
  mqd_t haptic_mq;         /* used to trigger vibration */
  mqd_t notif_mq;          /* used to push notifications */

  struct ctx_node_s ctx_stack; /* push ctx so one can rewind to the previous */
  const struct ctx_s *ctx;     /* ctx is modified locally */
  struct task_s tasks[NUM_TASKS];
};

struct data_s const *get_g_data(void);
void set_ctx(struct ctx_s *ctx);
void rewind_ctx(void);
void signal_ctx_update(void);
void register_task(char *name, main_t entry, uint8_t id);
void set_task_ctx(const struct ctx_s *ctx, uint8_t id);
void trigger_haptic(uint8_t effect_id);
int set_cpu_affinity(uint32_t core_id);

#endif
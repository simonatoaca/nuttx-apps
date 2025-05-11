#ifndef HACKTORWATCH_CONTEXT_H_
#define HACKTORWATCH_CONTEXT_H_

enum button_id {
  BUTTON_UNUSED,
  BUTTON_OK,
  BUTTON_UP,
  BUTTON_DOWN,
  NUM_BTNS
};

typedef void (*btn_behaviour)(const void *ctx);
typedef void (*display_func)(void *ctx);

struct ctx_s {
  btn_behaviour btn_action[NUM_BTNS + 1];
  display_func display;
  void *data; /* Will hold a <ctx_name>_data_s struct */
};

int menu(int argc, char *argv[]);
int home(int argc, char *argv[]);
int notif(int argc, char *argv[]);
int haptic(int argc, char *argv[]);
int button_handler(int argc, char *argv[]);
int timer_handler(int argc, char *argv[]);
int nimble(int argc, char *argv[]);

#endif
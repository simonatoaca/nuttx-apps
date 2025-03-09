#ifndef HACKTORWATCH_CONTEXT_H_
#define HACKTORWATCH_CONTEXT_H_

#define NUM_BTNS (3)

typedef void (*btn_behaviour)(void *ctx);
typedef void (*display_func)(void *ctx);

struct ctx_s {
  btn_behaviour btn_action[NUM_BTNS + 1];
  display_func display;
  void *data; /* Will hold a <ctx_name>_data_s struct */
  size_t data_size; /* <ctx_name>_data_s struct size */
};

int menu(int argc, char *argv[]);

#endif
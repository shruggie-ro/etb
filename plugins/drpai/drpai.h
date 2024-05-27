#ifndef __DRPAI_H__
#define __DRPAI_H__

#include <json-c/json.h>
#include "models.h"

// FIXME hard-coded
#define DRPAI_BUF_LEN	(640 * 480 * 2)

extern bool drpai_active;
extern struct drpai *drpai;

struct drpai;

struct drpai *drpai_init(int *err);
void drpai_free(struct drpai *d);

int drpai_is_running(struct drpai *d);

const char *drpai_model_load_input(struct drpai *d, void *addr, int len);
const char *drpai_model_start(struct drpai *d);
const char *drpai_model_get_result(struct drpai *d, json_object* result);

int drpai_load_model(struct drpai *d, json_object *req);

int drpai_models_get(json_object *req);

// FIXME: hack
int drpai_model_run_and_wait_hack(void *addr, json_object *result);

#endif

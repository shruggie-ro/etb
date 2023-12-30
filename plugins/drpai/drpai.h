#ifndef __DRPAI_H__
#define __DRPAI_H__

#include <json-c/json.h>

struct drpai;

enum model_type {
	MODEL_YOLOV3,
};

struct drpai *drpai_init(int *err);
void drpai_free(struct drpai *d);

int drpai_model_run_and_wait(struct drpai *d, void *addr, json_object *result);

int drpai_load_model(struct drpai *d, json_object *req);

int drpai_models_get(json_object *req);

// FIXME: hack
int drpai_model_run_and_wait_hack(void *addr, json_object *result);

#endif

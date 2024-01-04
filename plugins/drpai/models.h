#ifndef __MODELS_H__
#define __MODELS_H__

#include "drpai.h"
#include <json-c/json.h>

struct drpai_model {
	void *(*init)(const char *name, int *err);
	void (*cleanup)(void *model_params);
	int (*postprocessing)(void *model_params, float *data, int width, int height, json_object *result);
};

const struct drpai_model *drpai_model_type_enum_to_ops(enum model_type type);
char **drpai_load_labels_from_file(const char *model, const char *fname, int *ret);

json_object *drpai_model_types_get();
enum model_type drpai_model_name_to_enum(const char *name);

#ifdef MODELS_PRIVATE_DATA
extern const struct drpai_model yolov3_model;
#endif

#endif /* __MODELS_H__ */

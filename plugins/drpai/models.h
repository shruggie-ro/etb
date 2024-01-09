#ifndef __MODELS_H__
#define __MODELS_H__

#include <json-c/json.h>

struct drpai_model_ops {
	void *(*init)(json_object *config, int *err);
	void (*cleanup)(void *priv);
	int (*postprocessing)(void *priv, float *data, int width, int height, json_object *result);
};

const struct drpai_model_ops *drpai_model_type_to_ops(const char *type);
/* FIXME: Currently not used */
char **drpai_load_labels_from_file(const char *model, const char *fname, int *ret);

json_object *drpai_model_types_get();

#ifdef MODELS_PRIVATE_DATA
extern const struct drpai_model_ops yolo_model_ops;
#endif

#endif /* __MODELS_H__ */

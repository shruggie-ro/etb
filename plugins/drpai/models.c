#define MODELS_PRIVATE_DATA
#include "models.h"

#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *model_type_names[] = {
	[MODEL_TYPE_YOLOV3] = "YOLOv3",
	NULL
};

const struct drpai_model *drpai_model_type_enum_to_ops(enum model_type type)
{
	switch(type) {
		case MODEL_TYPE_YOLOV3: return &yolov3_model;
		default: return NULL;
	}
}

enum model_type drpai_model_name_to_enum(const char *name)
{
	int i;

	for (i = 0; model_type_names[i]; i++) {
		if (strcmp(name, model_type_names[i]))
			continue;
		return i;
	}

	return MODEL_TYPE_INVALID;
}

json_object *drpai_model_types_get()
{
	json_object *arr;
	int i;

	arr = json_object_new_array();
	if (!arr)
		return NULL;

	for (i = 0; model_type_names[i]; i++)
		json_object_array_add(arr, json_object_new_string(model_type_names[i]));

	return arr;
}

char **drpai_load_labels_from_file(const char *model, const char *fname, int *ret)
{
	char path[512], *line = NULL;
	size_t n, allocated, num_labels;
	char **labels = NULL;
	struct stat st;
	ssize_t read;
	FILE *fp;
	int lret;

	if (!ret)
		return NULL;

	if (!fname) {
		lret = -EINVAL;
		goto err_store;
	}

	if (model)
		snprintf(path, sizeof(path), "%s/%s/%s", DRPAI_MODELS_ROOT_DIR, model, fname);
	else
		snprintf(path, sizeof(path), "%s/%s", DRPAI_MODELS_ROOT_DIR, fname);

	if (stat(path, &st)) {
		lret = -errno;
		goto err_store;
	}

	/* first count the lines */
	fp = fopen(path, "r");
	if (!fp) {
		lret = -errno;
		goto err_store;
	}

	allocated = 0;
	num_labels = 0;
	while ((read = getline(&line, &n, fp)) != -1) {
		char *s;
		if (num_labels >= allocated) {
			void *oldptr = labels;
			allocated += 32;
			labels = realloc(labels, allocated * sizeof(char*));
			if (!labels) {
				free(oldptr);
				lret = -ENOMEM;
				goto err_unwind;
			}
		}
		s = labels[num_labels] = strdup(line);
		/* cut new line if there is one */
		s += (read - 1);
		if (*s == '\n')
			*s = '\0';
		num_labels++;
	}
	free(line);
	fclose(fp);

	*ret = num_labels;

	return labels;
err_unwind:
	for (n = 0; n < num_labels; n++)
		free(labels[n]);
	free(labels);
	fclose(fp);
err_store:
	*ret = lret;
	return NULL;
}

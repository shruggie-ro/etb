#define MODELS_PRIVATE_DATA
#include "models.h"

#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct model_type_to_ops_map {
	const char *type;
	const struct drpai_model_ops *ops;
};

/* These are common/reference models */
static const struct model_type_to_ops_map model_type_to_ops_map[] = {
	{ "tinyyolov2",		&yolo_model_ops },
	{ "tinyyolov3",		&yolo_model_ops },
	{ "yolov2",		&yolo_model_ops },
	{ "yolov3",		&yolo_model_ops },
	{ /* sentinel */ }
};

const struct drpai_model_ops *drpai_model_type_to_ops(const char *type)
{
	int i;

	if (!type)
		return NULL;

	for (i = 0; model_type_to_ops_map[i].type; i++) {
		if (strcmp(type, model_type_to_ops_map[i].type))
			continue;
		return model_type_to_ops_map[i].ops;
	}

	return NULL;
}

/* FIXME: Currently not used */
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


#include "models.h"

#include <errno.h>
#include <math.h>
#include <float.h>

#include <libwebsockets.h>

#define max(a, b)	((a) > (b) ? (a) : (b))

struct yolo_model_params {
	int ver;
	char **labels;
	int num_labels;
	float *classes;
	int num_inf_out_layer;
	int *num_grids;
	int num_bb;
	float thresh_prob;
	float thresh_nms;
	int model_in_w;
	int model_in_h;
	double *anchors;
};

struct box {
	int x;
	int y;
	int w;
	int h;
};

struct detection {
	struct box box;
	int pred_class;
	float probability;
};

static int yolo_index(int num_grid, int offs, int channel)
{
	return offs + channel * num_grid * num_grid;
}

static int yolo_offset(int n, int b, int y, int x, const int* num_grids, int numBB, int32_t numClass)
{
	int num = num_grids[n];
	int i, prev_layer_num = 0;

	for (i = 0; i < n; i++) {
		prev_layer_num += numBB * (numClass + 5) * num_grids[i] * num_grids[i];
	}
	return prev_layer_num + b * (numClass + 5) * num * num + y * num + x;
}

static double sigmoid(double x)
{
	return 1.0 / (1.0 + exp(-x));
}

static void softmax(float *val, int num)
{
	float max_num = -FLT_MAX;
	float sum = 0;
	int i;

	for (i = 0 ; i < num ; i++)
		max_num = max(max_num, val[i]);

	for (i = 0 ; i < num ; i++) {
		val[i]= (float) exp(val[i] - max_num);
		sum+= val[i];
	}

	for (i = 0 ; i < num ; i++)
		val[i]= val[i] / sum;
}

static float overlap(float x1, float w1, float x2, float w2)
{
	float l1 = x1 - w1 / 2;
	float l2 = x2 - w2 / 2;
	float left = l1 > l2 ? l1 : l2;
	float r1 = x1 + w1 / 2;
	float r2 = x2 + w2 / 2;
	float right = r1 < r2 ? r1 : r2;
	return right - left;
}

static float box_intersection(struct box *a, struct box *b)
{
	float w = overlap(a->x, a->w, b->x, b->w);
	float h = overlap(a->y, a->h, b->y, b->h);
	if (w < 0 || h < 0)
		return 0;
	return w * h;
}

static float box_union(float box_intersection, float area_a, float area_b)
{
	float i = box_intersection;
	float u = area_a + area_b - i;
	return u;
}

static float box_iou(float box_intersection, float area_a, float area_b)
{
    return box_intersection / box_union(box_intersection, area_a, area_b);
}

static void filter_boxes_nms(struct detection *d, int size, float th_nms)
{
	float b_intersection, area_a, area_b;
	int i, j;
	for (i = 0; i < size; i++) {
		struct box *a = &d[i].box;
		for (j = 0; j < size; j++) {
			struct box *b;
			if (i == j)
				continue;
			if (d[i].pred_class != d[j].pred_class)
				continue;
			b = &d[j].box;
			area_a = a->h * a->w;
			area_b = b->h * b->w;
			b_intersection = box_intersection(a, b);
			if ((box_iou(b_intersection, area_a, area_b) > th_nms) ||
			    (b_intersection >= area_a - 1) ||
			    (b_intersection >= area_b - 1)) {
				if (d[i].probability > d[j].probability)
					d[j].probability = 0;
				else
					d[i].probability = 0;
			}
		}
	}
}

static int yolo_postprocessing(void *model_params, float *data, int width, int height, json_object *result)
{
	struct yolo_model_params *p = model_params;
	struct detection *d, *detections = NULL;
	int num_detections = 0, allocated = 0;
	int num_class = p->num_labels;
	int i, n, b, y, x, rc;
	json_object *arr, *obj;
	float *classes = p->classes;

	/* Following variables are required for correct_yolo/region_boxes in Darknet implementation*/
	/* Note: This implementation refers to the "darknet detector test" */
	float new_w, new_h;
	const float correct_w = 1.;
	const float correct_h = 1.;
	if ((float)(p->model_in_w / correct_w) < (float)(p->model_in_h / correct_h)) {
		new_w = (float)p->model_in_w;
		new_h = correct_h * p->model_in_w / correct_w;
	} else {
		new_w = correct_w * p->model_in_h / correct_h;
		new_h = p->model_in_h;
	}

	for (n = 0; n < p->num_inf_out_layer; n++) {
		int num_grid = p->num_grids[n];
		int anchor_offset = 2 * p->num_bb * (p->num_inf_out_layer - (n + 1));
		for (b = 0; b < p->num_bb; b++) {
			for (y = 0; y < num_grid; y++) {
				for (x = 0; x < num_grid; x++) {
					int offs = yolo_offset(n, b, y, x, p->num_grids, p->num_bb, num_class);
					float tx = data[offs];
					float ty = data[yolo_index(num_grid, offs, 1)];
					float tw = data[yolo_index(num_grid, offs, 2)];
					float th = data[yolo_index(num_grid, offs, 3)];
					float tc = data[yolo_index(num_grid, offs, 4)];

					float max_pred = 0;
					int pred_class = -1;

					float objectness = sigmoid(tc);
					float probability;

					/* Compute the bounding box */
					/* get_yolo_box/get_region_box in paper implementation*/
					float center_x = ((float)x + sigmoid(tx)) / (float)num_grid;
					float center_y = ((float)y + sigmoid(ty)) / (float)num_grid;
					float box_w;
					float box_h;

					if (p->ver == 3) {
						box_w = (float)exp(tw) * p->anchors[anchor_offset + 2 * b + 0] / (float)p->model_in_w;
						box_h = (float)exp(th) * p->anchors[anchor_offset + 2 * b + 1] / (float)p->model_in_h;
					} else {
						box_w = (float)exp(tw) * p->anchors[anchor_offset + 2 * b + 0] / (float)num_grid;
						box_h = (float)exp(th) * p->anchors[anchor_offset + 2 * b + 1] / (float)num_grid;
					}

					/* Adjustment for VGA size */
					/* correct_yolo/region_boxes */
					center_x = (center_x - (p->model_in_w - new_w) / 2. / p->model_in_w) / ((float)new_w / p->model_in_w);
					center_y = (center_y - (p->model_in_h - new_h) / 2. / p->model_in_h) / ((float)new_h / p->model_in_h);
					box_w *= (float)(p->model_in_w / new_w);
					box_h *= (float)(p->model_in_h / new_h);

					center_x = round(center_x * width);
					center_y = round(center_y * height);
					box_w = round(box_w * width);
					box_h = round(box_h * height);

					/* Get the class prediction */
					for (i = 0; i < num_class; i++) {
						if (p->ver == 3)
							classes[i] = sigmoid(data[yolo_index(num_grid, offs, 5 + i)]);
						else
							classes[i] = data[yolo_index(num_grid, offs, 5 + i)];
					}

					if (p->ver == 2)
						softmax(classes, num_class);

					for (i = 0; i < num_class; i++) {
						if (classes[i] > max_pred) {
							pred_class = i;
							max_pred = classes[i];
						}
					}

					probability = max_pred * objectness;

					/* Store the result into the list if the probability is more than the threshold */
					if (probability < p->thresh_prob)
						continue;

					if (num_detections >= allocated) {
						void *oldptr = detections;
						allocated += 32;
						detections = realloc(detections, allocated * sizeof(*detections));
						if (!detections) {
							free(oldptr);
							return -ENOMEM;
						}
					}
					d = &detections[num_detections];
					d->pred_class = pred_class;
					d->probability = probability * 100.0f;
					d->box.w = box_w;
					d->box.h = box_h;
					d->box.x = (int)(center_x - (d->box.w / 2));
					d->box.y = (int)(center_y - (d->box.h / 2));
					num_detections++;
				}
			}
		}
	}
	/* Non-Maximum Supression filter */
	filter_boxes_nms(detections, num_detections, p->thresh_nms);

	obj = json_object_new_object();
	arr = json_object_new_array();
	if (!arr || !obj) {
		rc = -errno;
		goto err;
	}
	json_object_object_add(result, "name", json_object_new_string("drpai-object-detection-result"));
	json_object_object_add(result, "value", arr);

	rc = 0;
	for (i = 0; i < num_detections; i++) {
		json_object *jobj, *jbox;
		d = &detections[i];
		if (d->probability < p->thresh_prob)
			continue;
		jobj = json_object_new_object();
		jbox = json_object_new_object();
		if (!jobj || !jbox) {
			json_object_put(jbox);
			json_object_put(jobj);
			rc = -errno;
			goto err;
		}
		n = d->pred_class;
		json_object_object_add(jobj, "label", json_object_new_string(p->labels[n]));
		json_object_object_add(jobj, "box", jbox);
		json_object_object_add(jbox, "x", json_object_new_int(d->box.x));
		json_object_object_add(jbox, "y", json_object_new_int(d->box.y));
		json_object_object_add(jbox, "w", json_object_new_int(d->box.w));
		json_object_object_add(jbox, "h", json_object_new_int(d->box.h));

		json_object_object_add(jobj, "probability", json_object_new_double(d->probability));

		json_object_array_add(arr, jobj);
        }
	free(detections);

	return 0;
err:
	free(detections);
	json_object_put(obj);
	json_object_put(arr);
	return rc;
}

static int yolo_load_labels(json_object *config, struct yolo_model_params *p)
{
	json_object *jobj;
	int i;

	jobj = json_object_object_get(config, "labels");
	if (!jobj || !json_object_is_type(jobj, json_type_array))
		return -ENOENT;

	p->num_labels = json_object_array_length(jobj);
	p->labels = malloc(sizeof(char*) * p->num_labels);
	if (!p->labels) {
		free(p);
		return -ENOMEM;
	}

	/* This saves up a bit on re-allocation during processing */
	p->classes = malloc(sizeof(*(p->classes)) * p->num_labels);
	if (!p->classes) {
		free(p->labels);
		free(p);
		return -ENOMEM;
	}

	for (i = 0; i < p->num_labels; i++) {
		json_object *e = json_object_array_get_idx(jobj, i);
		p->labels[i] = strdup(json_object_get_string(e));
	}

	return 0;
}

static void yolo_free_labels(struct yolo_model_params *p)
{
	int i;

	if (!p || !p->labels)
		return;

	for (i = 0; i < p->num_labels; i++)
		free(p->labels[i]);
	free(p->labels);
	free(p->classes);
}

static int yolo_config_get_int(json_object *cfg, const char *id, int dflt)
{
	json_object *jobj = json_object_object_get(cfg, id);
	return jobj ? json_object_get_int(jobj) : dflt;
}

static float yolo_config_get_float(json_object *cfg, const char *id, float dflt)
{
	json_object *jobj = json_object_object_get(cfg, id);
	return jobj ? json_object_get_double(jobj) : dflt;
}

static int yolo_config_load_num_grids(json_object *cfg, struct yolo_model_params *p)
{
	json_object *jobj = json_object_object_get(cfg, "num_grids");
	int i, num;

	if (!jobj || !json_object_is_type(jobj, json_type_array))
		return -EINVAL;

	num = json_object_array_length(jobj);
	p->num_inf_out_layer = num;
	p->num_grids = malloc(sizeof(*(p->num_grids)) * num);
	if (!p->num_grids)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		json_object *e = json_object_array_get_idx(jobj, i);
		p->num_grids[i] = json_object_get_int(e);
	}

	return 0;
}

static int yolo_config_load_anchors(json_object *cfg, struct yolo_model_params *p)
{
	json_object *jobj = json_object_object_get(cfg, "anchors");
	int i, num;

	if (!jobj || !json_object_is_type(jobj, json_type_array))
		return -EINVAL;

	num = json_object_array_length(jobj);
	p->anchors = malloc(sizeof(*(p->anchors)) * num);
	if (!p->anchors)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		json_object *e = json_object_array_get_idx(jobj, i);
		p->anchors[i] = json_object_get_double(e);
	}

	return 0;
}

static void *yolo_init(json_object *config, int *err)
{
	struct yolo_model_params *p = NULL;
	int rc, lret = 0;
	const char *s;

	s = json_object_get_string(json_object_object_get(config, "model_type"));
	if (!s) {
		lret = -ENOENT;
		goto err_store;
	}

	p = calloc(1, sizeof(*p));
	if (!p) {
		lret = -ENOMEM;
		goto err_store;
	}

	if (!strcmp("yolov3", s)) {
		p->ver = 3;
	} else if (!strcmp("yolov2", s)) {
		p->ver = 2;
	} else {
		lret = -EINVAL;
		goto err_store;
	}

	rc = yolo_load_labels(config, p);
	if (rc)
		goto err_store;

	p->model_in_w = yolo_config_get_int(config, "model_in_w", 416);
	p->model_in_h = yolo_config_get_int(config, "model_in_h", 416);
	p->num_bb = yolo_config_get_int(config, "num_bb", -1);
	if (p->num_bb < 0) {
		lret = -EINVAL;
		goto err_store;
	}

	p->thresh_prob = yolo_config_get_float(config, "thresh_prob", -1.);
	if (p->thresh_prob < 0) {
		lret = -EINVAL;
		goto err_store;
	}

	p->thresh_nms = yolo_config_get_float(config, "thresh_nms", -1.);
	if (p->thresh_nms < 0) {
		lret = -EINVAL;
		goto err_store;
	}

	lret = yolo_config_load_num_grids(config, p);
	if (lret < 0)
		goto err_store;

	lret = yolo_config_load_anchors(config, p);
	if (lret < 0)
		goto err_store;

	return p;
err_store:
	free(p->num_grids);
	free(p->anchors);
	yolo_free_labels(p);
	free(p);
	if (err)
		*err = lret;
	return NULL;
}

static void yolo_cleanup(void *model_params)
{
	struct yolo_model_params *p = model_params;

	if (!p)
		return;

	free(p->num_grids);
	free(p->anchors);
	yolo_free_labels(p);
	free(p);
}

const struct drpai_model_ops yolo_model_ops = {
	.init = yolo_init,
	.cleanup = yolo_cleanup,
	.postprocessing = yolo_postprocessing,
};


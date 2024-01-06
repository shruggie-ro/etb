
#include "models.h"

#include <errno.h>
#include <math.h>

#include <libwebsockets.h>

struct yolov3_model_params {
	char **labels;
	int num_labels;
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

#define YOLOV3_NUM_INF_OUT_LAYER	3
static const int num_grids[] = { 13, 26, 52 }; /* the number of elements should be YOLOV3_NUM_INF_OUT_LAYER */

#define YOLOV3_NUM_BB			3
#define YOLOV3_TH_PROB			0.5f
#define YOLOV3_TH_NMS			0.5f
#define COCO_LABELS_FILENAME		"coco-labels-2014_2017.txt"

static const double anchors[] = {
	10, 13,
	16, 30,
	33, 23,
	30, 61,
	62, 45,
	59, 119,
	116, 90,
	156, 198,
	373, 326
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

static float box_union(struct box *a, struct box *b)
{
	float i = box_intersection(a, b);
	float u = a->w * a->h + b->w * b->h - i;
	return u;
}

static float box_iou(struct box *a, struct box *b)
{
    return box_intersection(a, b) / box_union(a, b);
}

static void filter_boxes_nms(struct detection *d, int size, float th_nms)
{
	float b_intersection = 0;
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
			b_intersection = box_intersection(a, b);
			if ((box_iou(a, b) > th_nms) ||
			    (b_intersection >= a->h * a->w - 1) ||
			    (b_intersection >= b->h * b->w - 1)) {
				if (d[i].probability > d[j].probability)
					d[j].probability = 0;
				else
					d[i].probability = 0;
			}
		}
	}
}

static int yolov3_postprocessing(void *model_params, float *data, int width, int height, json_object *result)
{
	struct yolov3_model_params *p = model_params;
	struct detection *d, *detections = NULL;
	int num_detections = 0, allocated = 0;
	int num_class = p->num_labels;
	float width_f = width;
	float height_f = height;
	int i, n, b, y, x, rc;
	json_object *arr;

	for (n = 0; n < YOLOV3_NUM_INF_OUT_LAYER; n++) {
		int num_grid = num_grids[n];
		int anchor_offset = 2 * YOLOV3_NUM_BB * (YOLOV3_NUM_INF_OUT_LAYER - (n + 1));
		for (b = 0; b < YOLOV3_NUM_BB; b++) {
			for (y = 0; y < num_grid; y++) {
				for (x = 0; x < num_grid; x++) {
					int offs = yolo_offset(n, b, y, x, num_grids, YOLOV3_NUM_BB, num_class);
					float tx = data[offs];
					float ty = data[yolo_index(num_grid, offs, 1)];
					float tw = data[yolo_index(num_grid, offs, 2)];
					float th = data[yolo_index(num_grid, offs, 3)];
					float tc = data[yolo_index(num_grid, offs, 4)];

					/* Compute the bounding box */
					/* get_yolo_box/get_region_box in paper implementation*/
					float center_x = ((float)x + sigmoid(tx)) / (float)num_grid;
					float center_y = ((float)y + sigmoid(ty)) / (float)num_grid;
					float box_w = (float)exp(tw) * anchors[anchor_offset + 2 * b + 0] / width_f;
					float box_h = (float)exp(th) * anchors[anchor_offset + 2 * b + 1] / height_f;

					float max_pred = 0;
					int pred_class = -1;

					float objectness = sigmoid(tc);
					float probability;

					center_x = round(center_x * width_f);
					center_y = round(center_y * height_f);
					box_w = round(box_w * width_f);
					box_h = round(box_h * height_f);

					/* Get the class prediction */
					for (i = 0; i < num_class; i++) {
						float class_pred = sigmoid(data[yolo_index(num_grid, offs, 5 + i)]);
						if (class_pred > max_pred) {
							pred_class = i;
							max_pred = class_pred;
						}
					}

					probability = max_pred * objectness;

					/* Store the result into the list if the probability is more than the threshold */
					if (probability < YOLOV3_TH_PROB)
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
					d->box.y = (int)(center_y - (d->box.y / 2));
					num_detections++;
				}
			}
		}
	}
	/* Non-Maximum Supression filter */
	filter_boxes_nms(detections, num_detections, YOLOV3_TH_NMS);

	arr = json_object_new_array();
	if (!arr) {
		free(detections);
		return -errno;
	}
	json_object_object_add(result, "object_detection", arr);

	rc = 0;
	for (i = 0; i < num_detections; i++) {
		json_object *jobj, *jbox;
		d = &detections[i];
		if (d->probability == 0)
			continue;
		jobj = json_object_new_object();
		if (!jobj) {
			rc = -errno;
			break;
		}
		jbox = json_object_new_object();
		if (!jbox) {
			rc = -errno;
			break;
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

	return rc;
}

static void *yolov3_init(const char *name, int *err)
{
	char label_file[128];
	struct yolov3_model_params *p;
	int lret = 0;

	p = malloc(sizeof(*p));
	if (!p) {
		lret = -ENOMEM;
		goto err_store;
	}

	snprintf(label_file, sizeof(label_file), "%s.txt", name);
	p->labels = drpai_load_labels_from_file(NULL, label_file, &lret);
	if (lret > 0) {
		p->num_labels = lret;
		return p;
	}
	free(p->labels);

	/* Try to load a 'labels.txt' from inside the model dir */
	p->labels = drpai_load_labels_from_file(name, "labels.txt", &lret);
	if (lret > 0) {
		p->num_labels = lret;
		return p;
	}
	free(p->labels);

	p->labels = drpai_load_labels_from_file(name, COCO_LABELS_FILENAME, &lret);
	if (lret > 0) {
		p->num_labels = lret;
		return p;
	}
	free(p->labels);

err_store:
	free(p);
	if (err)
		*err = lret;
	return NULL;
}

static void yolov3_cleanup(void *model_params)
{
	struct yolov3_model_params *p = model_params;
	int i;

	if (!p)
		return;

	for (i = 0; i < p->num_labels; i++)
		free(p->labels[i]);
	free(p->labels);
	free(p);
}

const struct drpai_model yolov3_model = {
	.init = yolov3_init,
	.cleanup = yolov3_cleanup,
	.postprocessing = yolov3_postprocessing,
};


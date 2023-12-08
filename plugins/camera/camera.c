
#include "handler.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <libwebsockets.h>

#define NUM_MAX_CAMERAS		32
#define NUM_MAX_CAPTURE_BUFS	8
#define DEV_NAME_MAX_SIZE	sizeof("/dev/video999")

struct camera_buffer {
	uint8_t *ptr;
	size_t length;
};

struct camera_entry {
	char dev_name[DEV_NAME_MAX_SIZE];
	struct camera_buffer buffers[NUM_MAX_CAPTURE_BUFS];
	int fd;
};

/* FIXME: add mutex when adding threads */
static struct camera_entry camera_active_list[NUM_MAX_CAMERAS];

static int xioctl(int fd, int request, void* arg)
{
	int r;
	do {
		r = ioctl(fd, request, arg);
	} while (r < 0 && EINTR == errno);
	return r;
}

static int camera_set_capture_parameters(int fd)
{
	struct v4l2_streamparm setfps = {};
	struct v4l2_format fmt = {};

	/* FIXME: hard-coded for now */
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = 640;
	fmt.fmt.pix.height = 480;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		lwsl_err("ioctl(VIDIOC_S_FMT): %s\n", strerror(errno));
		return -1;
	}

	/* FIXME: this may not always work on some RZ ISP stuff */
	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	setfps.parm.capture.timeperframe.numerator = 1;
	setfps.parm.capture.timeperframe.denominator = 30;

	if (xioctl(fd, VIDIOC_S_PARM, &setfps) < 0) {
		lwsl_err("ioctl(VIDIOC_S_PARM): %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int camera_request_buffers(int fd, int num_bufs) {
	struct v4l2_requestbuffers req = {};

	req.count = num_bufs;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		lwsl_err("ioctl(VIDIOC_REQBUFS): %s\n", strerror(errno));
		return -1;
	}
 
	return req.count;
}

static int camera_enqueue_buffer(int fd, int index) {
	struct v4l2_buffer bufd = {};

	bufd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufd.memory = V4L2_MEMORY_MMAP;
	bufd.index = index;
	if (xioctl(fd, VIDIOC_QBUF, &bufd) < 0) {
		lwsl_err("ioctl(VIDIOC_QBUF)[%d]: %s\n", index, strerror(errno));
		return -1;
	}

	return bufd.bytesused;
}

static int camera_dequeue_buffer(int fd, size_t *blen) {
	struct v4l2_buffer buf = {};

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;

	if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
		lwsl_err("ioctl(VIDIOC_QBUF): %s\n", strerror(errno));
		return -1;
	}

	*blen = buf.length;

	return buf.index;
}

static int camera_query_buffer(int fd, int index, struct camera_buffer *cam_buf) {
	struct v4l2_buffer buf = {};

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = index;

	if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
		lwsl_err("ioctl(VIDIOC_QUERYBUF)[%d]: %s\n", index, strerror(errno));
		return -1;
	}


	cam_buf->ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
	cam_buf->length = buf.length;

	return buf.length;
}

static int camera_streaming_set_on(int fd, bool on) {
	unsigned int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	const int vidioc = on ? VIDIOC_STREAMON : VIDIOC_STREAMOFF;

	if (xioctl(fd, vidioc, &type) < 0) {
		lwsl_err("ioctl(%s): %s\n",
			 on ? "VIDIOC_STREAMON" : "VIDIOC_STREAMOFF",
			 strerror(errno));
		return -1;
	}

	return 0;
}

static struct camera_entry *camera_find_active(const char *dev, int *first_free_idx)
{
	int i;

	if (!dev)
		return NULL;

	if (first_free_idx)
		*first_free_idx = -1;

	for (i = 0; i < NUM_MAX_CAMERAS; i++) {
		struct camera_entry *e = &camera_active_list[i];
		if (e->dev_name[0] == '\0') {
			if (first_free_idx && *first_free_idx < 0)
				*first_free_idx = i;
			continue;
		}
		if (strncmp(dev, e->dev_name, sizeof(e->dev_name) - 1) == 0)
			return e;
	}

	return NULL;
}

/* Public functions defined from here on */

json_object *camera_devices_get(json_object *, enum RESPONSE *rc)
{
	char dev_name[DEV_NAME_MAX_SIZE];
	struct v4l2_capability caps;
	json_object *cam_arr;
	int i, fd, ret;

	cam_arr = json_object_new_array();
	if (!cam_arr) {
		*rc = RESPONSE_INTERNAL_ERROR;
		return NULL;
	}

	for (i = 0; i < 999; i++) {
		json_object *e;
		snprintf(dev_name, sizeof(dev_name), "/dev/video%d", i);

		fd = open(dev_name, O_RDWR);
		if (fd < 0)
			break;

		memset(&caps, 0, sizeof(caps));
		ret = xioctl(fd, VIDIOC_QUERYCAP, &caps);
		close(fd);
		if (ret < 0)
			continue;

		e = json_object_new_object();
		if (!e)
			continue;

		json_object_object_add(e, "device", json_object_new_string(dev_name));
		json_object_object_add(e, "driver", json_object_new_string((char *)caps.driver));
		json_object_object_add(e, "card", json_object_new_string((char *)caps.card));
		json_object_object_add(e, "version", json_object_new_uint64(caps.version));
		/* FIXME: maybe we stringify these below? */
		json_object_object_add(e, "capabilities", json_object_new_uint64(caps.capabilities));;
		json_object_object_add(e, "device_caps", json_object_new_uint64(caps.device_caps));

		json_object_array_add(cam_arr, e);
	}

	*rc = RESPONSE_SEND_BACK_JSON;

	return cam_arr;
}

json_object *camera_dev_play_start(json_object *req, enum RESPONSE *rc)
{
	struct camera_entry *cam;
	const char *dev;
	int i, cam_idx = -1;

	dev = json_object_get_string(json_object_object_get(req, "device"));
	if (!dev) {
		lwsl_warn("%s: no camera device provided\n", __func__);
		*rc = RESPONSE_INVALID_REQUEST;
		return NULL;
	}

	if (camera_find_active(dev, &cam_idx)) {
		lwsl_info("%s: camera is already playing\n", __func__);
		*rc = RESPONSE_SEND_FRAMES;
		return NULL;
	}

	if (cam_idx < 0) {
		lwsl_info("%s: cannot support more than 32 cameras\n", __func__);
		*rc = RESPONSE_INTERNAL_ERROR;
		return NULL;
	}

	cam = &camera_active_list[cam_idx];

	cam->fd = open(dev, O_RDWR | O_CLOEXEC);
	if (cam->fd < 0) {
		*rc = RESPONSE_INTERNAL_ERROR;
		return NULL;
	}

	if (camera_set_capture_parameters(cam->fd) < 0)
		goto err;

	if (camera_request_buffers(cam->fd, NUM_MAX_CAPTURE_BUFS) < 0)
		goto err;

	for (i = 0; i < NUM_MAX_CAPTURE_BUFS; i++) {
		int sz = camera_query_buffer(cam->fd, i, &cam->buffers[i]);
		if (sz < 0)
			goto err;
		/* For now, we assume buffers are the same size */

		if (camera_enqueue_buffer(cam->fd, i) < 0)
			goto err;
	}

	if (camera_streaming_set_on(cam->fd, true) < 0)
		goto err;

	strncpy(cam->dev_name, dev, sizeof(cam->dev_name) - 1);
	json_object_object_add(req, "value", json_object_new_uint64(cam_idx));

	*rc = RESPONSE_SEND_FRAMES;

	return NULL;
err:
	*rc = RESPONSE_INTERNAL_ERROR;
	close(cam->fd);
	return NULL;
}

json_object *camera_dev_play_stop(json_object *req, enum RESPONSE *rc)
{
	struct camera_entry *cam;
	const char *dev;
	int i;

	dev = json_object_get_string(json_object_object_get(req, "device"));
	if (!dev) {
		*rc = RESPONSE_INVALID_REQUEST;
		return NULL;
	}

	cam = camera_find_active(dev, NULL);
	if (!cam) {
		*rc = RESPONSE_INVALID_REQUEST;
		return NULL;
	}

	camera_streaming_set_on(cam->fd, false);

	cam->dev_name[0] = '\0';
	close(cam->fd);

	for (i = 0; i < NUM_MAX_CAPTURE_BUFS; i++)
		munmap(cam->buffers[i].ptr, cam->buffers[i].length);

	*rc = RESPONSE_STOP_CAPTURE;

	return NULL;
}

const uint8_t *camera_dev_acquire_capture_buffer(int cam_idx, int *buf_id, size_t *buf_len)
{
	struct camera_entry *cam;

	if (cam_idx < 0 || cam_idx >= NUM_MAX_CAMERAS) {
		lwsl_err("%s: camera index out of range: %d\n", __func__, cam_idx);
		return NULL;
	}

	cam = &camera_active_list[cam_idx];
	if (cam->dev_name[0] == '\0') {
		lwsl_err("%s: inactive camera for index %d\n", __func__, cam_idx);
		return NULL;
	}

	if (buf_len)
		*buf_len = 0;

	*buf_id = camera_dequeue_buffer(cam->fd, buf_len);

	if (*buf_id < 0)
		return NULL;

	return cam->buffers[*buf_id].ptr;
}

void camera_dev_release_capture_buffer(int cam_idx, int buf_id)
{
	struct camera_entry *cam;

	if (cam_idx < 0 || cam_idx >= NUM_MAX_CAMERAS) {
		lwsl_err("%s: camera index out of range: %d\n", __func__, cam_idx);
		return;
	}

	if (buf_id < 0 || buf_id >= NUM_MAX_CAPTURE_BUFS) {
		lwsl_err("%s: buffer index out of range: %d\n", __func__, buf_id);
		return;
	}

	cam = &camera_active_list[cam_idx];
	if (cam->dev_name[0] == '\0') {
		lwsl_err("%s: inactive camera for index %d\n", __func__, cam_idx);
		return;
	}

	camera_enqueue_buffer(cam->fd, buf_id);
}


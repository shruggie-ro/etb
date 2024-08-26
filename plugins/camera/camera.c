
#include "camera.h"

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

struct camera_entry {
	char dev_name[DEV_NAME_MAX_SIZE];
	struct camera_buffer buffers[NUM_MAX_CAPTURE_BUFS];
	int fd;
};

/* FIXME: add mutex when adding threads */
static struct camera_entry camera_active_list[NUM_MAX_CAMERAS] = {};

static int xioctl(int fd, int request, void* arg)
{
	int r;
	do {
		r = ioctl(fd, request, arg);
	} while (r < 0 && EINTR == errno);
	return r;
}

static int camera_set_capture_parameters(int fd, int width, int height)
{
	struct v4l2_streamparm setfps = {};
	struct v4l2_format fmt = {};

	/* FIXME: hard-coded for now */
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		lwsl_err("ioctl(VIDIOC_S_FMT): %s\n", strerror(errno));
		return -1;
	}

	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	setfps.parm.capture.timeperframe.numerator = 1;
	setfps.parm.capture.timeperframe.denominator = 30;

	if (xioctl(fd, VIDIOC_S_PARM, &setfps) < 0) {
		lwsl_warn("ioctl(VIDIOC_S_PARM): %s\n", strerror(errno));
		return 0;
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

static int camera_dequeue_buffer(int fd) {
	struct v4l2_buffer buf = {};

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;

	if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
		lwsl_err("ioctl(VIDIOC_QBUF): %s\n", strerror(errno));
		return -1;
	}

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
	cam_buf->id = index;

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

static json_object* camera_resolutions_get(int fd)
{
	json_object* resolutions;
	struct v4l2_fmtdesc fmt;
	struct v4l2_frmsizeenum frmsize;

	resolutions = json_object_new_array();
	if (!resolutions)
		return NULL;

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
		if (fmt.pixelformat != V4L2_PIX_FMT_YUYV) {
			lwsl_warn("Ignoring pixel format: %08X, %s", fmt.pixelformat,
				  (const char *)fmt.description);
			fmt.index++;
			continue;
		}

		memset(&frmsize, 0, sizeof(frmsize));
		frmsize.pixel_format = fmt.pixelformat;

		while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
			json_object *res;

			if (frmsize.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
				frmsize.index++;
				lwsl_warn("Currently only discrete frame-sizes supported\n");
				continue;
			}

			res = json_object_new_object();
			if (!res)
				goto error;

			json_object_object_add(res, "width", json_object_new_int(frmsize.discrete.width));
			json_object_object_add(res, "height", json_object_new_int(frmsize.discrete.height));
			json_object_array_add(resolutions, res);

			frmsize.index++;
		}

		fmt.index++;
	}

	return resolutions;

error:
	json_object_put(resolutions);
	return NULL;
}

/* Public functions defined from here on */

int camera_devices_get(json_object *req)
{
	char dev_name[DEV_NAME_MAX_SIZE];
	struct v4l2_capability caps;
	json_object *arr;
	int i, fd, ret;
	const char *err = NULL;

	arr = json_object_new_array();
	if (!arr) {
		err = "error creating JSON array";
		goto err;
	}

	for (i = 0; i < 999; i++) {
		json_object *e;
		snprintf(dev_name, sizeof(dev_name), "/dev/video%d", i);

		fd = open(dev_name, O_RDWR);
		if (fd < 0)
			break;

		memset(&caps, 0, sizeof(caps));
		ret = xioctl(fd, VIDIOC_QUERYCAP, &caps);
		if (ret < 0) {
			close(fd);
			continue;
		}

		e = json_object_new_object();
		if (!e) {
			close(fd);
			continue;
		}

		json_object_object_add(e, "device", json_object_new_string(dev_name));
		json_object_object_add(e, "driver", json_object_new_string((char *)caps.driver));
		json_object_object_add(e, "card", json_object_new_string((char *)caps.card));
		json_object_object_add(e, "version", json_object_new_int(caps.version));
		/* FIXME: maybe we stringify these below? */
		json_object_object_add(e, "capabilities", json_object_new_int(caps.capabilities));;
		json_object_object_add(e, "device_caps", json_object_new_int(caps.device_caps));

		json_object_object_add(e, "resolutions", camera_resolutions_get(fd));

		json_object_array_add(arr, e);

		close(fd);
	}

	json_object_object_add(req, "value", arr);

	return 0;
err:
	json_object_object_add(req, "error", json_object_new_string(err));
	lwsl_err("%s: %s\n", __func__, err);
	return -1;
}

int camera_dev_play_start(json_object *req)
{
	struct camera_entry *cam = NULL;
	json_object *jval, *jres;
	const char *dev;
	int ibuf, cam_id = -1;
	int width, height;
	const char *err = NULL;

	jval = json_object_object_get(req, "value");
	dev = json_object_get_string(json_object_object_get(jval, "device"));
	if (!dev) {
		err = "no camera device provided";
		goto err_msg;
	}

	if (camera_find_active(dev, &cam_id)) {
		err = "camera is already playing";
		goto err_msg;
	}

	if (cam_id < 0) {
		err = "cannot support more than 32 cameras";
		goto err_msg;
	}

	cam = &camera_active_list[cam_id];

	cam->dev_name[0] = '\0';
	cam->fd = open(dev, O_RDWR | O_CLOEXEC);
	if (cam->fd < 0) {
		err = "error opening socket to device";
		goto err_cam_inactive;
	}

	jres = json_object_object_get(jval, "resolution");
	width = json_object_get_int(json_object_object_get(jres, "width"));
	height = json_object_get_int(json_object_object_get(jres, "height"));

	if (!width || !height) {
		lwsl_warn("Invalid width/height; using default 640x480\n");
		width = 640;
		height = 480;
	}

	if (camera_set_capture_parameters(cam->fd, width, height) < 0) {
		err = "error configuring camera parameters";
		goto err_close_fd;
	}

	if (camera_request_buffers(cam->fd, NUM_MAX_CAPTURE_BUFS) < 0) {
		err = "error requesting capture buffers";
		goto err_close_fd;
	}

	for (ibuf = 0; ibuf < NUM_MAX_CAPTURE_BUFS; ibuf++) {
		struct camera_buffer *buf = &cam->buffers[ibuf];
		int sz = camera_query_buffer(cam->fd, ibuf, buf);
		if (sz < 0) {
			err = "error querying buffer";
			goto err_free_bufs;
		}
		/* For now, we assume buffers are the same size */

		if (camera_enqueue_buffer(cam->fd, ibuf) < 0) {
			err = "error enqueuing buffer";
			goto err_free_bufs;
		}

		buf->width = width;
		buf->height = height;
	}

	ibuf -= 1; /* in case we need to unwind */
	if (camera_streaming_set_on(cam->fd, true) < 0) {
		err = "failed to enable streaming";
		goto err_free_bufs;
	}

	strncpy(cam->dev_name, dev, sizeof(cam->dev_name) - 1);
	json_object_object_add(req, "value", json_object_new_int(cam_id));

	return cam_id;

err_free_bufs:
	for (; ibuf >= 0; ibuf--) {
		munmap(cam->buffers[ibuf].ptr, cam->buffers[ibuf].length);
	}
err_close_fd:
	close(cam->fd);
err_cam_inactive:
	cam->dev_name[0] = '\0';
err_msg:
	lwsl_err("%s: %s\n", __func__, err);
	json_object_object_add(req, "error", json_object_new_string(err));

	return -1;
}

static void camera_dev_play_stop(struct camera_entry *cam)
{
	int i;

	if (cam->dev_name[0] == '\0')
		return;

	camera_streaming_set_on(cam->fd, false);

	cam->dev_name[0] = '\0';
	close(cam->fd);
	cam->fd = -1;

	for (i = 0; i < NUM_MAX_CAPTURE_BUFS; i++)
		munmap(cam->buffers[i].ptr, cam->buffers[i].length);
}

void camera_dev_play_stop_by_id(int cam_id)
{
	struct camera_entry *cam;

	if (cam_id < 0 || cam_id >= NUM_MAX_CAMERAS)
		return;

	cam = &camera_active_list[cam_id];

	camera_dev_play_stop(cam);
}

void camera_dev_play_stop_req(json_object *req)
{
	struct camera_entry *cam;
	json_object *jval;
	const char *dev;

	jval = json_object_object_get(req, "value");
	dev = json_object_get_string(json_object_object_get(jval, "device"));
	if (!dev)
		return;

	cam = camera_find_active(dev, NULL);
	if (!cam)
		return;

	camera_dev_play_stop(cam);
}

int camera_dev_acquire_capture_buffer(int cam_id, struct camera_buffer *buf)
{
	struct camera_entry *cam;
	int buf_id;

	if (cam_id < 0 || cam_id >= NUM_MAX_CAMERAS) {
		lwsl_err("%s: camera index out of range: %d\n", __func__, cam_id);
		return -1;
	}

	cam = &camera_active_list[cam_id];
	if (cam->dev_name[0] == '\0') {
		lwsl_err("%s: inactive camera for index %d\n", __func__, cam_id);
		return -1;
	}

	buf_id = camera_dequeue_buffer(cam->fd);
	if (buf_id < 0)
		return -1;

	memcpy(buf, &cam->buffers[buf_id], sizeof(*buf));

	return 0;
}

void camera_dev_release_capture_buffer(int cam_id, struct camera_buffer *buf)
{
	struct camera_entry *cam;

	if (cam_id < 0 || cam_id >= NUM_MAX_CAMERAS) {
		lwsl_err("%s: camera index out of range: %d\n", __func__, cam_id);
		return;
	}

	if (!buf) {
		lwsl_err("%s: NULL buffer object\n", __func__);
		return;
	}

	if (buf->id >= NUM_MAX_CAPTURE_BUFS) {
		lwsl_err("%s: buffer index out of range: %u\n", __func__, buf->id);
		return;
	}

	cam = &camera_active_list[cam_id];
	if (cam->dev_name[0] == '\0') {
		lwsl_err("%s: inactive camera for index %d\n", __func__, cam_id);
		return;
	}

	camera_enqueue_buffer(cam->fd, buf->id);
}


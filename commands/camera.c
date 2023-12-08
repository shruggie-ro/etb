
#include "internal_handlers.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <linux/videodev2.h>

#define DEV_NAME_MAX_SIZE	sizeof("/dev/video999")

static int xioctl(int fd, int request, void* arg)
{
	int r;
	do {
		r = ioctl(fd, request, arg);
	} while (r < 0 && EINTR == errno);
	return r;
}

json_object *camera_devices_get(json_object *)
{
	char dev_name[DEV_NAME_MAX_SIZE];
	struct v4l2_capability caps;
	json_object *cam_arr;
	int i, fd, ret;

	cam_arr = json_object_new_array();
	if (!cam_arr)
		return NULL;

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

	return cam_arr;
}


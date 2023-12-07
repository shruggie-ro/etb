
#include "internal_handlers.h"

#include <string.h>
#include <stdio.h>
#include <fcntl.h>

json_object* camera_get_dev_names(json_object *)
{
	char dev_name[] = "/dev/video999";
	json_object *cam_arr;
	int i, fd;

	cam_arr = json_object_new_array();
	if (!cam_arr)
		return NULL;

	for (i = 0; i < 999; i++) {
		snprintf(dev_name, sizeof(dev_name), "/dev/video%d", i);

		fd = open(dev_name, O_RDWR);
		if (fd < 0)
			break;

		json_object_array_add(cam_arr, json_object_new_string(dev_name));
	}

	return cam_arr;
}


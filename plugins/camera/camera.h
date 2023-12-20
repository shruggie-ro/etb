#ifndef __CAMERA_H__
#define __CAMERA_H__

#include <json-c/json.h>

struct camera_buffer {
	uint8_t *ptr;
	size_t length;
	uint32_t id;
};

int camera_devices_get(json_object *req);
int camera_dev_play_start(json_object *req);
void camera_dev_play_stop_req(json_object *req);
void camera_dev_play_stop_by_id(int cam_id);

int camera_dev_acquire_capture_buffer(int cam_id, struct camera_buffer *buf);
void camera_dev_release_capture_buffer(int cam_id, struct camera_buffer *buf);

#endif /* __CAMERA_H__ */

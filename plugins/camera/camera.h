#ifndef __CAMERA_H__
#define __CAMERA_H__

#include <json-c/json.h>

int camera_devices_get(json_object *req);
int camera_dev_play_start(json_object *req);
void camera_dev_play_stop(json_object *req);

const uint8_t *camera_dev_acquire_capture_buffer(int cam_idx, int *buf_id, size_t *buf_len);
void camera_dev_release_capture_buffer(int cam_idx, int buf_id);

#endif /* __CAMERA_H__ */

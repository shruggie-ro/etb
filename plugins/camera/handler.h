#ifndef __HANDLER_H__
#define __HANDLER_H__

#include <json-c/json.h>

enum RESPONSE {
	RESPONSE_SEND_BACK_JSON,
	RESPONSE_SEND_FRAMES,
	RESPONSE_STOP_CAPTURE,
	RESPONSE_INTERNAL_ERROR,
	RESPONSE_INVALID_DEVICE,
	RESPONSE_INVALID_REQUEST,
	RESPONSE_UNKNOWN_REQUEST,
};

json_object *camera_devices_get(json_object *req, enum RESPONSE *rc);
json_object *camera_dev_play_start(json_object *req, enum RESPONSE *rc);
json_object *camera_dev_play_stop(json_object *req, enum RESPONSE *rc);

const uint8_t *camera_dev_acquire_capture_buffer(int cam_idx, int *buf_id, size_t *buf_len);
void camera_dev_release_capture_buffer(int cam_idx, int buf_id);

enum RESPONSE camera_command_handler(json_object *req);

#endif /* __HANDLER_H__ */

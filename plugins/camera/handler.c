
#include <string.h>

#include "handler.h"

struct handler {
	const char *name;
	json_object *(*callback)(json_object *, enum RESPONSE *);
};

static const struct handler handlers[] = {
	{ "camera-devices-get", camera_devices_get },
	{ "camera-device-play", camera_dev_play_start },
	{ "camera-device-stop", camera_dev_play_stop },
	{}
};

enum RESPONSE camera_command_handler(json_object *req)
{
	enum RESPONSE resp_code = RESPONSE_INTERNAL_ERROR;
	json_object *resp_data, *req_data;
	const char *s;
	int i;

	if (!req)
		return RESPONSE_INVALID_REQUEST;

	s = json_object_get_string(json_object_object_get(req, "name"));
	if (!s)
		return RESPONSE_INVALID_REQUEST;

	for (i = 0; handlers[i].name; i++) {
		if (strcmp(s, handlers[i].name))
			continue;

		req_data = json_object_object_get(req, "value");
		resp_data = handlers[i].callback(req_data, &resp_code);

		if (resp_data)
			json_object_object_add(req, "value", resp_data);

		return resp_code;
	}

	return RESPONSE_UNKNOWN_REQUEST;
}


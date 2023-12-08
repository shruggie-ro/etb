
#include <string.h>

#include "handler.h"
#include "internal_handlers.h"

struct handler {
	const char *name;
	json_object *(*callback)(json_object *);
};

static const struct handler handlers[] = {
	{ "camera-devices-get", camera_devices_get },
	{}
};

json_object *command_handler(json_object *req)
{
	json_object *resp, *data;
	const char *s;
	int i;

	if (!req)
		return NULL;

	s = json_object_get_string(json_object_object_get(req, "name"));
	if (!s)
		return NULL;

	for (i = 0; handlers[i].name; i++) {
		if (strcmp(s, handlers[i].name))
			continue;
		resp = json_object_new_object();
		if (!resp)
			return NULL;

		/* FIXME: may need to address if 'data' being NULL means error */
		data = handlers[i].callback(json_object_object_get(req, "value"));
		json_object_object_add(resp, "name", json_object_new_string(s));
		json_object_object_add(resp, "value", data);

		return resp;
	}

	return NULL;
}


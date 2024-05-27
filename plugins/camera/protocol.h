#ifndef __PROTOCOL_CAMERA_H__
#define __PROTOCOL_CAMERA_H__

#include <stdbool.h>
#include <libwebsockets.h>
#include "jpeg.h"

/* FIXME: abstract this better */

int callback_camera(struct lws *wsi, enum lws_callback_reasons reason,
		     void *user, void *in, size_t len);

struct per_session_data__camera {
	struct lws_ring *ring;
	uint32_t msglen;
	uint32_t tail;
	int cam_id;
	tjhandle tjpeg_handle;
	uint8_t flow_controlled:1;
	struct lws *wsi;
};

#define LWS_PLUGIN_PROTOCOL_CAMERA \
	{ \
		"camera", \
		callback_camera, \
                sizeof(struct per_session_data__camera), \
                1024, \
                0, NULL, 0 \
        }

#endif /* __PROTOCOL_CAMERA_H__ */

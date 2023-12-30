#ifndef __PROTOCOL_DRPAI_H__
#define __PROTOCOL_DRPAI_H__

#include <stdbool.h>
#include <libwebsockets.h>
#include "drpai.h"

/* FIXME: abstract this better */

int callback_drpai(struct lws *wsi, enum lws_callback_reasons reason,
		   void *user, void *in, size_t len);

struct per_session_data__drpai {
	struct lws_ring *ring;
	uint32_t msglen;
	uint32_t tail;
	struct drpai *drpai;
	uint8_t flow_controlled:1;
	uint8_t write_consume_pending:1;
};

#define LWS_PLUGIN_PROTOCOL_DRPAI \
	{ \
		"drpai", \
		callback_drpai, \
                sizeof(struct per_session_data__drpai), \
                1024, \
                0, NULL, 0 \
        }

#endif /* __PROTOCOL_DRPAI_H__ */

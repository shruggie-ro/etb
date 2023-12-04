#ifndef __PROTOCOL_COMMAND_H__
#define __PROTOCOL_COMMAND_H__

#include <libwebsockets.h>

/* FIXME: abstract this better */

int callback_command(struct lws *wsi, enum lws_callback_reasons reason,
		     void *user, void *in, size_t len);

struct per_session_data__command {
	struct lws_ring *ring;
	uint32_t msglen;
	uint32_t tail;
	uint8_t completed:1;
	uint8_t flow_controlled:1;
	uint8_t write_consume_pending:1;
};

#define LWS_PLUGIN_PROTOCOL_COMMAND \
	{ \
		"command", \
		callback_command, \
                sizeof(struct per_session_data__command), \
                1024, \
                0, NULL, 0 \
        }

#endif /* __PROTOCOL_COMMAND_H__ */

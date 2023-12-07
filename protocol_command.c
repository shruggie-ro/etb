
#include <libwebsockets.h>
#include <stdbool.h>
#include <string.h>
#include <json-c/json.h>

#include "protocol_command.h"
#include "commands/handler.h"

#define RING_DEPTH 4096

/* one of these created for each message */

struct msg {
	json_object *response;
	uint8_t *send_buf;
};

struct vhd_command {
	struct lws_context *context;
	struct lws_vhost *vhost;
};

static void __destroy_message(void *_msg)
{
	struct msg *msg = _msg;

	json_object_put(msg->response);
	msg->response = NULL;

	free(msg->send_buf);
	msg->send_buf = NULL;
}

int callback_command(struct lws *wsi, enum lws_callback_reasons reason,
		     void *user, void *in, size_t len)
{
	struct per_session_data__command *pss = user;
	struct vhd_command *vhd = lws_protocol_vh_priv_get(lws_get_vhost(wsi),
							   lws_get_protocol(wsi));
	json_object *req, *resp;
	struct msg *pmsg;
	struct msg amsg;
	int m, n, flags;
	bool first, final;
	const char *s;
	size_t slen;

	switch (reason) {

	case LWS_CALLBACK_PROTOCOL_INIT:
		vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
				lws_get_protocol(wsi),
				sizeof(struct vhd_command));
		if (!vhd)
			return -1;

		vhd->context = lws_get_context(wsi);
		vhd->vhost = lws_get_vhost(wsi);

		lwsl_warn("command: protocol initialized\n");
		break;

	case LWS_CALLBACK_ESTABLISHED:
		lwsl_warn("command: client connected\n");
		pss->ring = lws_ring_create(sizeof(struct msg), RING_DEPTH,
					    __destroy_message);
		if (!pss->ring)
			return 1;
		pss->tail = 0;
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:

		lwsl_warn("LWS_CALLBACK_SERVER_WRITEABLE\n");

		if (pss->write_consume_pending) {
			/* perform the deferred fifo consume */
			lws_ring_consume_single_tail(pss->ring, &pss->tail, 1);
			pss->write_consume_pending = 0;
		}

		pmsg = (struct msg*)lws_ring_get_element(pss->ring, &pss->tail);
		if (!pmsg) {
			lwsl_warn(" (nothing in ring)\n");
			break;
		}
		/* should not happen, because we queue validated JSON objects */
		s = json_object_to_json_string_length(pmsg->response, 0, &slen);
		if (!s) {
			lwsl_warn(" (invalid json response)\n");
			break;
		}
		n = slen;
		pmsg->send_buf = malloc(n + LWS_PRE);
		if (!pmsg->send_buf) {
			lwsl_warn(" (could not allocate send buffer)\n");
			break;
		}
		memcpy(pmsg->send_buf + LWS_PRE, s, n);

		// FIXME: hardcoded
		flags = lws_write_ws_flags(LWS_WRITE_TEXT, 1, 1);

		m = lws_write(wsi, pmsg->send_buf + LWS_PRE, n, flags);
		if (m < n) {
			lwsl_err("ERROR %d writing to ws socket\n", m);
			return -1;
		}

		lwsl_warn(" wrote %d: flags: 0x%x\n", m, flags);
		//lwsl_warn(" wrote %d: flags: 0x%x first: %d final %d\n",
		//		m, flags, pmsg->first, pmsg->final);
		/*
		 * Workaround deferred deflate in pmd extension by only
		 * consuming the fifo entry when we are certain it has been
		 * fully deflated at the next WRITABLE callback.  You only need
		 * this if you're using pmd.
		 */
		pss->write_consume_pending = 1;
		lws_callback_on_writable(wsi);

		if (pss->flow_controlled &&
		    (int)lws_ring_get_count_free_elements(pss->ring) > RING_DEPTH - 5) {
			lws_rx_flow_control(wsi, 1);
			pss->flow_controlled = 0;
		}

		break;

	case LWS_CALLBACK_RECEIVE:

		lwsl_warn("LWS_CALLBACK_RECEIVE: %4d (rpp %5d, first %d, "
			  "last %d, bin %d, msglen %d (+ %d = %d))\n",
			  (int)len, (int)lws_remaining_packet_payload(wsi),
			  lws_is_first_fragment(wsi),
			  lws_is_final_fragment(wsi),
			  lws_frame_is_binary(wsi), pss->msglen, (int)len,
			  (int)pss->msglen + (int)len);

		if (len) {
			//puts((const char *)in);
			//lwsl_hexdump_notice(in, len);
		}

		if (lws_frame_is_binary(wsi)) {
			lwsl_warn("command: got binary data; dropping\n");
			break;
		}

		n = lws_ring_get_count_free_elements(pss->ring);
		if (!n) {
			lwsl_warn("dropping!\n");
			break;
		}

		first = lws_is_first_fragment(wsi);
		final = lws_is_final_fragment(wsi);

		resp = NULL;
		// FIXME: for now, we support only single-complete messages here
		if (first && final) {
			const char *s = in;

			req = json_tokener_parse(s);
			resp = command_handler(req);
			json_object_put(req);
		}

		if (resp) {
			amsg.response = resp;
			if (!lws_ring_insert(pss->ring, &amsg, 1)) {
				__destroy_message(&amsg);
				lwsl_warn("dropping!\n");
				break;
			}

			lws_callback_on_writable(wsi);
		}

		if (final)
			pss->msglen = 0;
		else
			pss->msglen += (uint32_t)len;

		if (n < 3 && !pss->flow_controlled) {
			pss->flow_controlled = 1;
			lws_rx_flow_control(wsi, 0);
		}
		break;

	case LWS_CALLBACK_CLOSED:
		lwsl_warn("command: client disconnected\n");
		lws_ring_destroy(pss->ring);
		break;

	default:
		break;
	}

	return 0;
}


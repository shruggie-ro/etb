
#include <libwebsockets.h>
#include <stdbool.h>
#include <string.h>
#include <json-c/json.h>

#include "protocol.h"
#include "drpai.h"

#define RING_DEPTH 4096

bool drpai_active = false;
struct drpai *drpai = NULL;

enum command {
	CMD_INVALID = -1,
	CMD_MODELS_GET,
	CMD_MODEL_UPLOAD,
	CMD_MODEL_START,
	CMD_MODEL_STOP,
	CMD_MODEL_DELETE,
	CMD_MAX,
};

static const char *command_names[] = {
	[CMD_MODELS_GET]   = "drpai-models-get",
	[CMD_MODEL_UPLOAD] = "drpai-model-upload",
	[CMD_MODEL_START]  = "drpai-model-start",
	[CMD_MODEL_STOP]   = "drpai-model-stop",
	[CMD_MODEL_DELETE] = "drpai-model-delete",
};

struct msg {
	json_object *response;
	uint8_t *send_buf;
};

struct vhd_drpai {
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

static enum command protocol_get_command_enum(const char *cmd)
{
	int i;

	if (!cmd)
		return CMD_INVALID;

	for (i = 0; i < CMD_MAX; i++) {
		if (strcmp(cmd, command_names[i]) == 0)
			return i;
	}

	return CMD_INVALID;
}

static int protocol_handle_incoming(struct lws *wsi, struct per_session_data__drpai *pss,
				    void *in, size_t len)
{
	json_object *req = NULL;
	bool first, final;
	enum command cmd = CMD_INVALID;
	bool send_req_back_as_reply = false;

	first = lws_is_first_fragment(wsi);
	final = lws_is_final_fragment(wsi);

	// FIXME: for now, we support only single-complete messages here
	if (first && final) {
		const char *s = in;

		req = json_tokener_parse(s);
		s = json_object_get_string(json_object_object_get(req, "name"));

		cmd = protocol_get_command_enum(s);
	}

	switch (cmd) {
		case CMD_MODELS_GET:
			send_req_back_as_reply = true;
			drpai_models_get(req);
			break;
		case CMD_MODEL_START:
			if (drpai_load_model(pss->drpai, req) == 0)
				drpai_active = true;
			break;
		case CMD_MODEL_STOP:
			drpai_active = false;
			break;
		default:
			break;
	}

	if (send_req_back_as_reply) {
		struct msg amsg;
		amsg.response = req;
		amsg.send_buf = NULL;
		if (!lws_ring_insert(pss->ring, &amsg, 1)) {
			__destroy_message(&amsg);
			lwsl_warn("dropping!\n");
			return -1;
		}

		json_object_get(req);
		lws_callback_on_writable(wsi);
	}

	json_object_put(req);

	if (final)
		pss->msglen = 0;
	else
		pss->msglen += (uint32_t)len;

	return 0;
}

static int handle_outgoing_message(struct lws *wsi, struct per_session_data__drpai *pss)
{
	struct msg *pmsg;
	int m, n, flags;
	const char *s;
	size_t slen;

	pmsg = (struct msg*)lws_ring_get_element(pss->ring, &pss->tail);
	if (!pmsg) {
		lwsl_info(" (nothing in ring)\n");
		return -1;
	}

	/* should not happen, because we queue validated JSON objects */
	s = json_object_to_json_string_length(pmsg->response, 0, &slen);
	if (!s) {
		lwsl_warn(" (invalid json response)\n");
		return -1;
	}

	n = slen;
	pmsg->send_buf = malloc(n + LWS_PRE);
	if (!pmsg->send_buf) {
		lwsl_warn(" (could not allocate send buffer)\n");
		return -1;
	}

	memcpy(pmsg->send_buf + LWS_PRE, s, n);

	// FIXME: hardcoded
	flags = lws_write_ws_flags(LWS_WRITE_TEXT, 1, 1);

	m = lws_write(wsi, pmsg->send_buf + LWS_PRE, n, flags);
	if (m < n) {
		lwsl_err("ERROR %d writing to ws socket\n", m);
		return -1;
	}

	lwsl_debug(" wrote %d: flags: 0x%x\n", m, flags);

	return 0;
}

int callback_drpai(struct lws *wsi, enum lws_callback_reasons reason,
		   void *user, void *in, size_t len)
{
	struct per_session_data__drpai *pss = user;
	struct vhd_drpai *vhd = lws_protocol_vh_priv_get(lws_get_vhost(wsi),
							 lws_get_protocol(wsi));
	int n, rc;

	switch (reason) {

	case LWS_CALLBACK_PROTOCOL_INIT:
		vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
				lws_get_protocol(wsi),
				sizeof(struct vhd_drpai));
		if (!vhd)
			return -1;

		vhd->context = lws_get_context(wsi);
		vhd->vhost = lws_get_vhost(wsi);

		lwsl_info("drpai: protocol initialized\n");
		break;

	case LWS_CALLBACK_ESTABLISHED:
		lwsl_info("drpai: client connected\n");
		pss->ring = lws_ring_create(sizeof(struct msg), RING_DEPTH,
					    __destroy_message);
		if (!pss->ring)
			return 1;

		pss->drpai = drpai_init(&rc);
		if (!pss->drpai) {
			lwsl_warn("%s: could not initialize DRP AI: %d\n",
				  __func__, rc);
			return 1;
		}
		// FIXME: hack
		drpai = pss->drpai;

		pss->tail = 0;
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:

		lwsl_debug("LWS_CALLBACK_SERVER_WRITEABLE\n");

		if (pss->write_consume_pending) {
			/* perform the deferred fifo consume */
			lws_ring_consume_single_tail(pss->ring, &pss->tail, 1);
			pss->write_consume_pending = 0;
		}

		if ((handle_outgoing_message(wsi, pss) < 0))
			break;

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

		lwsl_debug("LWS_CALLBACK_RECEIVE: %4d (rpp %5d, first %d, "
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
			lwsl_warn("drpai: got binary data; dropping\n");
			break;
		}

		n = lws_ring_get_count_free_elements(pss->ring);
		if (!n) {
			lwsl_warn("dropping!\n");
			break;
		}

		if (protocol_handle_incoming(wsi, pss, in, len))
			break;

		if (n < 3 && !pss->flow_controlled) {
			pss->flow_controlled = 1;
			lws_rx_flow_control(wsi, 0);
		}

		break;

	case LWS_CALLBACK_CLOSED:
		lwsl_info("drpai: client disconnected\n");
		drpai_free(pss->drpai);
		pss->drpai = NULL;
		// FIXME: hack
		drpai = NULL;
		lws_ring_destroy(pss->ring);
		break;

	default:
		break;
	}

	return 0;
}


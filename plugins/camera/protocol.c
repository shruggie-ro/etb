
#include <libwebsockets.h>
#include <stdbool.h>
#include <string.h>
#include <json-c/json.h>

#include "protocol.h"
#include "camera.h"
#include "../drpai/drpai.h"

#define RING_DEPTH 4096

#define VIDEO_STREAM_ID_SIZE	16

/* one of these created for each message */

enum command {
	CMD_INVALID = -1,
	CMD_DEVICES_GET = 0,
	CMD_DEVICE_PLAY,
	CMD_DEVICE_STOP,
	CMD_MAX,
};

static const char *command_names[] = {
	[CMD_DEVICES_GET] = "camera-devices-get",
	[CMD_DEVICE_PLAY] = "camera-device-play",
	[CMD_DEVICE_STOP] = "camera-device-stop",
};

struct msg {
	uint8_t *send_buf;
	int send_buf_len;
	int flags;
};

struct vhd_camera {
	struct lws_context *context;
	struct lws_vhost *vhost;
};

static void __destroy_message(void *_msg)
{
	struct msg *msg = _msg;

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

static int queue_json_message(struct lws *wsi, struct per_session_data__camera *pss,
			      json_object* jo)
{
	struct msg amsg = {};
	const char *s;
	size_t slen;
	int ret;

	/* should not happen, because we queue validated JSON objects */
	s = json_object_to_json_string_length(jo, 0, &slen);
	if (!s) {
		lwsl_warn(" (invalid json object)\n");
		return -1;
	}

	amsg.send_buf_len = slen;
	amsg.send_buf = malloc(slen + LWS_PRE);
	if (!amsg.send_buf) {
		lwsl_warn(" (could not allocate send buffer for json message)\n");
		return -1;
	}

	memcpy(amsg.send_buf + LWS_PRE, s, slen);
	amsg.flags = lws_write_ws_flags(LWS_WRITE_TEXT, 1, 1);

	ret = lws_ring_insert(pss->ring, &amsg, 1);

	if (!ret) {
		lwsl_warn(" (could insert message in ring)\n");
		return -1;
	}

	return 0;
}

static int queue_video_stream(struct lws *wsi, const char *stream_id,
			      struct per_session_data__camera *pss,
			      uint8_t* jpeg_buf, int jpeg_buflen)
{
	struct msg amsg = {};
	char *s;
	int ret;

	amsg.send_buf_len = jpeg_buflen + VIDEO_STREAM_ID_SIZE;
	amsg.send_buf = malloc(amsg.send_buf_len + LWS_PRE);
	if (!amsg.send_buf) {
		lwsl_warn(" (could not allocate send buffer)\n");
		return -1;
	}

	s = (char *)(amsg.send_buf + LWS_PRE);
	strcpy(s, stream_id);
	memcpy(amsg.send_buf + LWS_PRE + VIDEO_STREAM_ID_SIZE, jpeg_buf, jpeg_buflen);

	// FIXME: hardcoded
	amsg.flags = lws_write_ws_flags(LWS_WRITE_BINARY, 1, 1);

	ret = lws_ring_insert(pss->ring, &amsg, 1);

	if (!ret) {
		lwsl_warn(" (could insert message in ring)\n");
		return -1;
	}

	return 0;
}

static int protocol_handle_incoming(struct lws *wsi, struct per_session_data__camera *pss,
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
		case CMD_DEVICES_GET:
			send_req_back_as_reply = true;
			camera_devices_get(req);
			break;
		case CMD_DEVICE_PLAY:
			pss->cam_id = camera_dev_play_start(req);
			if (pss->cam_id > -1)
				lws_callback_on_writable(wsi);
			else
				send_req_back_as_reply = true;
			break;
		case CMD_DEVICE_STOP:
			camera_dev_play_stop_req(req);
			pss->cam_id = -1;
			break;
		default:
			break;
	}

	if (send_req_back_as_reply) {
		if (queue_json_message(wsi, pss, req)) {
			json_object_put(req);
			lwsl_warn("dropping!\n");
			return -1;
		}
	}

	lws_callback_on_writable(wsi);
	json_object_put(req);

	if (final)
		pss->msglen = 0;
	else
		pss->msglen += (uint32_t)len;

	return 0;
}

static int handle_outgoing_message(struct lws *wsi, struct per_session_data__camera *pss)
{
	struct msg *pmsg;
	int w;

	pmsg = (struct msg*)lws_ring_get_element(pss->ring, &pss->tail);
	if (!pmsg) {
		lwsl_debug(" (nothing in ring)\n");
		return -1;
	}

	w = lws_write(wsi, pmsg->send_buf + LWS_PRE, pmsg->send_buf_len, pmsg->flags);
	if (w < pmsg->send_buf_len) {
		lwsl_err("ERROR %d writing json to ws socket %d\n", w, pmsg->send_buf_len);
		return -1;
	}

	lws_ring_consume_single_tail(pss->ring, &pss->tail, 1);

	lwsl_debug(" wrote %d: flags: 0x%x\n", w, pmsg->flags);

	return 0;
}

static int handle_video_drpai(struct lws *wsi, struct per_session_data__camera *pss,
			      void *buf, uint8_t* jpeg_buf, int jpeg_buflen)
{
	json_object *res;
	const char *err_msg = NULL;
	static int get_result = 0;

	struct drpai *d = drpai;

	if (!d || !drpai_active)
		return 0;

	if (get_result == 0) {
		err_msg = drpai_model_load_input(d, buf, DRPAI_BUF_LEN);
		if (err_msg) {
			lwsl_warn("drpai_model_load_input: %s\n", err_msg);
			goto out_send_err;
		}

		err_msg = drpai_model_start(drpai);
		if (err_msg) {
			lwsl_warn("drpai_model_start: %s\n", err_msg);
			goto out_send_err;
		}

		get_result = 1;
		// send a copy to the DRP AI canvas
		queue_video_stream(wsi, "drpai+camera", pss, jpeg_buf, jpeg_buflen);
		return 1;
	}

	if (drpai_is_running(d))
		return 0;

	res = json_object_new_object();
	err_msg = drpai_model_get_result(d, res);
	if (err_msg)
		goto out_send_err;

	if (err_msg) {
		json_object_object_add(res, "error",
				       json_object_new_string(err_msg));
	}

	queue_json_message(pss->wsi, pss, res);
	json_object_put(res);
	get_result = 0;

	return 0;

out_send_err:
	res = json_object_new_object();
	if (err_msg) {
		json_object_object_add(res, "error",
				       json_object_new_string(err_msg));
	}

	queue_json_message(wsi, pss, res);
	json_object_put(res);

	return 0;
}

static int handle_video_stream_out(struct lws *wsi, struct per_session_data__camera *pss)
{
	struct camera_buffer buf = {};
	uint8_t* jpeg_buf;
	size_t jpeg_buflen = 0;
	int sent_frame;

	if (camera_dev_acquire_capture_buffer(pss->cam_id, &buf)) {
		lwsl_err(" (got null buffer from camera)\n");
		return -1;
	}

	jpeg_buf = turbo_jpeg_compress(pss->tjpeg_handle, buf.ptr,
				       buf.width, buf.height,
				       2, 1, 75, &jpeg_buflen);
	if (!jpeg_buf) {
		lwsl_warn(" (could not compress jpeg)\n");
		return -1;
	}

	// FIXME: (hack) separate this nicer
	sent_frame = handle_video_drpai(wsi, pss, buf.ptr, jpeg_buf, jpeg_buflen);

	if (!sent_frame)
		queue_video_stream(wsi, "camera", pss, jpeg_buf, jpeg_buflen);

	tjFree(jpeg_buf);

	camera_dev_release_capture_buffer(pss->cam_id, &buf);

	return 0;
}

int callback_camera(struct lws *wsi, enum lws_callback_reasons reason,
		    void *user, void *in, size_t len)
{
	struct per_session_data__camera *pss = user;
	struct vhd_camera *vhd = lws_protocol_vh_priv_get(lws_get_vhost(wsi),
							   lws_get_protocol(wsi));
	int n;

	switch (reason) {

	case LWS_CALLBACK_PROTOCOL_INIT:
		vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
				lws_get_protocol(wsi),
				sizeof(struct vhd_camera));
		if (!vhd)
			return -1;

		vhd->context = lws_get_context(wsi);
		vhd->vhost = lws_get_vhost(wsi);

		lwsl_info("camera: protocol initialized\n");
		break;

	case LWS_CALLBACK_ESTABLISHED:
		lwsl_info("camera: client connected\n");
		pss->ring = lws_ring_create(sizeof(struct msg), RING_DEPTH,
					    __destroy_message);
		if (!pss->ring)
			return 1;

		/* FIXME: fallback to RAW? */
		pss->tjpeg_handle = tjInitCompress();
		if (!pss->tjpeg_handle) {
			lwsl_warn("%s: could not initialize turbo-jpeg: %s\n",
				  __func__, tjGetErrorStr());
			return -1;
		}

		pss->wsi = wsi;

		pss->cam_id = -1;
		pss->tail = 0;
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:

		lwsl_debug("LWS_CALLBACK_SERVER_WRITEABLE\n");

		if (pss->cam_id > -1)
			handle_video_stream_out(wsi, pss);

		while ((handle_outgoing_message(wsi, pss) == 0))
			;

		lws_callback_on_writable(wsi);

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
			lwsl_warn("camera: got binary data; dropping\n");
			break;
		}

		n = lws_ring_get_count_free_elements(pss->ring);
		if (!n) {
			lwsl_warn("dropping!\n");
			break;
		}

		if (protocol_handle_incoming(wsi, pss, in, len))
			break;

		break;

	case LWS_CALLBACK_CLOSED:
		lwsl_info("camera: client disconnected\n");
		camera_dev_play_stop_by_id(pss->cam_id);
		tjDestroy(pss->tjpeg_handle);
		lws_ring_destroy(pss->ring);
		break;

	default:
		break;
	}

	return 0;
}


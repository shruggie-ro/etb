
#include <string.h>
#include <stdio.h>

#include <libwebsockets.h>

#include "ws_server.h"
#include "plugins/camera/protocol.h"

#define LWS_PROTOCOL_HTTP_DEFAULT \
	{ "http", lws_callback_http_dummy, 0, 0, 0, NULL, 0}

#ifndef HTTP_ROOT
#error "Must define HTTP_ROOT, for the location of the files to serve"
#endif

struct ws_server {
	struct lws_context *context;
};

static struct lws_protocols protocols[] = {
	LWS_PROTOCOL_HTTP_DEFAULT,
	LWS_PLUGIN_PROTOCOL_CAMERA,
	LWS_PROTOCOL_LIST_TERM
};

static const struct lws_http_mount mount = {
	.mountpoint			= "/",			/* mountpoint URL */
	.origin				= "" HTTP_ROOT "",	/* serve from dir */
	.def				= "index.html",		/* default filename */
	.origin_protocol		= LWSMPRO_FILE,		/* files in a dir */
	.mountpoint_len			= 1,			/* char count */
};

int ws_server_init(struct ws_server **ws, int argc, const char *argv[])
{
	struct lws_context_creation_info info;

	if (!ws)
		return -EINVAL;

	*ws = calloc(1, sizeof(**ws));
	if (!*ws)
		return -ENOMEM;

	memset(&info, 0, sizeof(info));

	/* FIXME: hard-coded for now */
	info.port = 8000;
	info.mounts = &mount;
	info.protocols = protocols;
	info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
	info.gid = -1;
	info.uid = -1;

	(*ws)->context = lws_create_context(&info);

	return 0;
}

int ws_server_run(struct ws_server *ws)
{
	while(1)
	{
		lws_service(ws->context, /* timeout_ms = */ 3000);
	}

	return 0;
}

void ws_server_close(struct ws_server *ws)
{
	if (!ws)
		return;

	lws_context_destroy(ws->context);
	free(ws);
}

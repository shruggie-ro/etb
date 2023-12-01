
#include <string.h>
#include <stdio.h>

#include <libwebsockets.h>

#include "ws_server.h"

struct ws_server {
	struct lws_context *context;
};


static const struct lws_http_mount mount = {
	.mountpoint			= "/",			/* mountpoint URL */
	.origin				= "./client",		/* serve from dir */
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

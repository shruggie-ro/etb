
#include <stdio.h>

#include "ws_server.h"

int main(int argc, const char* argv[])
{
	struct ws_server *ws = NULL;
	int ret;

	ret = ws_server_init(&ws, argc, argv);
	if (ret)
		return ret;

	ret = ws_server_run(ws);

	ws_server_close(ws);

	return ret;
}

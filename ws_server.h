#ifndef __WS_SERVER_H__
#define __WS_SERVER_H__

/* Opaque type for a Websocket server instance */
struct ws_server;

int ws_server_init(struct ws_server **ws, int argc, const char *argv[]);

int ws_server_run(struct ws_server *ws);

void ws_server_close(struct ws_server *ws);

#endif /* __WS_SERVER_H__ */

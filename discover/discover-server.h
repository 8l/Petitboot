#ifndef _DISCOVER_SERVER_H
#define _DISCOVER_SERVER_H

struct discover_server;
struct device_handler;

struct discover_server *discover_server_init(void);

void discover_server_destroy(struct discover_server *server);

void discover_server_set_device_source(struct discover_server *server,
		struct device_handler *handler);

#endif /* _DISCOVER_SERVER_H */
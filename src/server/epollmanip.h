#ifndef TETRISH_TETRISD_EPOLLMANIP_H
#define TETRISH_TETRISD_EPOLLMANIP_H

#include "client_io.h"

void close_client(int epoll_fd, struct client_io *c);
struct client_io* add_client(int epoll_fd, int client_fd);
int handle_read(struct client_io *c);
int handle_write(int epoll_fd, struct client_io *c);

#endif
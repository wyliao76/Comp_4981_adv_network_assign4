// cppcheck-suppress-file unusedStructMember

#ifndef NETWORKING_H
#define NETWORKING_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

ssize_t convert_port(const char *str, in_port_t *port);
int     tcp_server(const char *address, in_port_t port, int backlog, int *err);
int     tcp_client(const char *address, in_port_t port, int *err);
int     setSocketNonBlocking(int socket, int *err);
int     setSocketBlocking(int socket, int *err);
int     send_fd(int socket, int fd, int fd_num);
int     recv_fd(int socket, int *fd_num);
ssize_t send_number(int socket, int fd_num);
ssize_t recv_number(int socket, int *fd_num);

#endif    // NETWORKING_H

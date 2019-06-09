#ifndef EPOLL
#define EPOLL
#include "requestData.h"

constexpr int MAXEVENTS = 5000;
constexpr int LISTENQ = 1024;

int epoll_init();
int epoll_add(int epoll_fd, int fd, void* request, __uint32_t events);
int epoll_mod(int epoll_fd, int fd, void* request, __uint32_t events);
int epoll_del(int epoll_fd, int fd, void* request, __uint32_t events);
int my_epoll_wait(int epoll_fd, struct epoll_event* events, int max_events, int timeout);

#endif
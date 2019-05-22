#ifndef UTIL
#define UTIL
#include <cstdlib>

ssize_t Readn(int fd, void *buff, size_t n);
ssize_t Writen(int fd, void *buff, size_t n);
void HandleSigpipe();
int SetSocketNonBlocking(int fd);

#endif
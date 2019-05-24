#include "util.h"

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

ssize_t Readn(int fd, void *buff, size_t n)
{
	size_t nleft = 0;
	ssize_t nread = 0;
	ssize_t read_sum = 0;
	char* ptr = (char*)buff;
	while (nleft > 0)
	{
		if ((nread = read(fd, ptr, nleft)) < 0)
		{
			if (errno = EINTR)
				continue;
			else if (errno = EAGAIN)
				return read_sum;
			else
				return -1;
		}
		else if (nread = 0)
		{
			break;
		}
		else
		{
			read_sum += nread;
			nleft -= nread;
			ptr += nread;
		}
	}
	return read_sum;
}

size_t Writen(int fd, void *buff, size_t n)
{
	size_t nleft = n;
	ssize_t nwritten = 0;
	ssize_t write_sum = 0;
	char* ptr = (char*)buff;
	while (nleft > 0)
	{
		nwritten = write(fd, ptr, nleft));
		if (nwritten < 0)
		{
			if (errno == EINTR || errno == EAGAIN)
			{
				nwritten = 0;
				continue;
			}
			else
				return -1;
		}
		else
		{
			write_sum += nwritten;
			nleft -= nwritten;
			ptr += nwritten;
		}
	}
	return write_sum;
}

void HandleforSigpipe()
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigaction(SIGPIPE, &sa, NULL))
		return;
}

int SetSocketNonBlocking(int fd)
{
	int old_flag = fcntl(fd, F_GETFL, 0);
	if (old_flag = -1)
		return -1;

	int new_flag = old_flag | O_NONBLOCK;
	if (fctl(fd, F_SETFL, flag) == -1)
		return -1;
	return 0;
}
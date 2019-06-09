#include "util.h"
#include "requestData.h"
#include "epoll.h"
#include "threadpool.h"

#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <queue>
#include <iostream>
#include <vector>

using namespace std;

constexpr int THREADPOOL_THREAD_NUM = 4;
constexpr int QUEUE_SIZE = 65535;

constexpr int PORT = 8888;
constexpr int ASK_STATIC_FILE = 1;
constexpr int ASK_IMAGE_STITCH = 2;

const string PATH = "/";

constexpr int TIMER_TIME_OUT = 500;

extern pthread_mutex_t qlock;
extern struct epoll_event* events;
void acceptConnection(int listen_fd, int epoll_fd, const string& path);

extern priority_queue<mytimer*, deque<mytimer*>, timerCmp> myTimerQueue;

int socket_bind_listen(int port)
{
	assert(port >= 1024 && port <= 65535);
	
	// 创建socket，返回监听描述符
	int listen_fd;
	if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		close(listen_fd);
		return -1;
	}

	// 设置sockopt,防止出现"address already in use"错误
	int optval = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
		return -1;

	// 设置服务器IP和PORT，使用bind绑定
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);
	if (bind(listen_fd, (struct sockaddr*) & server_addr, sizeof(server_addr)) == -1)
		return -1;

	// 监听
	if (listen(listen_fd, LISTENQ) == -1)
		return -1;
		
	return listen_fd;
}

void myHandler(void* args)
{
	requestData* req_data = (requestData*)args;
	req_data->handleRequest();
}

void acceptConnection(int listen_fd, int epoll_fd, const string& path)
{
	struct sockaddr_in client_addr;
	memset(&client_addr, 0, sizeof(client_addr));
	socklen_t client_addr_len = 0;
	int accept_fd = 0;
	while ((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0)
	{
		// set socket non-blocking
		int ret = SetSocketNonBlocking(accept_fd);
		assert(ret >= 0);

		requestData* req_info = new requestData(epoll_fd, accept_fd, path);

		// 文件描述符可以读，边缘触发(Edge Triggered)模式，保证一个socket连接在任一时刻只被一个线程处理
		__uint32_t _epoll_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
		epoll_add(epoll_fd, accept_fd, static_cast<void*>(req_info), _epoll_event);
		// 新增时间信息
		mytimer* mtimer = new mytimer(req_info, TIMER_TIME_OUT);
		req_info->addTimer(mtimer);
		pthread_mutex_lock(&qlock);
		myTimerQueue.push(mtimer);
		pthread_mutex_unlock(&qlock);
	}
}

// 分发处理函数
void handle_events(int epoll_fd, int listen_fd, struct epoll_event* events, int events_num, const string& path, threadpool_t* tp)
{
	for (int idx = 0; idx < events_num; ++idx)
	{
		// 获取监听到触发事件的描述符
		requestData* request = (requestData*)(events[idx].data.ptr);
		int fd = request->getFd();

		// 有事件发生的描述符为监听描述符
		if (fd == listen_fd)
		{
			acceptConnection(listen_fd, epoll_fd, path);
		}
		else
		{
			if ((events[idx].events & EPOLLERR) || (events[idx].events & EPOLLHUP) || (events[idx].events & EPOLLIN))
			{
				cout << "error event" << endl;
				delete request;
				continue;
			}

			// 将请求任务加入到线程池中
			// 加入线程池之前将Timer和request分离
			request->seperateTimer();
			//int rc = threadpool_add(tp, myHandler, events[idx].data.ptr, 0);
			threadpool_add(tp, myHandler, events[idx].data.ptr, 0);
		}
	}
}


/* 处理逻辑是这样的~
因为(1) 优先队列不支持随机访问
(2) 即使支持，随机删除某节点后破坏了堆的结构，需要重新更新堆结构。
所以对于被置为deleted的时间节点，会延迟到它(1)超时 或 (2)它前面的节点都被删除时，它才会被删除。
一个点被置为deleted,它最迟会在TIMER_TIME_OUT时间后被删除。
这样做有两个好处：
(1) 第一个好处是不需要遍历优先队列，省时。
(2) 第二个好处是给超时时间一个容忍的时间，就是设定的超时时间是删除的下限(并不是一到超时时间就立即删除)，如果监听的请求在超时后的下一次请求中又一次出现了，
就不用再重新申请requestData节点了，这样可以继续重复利用前面的requestData，减少了一次delete和一次new的时间。
*/
void handle_expired_event()
{
	pthread_mutex_lock(&qlock);
	while (!myTimerQueue.empty())
	{
		mytimer* ptimer_now = myTimerQueue.top();
		if (ptimer_now->isDeleted())
		{
			myTimerQueue.pop();
			delete ptimer_now;
		}
		else if (ptimer_now->isvalid() == false)
		{
			myTimerQueue.pop();
			delete ptimer_now;
		}
		else
		{
			break;
		}
	}
	pthread_mutex_unlock(&qlock);
}

int main(int argc, char** argv)
{
	HandleSigpipe();
	int epoll_fd = epoll_init();
	assert(epoll_fd >= 0);
	
	threadpool_t* threadpool = threadpool_create(THREADPOOL_THREAD_NUM, QUEUE_SIZE, 0);
	
	int listen_fd = socket_bind_listen(PORT);
	assert(listen_fd >= 0);
	SetSocketNonBlocking(listen_fd);
	
	__uint32_t event = EPOLLIN | EPOLLET;
	requestData* req = new requestData();
	req->setFd(listen_fd);
	epoll_add(epoll_fd, listen_fd, static_cast<void*>(req), event);
	while (true)
	{
		int event_num = my_epoll_wait(epoll_fd, events, MAXEVENTS, -1);
		if (event_num == 0)
			continue;
		cout << event_num << " events are ready" << endl;

		// 遍历events数组，根据监听种类及描述符类型分发操作
		handle_events(epoll_fd, listen_fd, events, event_num, PATH, threadpool);
		handle_expired_event();
	}
	return 0;
}
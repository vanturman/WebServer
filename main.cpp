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
	
	// ����socket�����ؼ���������
	int listen_fd;
	if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		close(listen_fd);
		return -1;
	}

	// ����sockopt,��ֹ����"address already in use"����
	int optval = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
		return -1;

	// ���÷�����IP��PORT��ʹ��bind��
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);
	if (bind(listen_fd, (struct sockaddr*) & server_addr, sizeof(server_addr)) == -1)
		return -1;

	// ����
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

		// �ļ����������Զ�����Ե����(Edge Triggered)ģʽ����֤һ��socket��������һʱ��ֻ��һ���̴߳���
		__uint32_t _epoll_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
		epoll_add(epoll_fd, accept_fd, static_cast<void*>(req_info), _epoll_event);
		// ����ʱ����Ϣ
		mytimer* mtimer = new mytimer(req_info, TIMER_TIME_OUT);
		req_info->addTimer(mtimer);
		pthread_mutex_lock(&qlock);
		myTimerQueue.push(mtimer);
		pthread_mutex_unlock(&qlock);
	}
}

// �ַ�������
void handle_events(int epoll_fd, int listen_fd, struct epoll_event* events, int events_num, const string& path, threadpool_t* tp)
{
	for (int idx = 0; idx < events_num; ++idx)
	{
		// ��ȡ�����������¼���������
		requestData* request = (requestData*)(events[idx].data.ptr);
		int fd = request->getFd();

		// ���¼�������������Ϊ����������
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

			// ������������뵽�̳߳���
			// �����̳߳�֮ǰ��Timer��request����
			request->seperateTimer();
			//int rc = threadpool_add(tp, myHandler, events[idx].data.ptr, 0);
			threadpool_add(tp, myHandler, events[idx].data.ptr, 0);
		}
	}
}


/* �����߼���������~
��Ϊ(1) ���ȶ��в�֧���������
(2) ��ʹ֧�֣����ɾ��ĳ�ڵ���ƻ��˶ѵĽṹ����Ҫ���¸��¶ѽṹ��
���Զ��ڱ���Ϊdeleted��ʱ��ڵ㣬���ӳٵ���(1)��ʱ �� (2)��ǰ��Ľڵ㶼��ɾ��ʱ�����Żᱻɾ����
һ���㱻��Ϊdeleted,����ٻ���TIMER_TIME_OUTʱ���ɾ����
�������������ô���
(1) ��һ���ô��ǲ���Ҫ�������ȶ��У�ʡʱ��
(2) �ڶ����ô��Ǹ���ʱʱ��һ�����̵�ʱ�䣬�����趨�ĳ�ʱʱ����ɾ��������(������һ����ʱʱ�������ɾ��)����������������ڳ�ʱ�����һ����������һ�γ����ˣ�
�Ͳ�������������requestData�ڵ��ˣ��������Լ����ظ�����ǰ���requestData��������һ��delete��һ��new��ʱ�䡣
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

		// ����events���飬���ݼ������༰���������ͷַ�����
		handle_events(epoll_fd, listen_fd, events, event_num, PATH, threadpool);
		handle_expired_event();
	}
	return 0;
}
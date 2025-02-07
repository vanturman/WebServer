#ifndef THREADPOOL
#define THREADPOOL

#include "requestData.h"
#include <iostream>
#include <pthread.h>
#include <assert.h>

constexpr int THREADPOOL_INVALID = -1;
constexpr int THREADPOOL_LOCK_FAILURE = -2;
constexpr int THREADPOOL_QUEUE_FULL = -3;
constexpr int THREADPOOL_SHUTDOWN = -4;
constexpr int THREADPOOL_THREAD_FAILURE = -5;
constexpr int THREADPOOL_GRACEFUL = 1;

constexpr int MAX_THREADS = 1024;
constexpr int MAX_QUEUE = 65535;

typedef enum
{
	immediate_shutdown = 1,
	graceful_shutdown = 2
}threadpool_shutdown_t;

/**
 *  @struct threadpool_task
 *  @brief task struct
 *
 *  @var function: Pointer of function that performs the task.
 *  @var argument: Argument of the function.
 */
typedef struct
{
	void (*function)(void*);
	void* argument;
} threadpool_task_t;

/**
 *  @struct threadpool
 *  @brief threadpool struct
 *
 *	&var lock:		   Lock the threadpool for operations
 *  @var notify:       Condition variable to notify worker threads.
 *  @var threads:      Array contains worker threads ID.
 *  @var thread_count: Number of threads
 *  @var queue:        Array containing the task queue.
 *  @var queue_size:   Size of the task queue.
 *  @var head:         Index of the first task element.
 *  @var tail:         Index of the last task element.
 *  @var count:        Number of pending tasks
 *  @var shutdown:     Flag indicating if the pool is shutting down
 *  @var started:      Number of started threads
 */
struct threadpool_t 
{
	pthread_mutex_t lock;
	pthread_cond_t notify;
	pthread_t *threads;
	threadpool_task_t* queue;
	int thread_count;
	int queue_size;
	int head;
	int tail;
	int count;
	int shutdown;
	int started;

};

threadpool_t* threadpool_create(int thread_count, int queue_size, int flags);
int threadpool_add(threadpool_t* pool, void(*function)(void*), void *argument, int flags);
int threadpool_destroy(threadpool_t *pool, int flags);
int threadpool_free(threadpool_t *pool);
static void* threadpool_thread(void *threadpool);

#endif

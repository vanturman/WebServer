#include "threadpool.h"

threadpool_t* threadpool_create(int thread_count, int queue_size, int flags)
{
	threadpool_t* pool;

	// use do...while(false) instead of goto statement
	do
	{
		if (thread_count <= 0 || thread_count > MAX_THREADS || queue_size <= 0 || queue_size > MAX_QUEUE) 
		{
			return nullptr;
		}
		if ((pool = (threadpool_t*)malloc(sizeof(threadpool_t))) == nullptr)
		{
			break;
		}
		
		// initialization
		pool->thread_count = 0;
		pool->shutdown = pool->started = 0;
		pool->queue_size = 0;
		pool->head = pool->tail = pool->count = 0;

		// allocate thread and task queue
		pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_count);
		pool->queue = (threadpool_task_t*)malloc(sizeof(threadpool_task_t) * queue_size);
		assert(pool->threads != nullptr && pool->queue != nullptr);


		// initialize mutex and condition variable first
		if (pthread_mutex_init(&(pool->lock), nullptr) != 0 || pthread_cond_init(&(pool->notify), nullptr) != 0)
		{
			break;
		}

		// start worker threads
		for (int idx = 0; idx < thread_count; ++idx)
		{
			if(pthread_create(&(pool->threads[idx]), nullptr, threadpool_thread, (void*)pool) != 0)
			{
				threadpool_destroy(pool, 0);
				return nullptr;
			}
			pool->thread_count++;
			pool->started++;
		}
		return pool;
	} while(false);

	if(pool != nullptr)
	{
		threadpool_free(pool);
	}
	return nullptr;
}

int threadpool_add(threadpool_t* pool, void (*function)(void*), void* argument, int flags)
{
	std::cout << "add thread to thread pool" << std::endl;
	int err = 0;
	if (pool == nullptr || function == nullptr)
	{
		return THREADPOOL_INVALID;
	}
	if (pthread_mutex_lock(&(pool->lock)) != 0)
	{
		return THREADPOOL_LOCK_FAILURE;
	}
	do
	{
		// determine if the queue is full
		if (pool->count == pool->queue_size)
		{
			err = THREADPOOL_QUEUE_FULL;
			break;
		}
		// determine if all the thread pool are shutting down
		if (pool->shutdown)
		{
			err = THREADPOOL_SHUTDOWN;
			break;
		}
		// add the new task to task queue
		pool->queue[pool->tail].function = function;
		pool->queue[pool->tail].argument = argument;
		pool->tail = (pool->tail + 1) % pool->queue_size;
		pool->count += 1;
		// pthread_cond_broadcast
		if(pthread_cond_signal(&(pool->notify)) != 0)
		{
			err = THREADPOOL_LOCK_FAILURE;
			break;
		}
	} while(false);

	if (pthread_mutex_unlock(&pool->lock) != 0) 
	{
		err = THREADPOOL_LOCK_FAILURE;
	}
	return err;
}

int threadpool_destroy(threadpool_t* pool, int flags)
{
	std::cout << "Thread pool is destroying." << std::endl;
	if (pool == nullptr)
	{
		return THREADPOOL_INVALID;
	}
	if (pthread_mutex_lock(&(pool->lock)) != 0)
	{
		return THREADPOOL_LOCK_FAILURE;
	}

	int err = 0;
	do
	{
		// determine if thread pool has already shutted down
		if (pool->shutdown == 1)
		{
			err = THREADPOOL_SHUTDOWN;
			break;
		}
		pool->shutdown = (flags & THREADPOOL_GRACEFUL) ? graceful_shutdown : immediate_shutdown;

		// wake up all worker threads
		if (pthread_cond_broadcast(&(pool->notify)) != 0 || pthread_mutex_unlock(&(pool->lock)) != 0)
		{
			err = THREADPOOL_LOCK_FAILURE;
			break;
		}
		for (int idx = 0; idx < pool->thread_count; ++idx)
		{
			if (pthread_join(pool->threads[idx], nullptr) != 0)
			{
				err = THREADPOOL_THREAD_FAILURE;
			}
		}

	} while(false);

	// deallocate the thread pool when we get everything done correctly
	if (err == 0)
	{
		threadpool_free(pool);
	}
	return err;
}

int threadpool_free(threadpool_t* pool)
{
	if (pool == nullptr || pool->started > 0)
	{
		return -1;
	}
	// deallocate all the resources
	// 1. thread pool 2. task queue 3. lock 4. condition variable
	if (pool->threads != nullptr)
	{
		free(pool->threads);
		free(pool->queue);

		// since we allocate pool->threads after initializing the mutex and condition variable, they
		// are already initialized. Lock the mutex just in case.
		pthread_mutex_lock(&(pool->lock));
		pthread_mutex_destroy(&(pool->lock));
		pthread_cond_destroy(&(pool->notify));
	}
	free(pool);
	return 0;
}

static void* threadpool_thread(void* threadpool)
{
	threadpool_t* pool = (threadpool_t*)threadpool;

	while (true)
	{
		// lock must be token to wait on condition variable
		pthread_mutex_lock(&(pool->lock));

		// wait on condition variable, check spurious wakeups.
		// when pthread_cond_wait() returns, obtain the lock
		while (pool->count == 0 && pool->shutdown == 0)
		{
			pthread_cond_wait(&(pool->notify), &(pool->lock));
		}
		if ((pool->shutdown == immediate_shutdown) || (pool->shutdown == graceful_shutdown && pool->count == 0))
		{
			break;
		}
		
		// grab our task
		threadpool_task_t task;
		task.function = pool->queue[pool->head].function;
		task.argument = pool->queue[pool->head].argument;
		pool->head = (pool->head + 1) % pool->queue_size;
		pool->count -= 1;

		// release the lock
		pthread_mutex_unlock(&(pool->lock));

		// execute the task
		(*(task.function))(task.argument);
	}
	pool->started--;
	pthread_mutex_unlock(&(pool->lock));
	pthread_exit(nullptr);
	return nullptr;
}
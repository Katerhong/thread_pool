/*************************************************************************
	> File Name: threadpool.c
	> Author: Katerhong
	> Mail:  
	> Created Time: Sun 26 Jul 2020 01:32:51 AM PDT
 ************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>



#define DEFAULT_THREAD_VARY 10
#define DEFAULT_TIME 10
#define MIN_WAIT_TASK_NUM 10
typedef struct{
	void *(*function)(void*);
	void *arg;
}threadpool_tast_t;

/*线程池信息*/
typedef struct{
	pthread_mutex_t lock;
	pthread_mutex_t thread_counter;      //busy_thr_num
	pthread_cond_t queue_not_full;
	pthread_cond_t queue_not_empty;

	pthread_t *threads;
	pthread_t adjust_tid;
	threadpool_tast_t *task_queue;

    int min_thr_num;
	int max_thr_num;
	int live_thr_num;
	int busy_thr_num;
	int wait_exit_thr_num;
	
	int queue_front;
	int queue_rear;
	int queue_size;
	int queue_max_size;

	int shutdown;
}threadpool_t;

int is_thread_alive(pthread_t threadid)
{
	int kill_rc = pthread_kill(threadid, 0);
	if (kill_rc == ESRCH)
	{
		return 0;
	}
	return 1;

}
int threadpool_free(threadpool_t *pool)
{
	if (pool == NULL)
	{
		return -1;
	}
	if (pool->threads)
	{
		free(pool->threads);
		pthread_mutex_lock(&(pool->lock));
		pthread_mutex_destroy(&(pool->lock));
		pthread_mutex_unlock(&(pool->thread_counter));
		pthread_mutex_destroy(&(pool->thread_counter));
		pthread_cond_destroy(&(pool->queue_not_empty));
		pthread_cond_destroy(&(pool->queue_not_full));
	}
	free(pool);
	pool = NULL;
	return 0;

}
void *threadpool_thread(void *threadpool)
{
	threadpool_t *pool = (threadpool_t*)threadpool;
	threadpool_tast_t  task;

	while(1)
	{
		pthread_mutex_lock(&pool->lock);
		while((pool->queue_size == 0) && (!pool->shutdown))
		{
			printf("thread 0x%x is waiting\n",(unsigned int)pthread_self());
			pthread_cond_wait(&(pool->queue_not_empty), &(pool->lock));

			if (pool->wait_exit_thr_num > 0)
			{
				pool->wait_exit_thr_num--;

				if(pool->live_thr_num > pool->min_thr_num)
				{
					printf("thread 0x%x is exiting\n", (unsigned int)pthread_self());
					pool->live_thr_num--;
					pthread_mutex_unlock(&(pool->lock));
					pthread_exit(NULL);
				}

			}
		}

		if (pool->shutdown)
		{
			pthread_mutex_unlock(&(pool->lock));
			printf("thread 0x%x exit!\n", (unsigned int)pthread_self());
			pthread_exit(NULL);
		}

		task.function = pool->task_queue[pool->queue_front].function;
		task.arg = pool->task_queue[pool->queue_front].arg;

		pool->queue_front = (pool->queue_front + 1) % pool->queue_max_size;
		pool->queue_size--;

		pthread_cond_broadcast(&(pool->queue_not_full));

		pthread_mutex_unlock(&(pool->lock));

		printf("thread 0x%x start workding\n", (unsigned int)pthread_self());
		pthread_mutex_lock(&(pool->thread_counter));
		pool->busy_thr_num++;
		pthread_mutex_unlock(&(pool->thread_counter));
		(*(task.function))(task.arg);

		printf("thread 0x%x end working\n", (unsigned int)pthread_self());
		pthread_mutex_lock(&(pool->thread_counter));
		pool->busy_thr_num--;
		pthread_mutex_unlock(&(pool->thread_counter));
	}
	pthread_exit(NULL);
	

}

void *adjust_thread(void *threadpool)
{
	int i;
	threadpool_t *pool = (threadpool_t*)threadpool;
	while(!pool->shutdown)
	{
		sleep(10);

		pthread_mutex_lock(&pool->lock);
		int queue_size = pool->queue_size;
		int live_thr_num = pool->live_thr_num;
		pthread_mutex_unlock(&(pool->lock));

		pthread_mutex_lock(&(pool->thread_counter));
		int busy_thr_num = pool->busy_thr_num;
		pthread_mutex_unlock(&(pool->thread_counter));

		if (queue_size >= MIN_WAIT_TASK_NUM && live_thr_num < pool->max_thr_num)
		{
			pthread_mutex_lock(&(pool->lock));
			int add = 0;

			for(i=0; i < pool->max_thr_num && add < DEFAULT_THREAD_VARY
				&& pool->live_thr_num < pool->max_thr_num; i++)
			{
				if (pool->threads[i] == 0 || !is_thread_alive(pool->threads[i]))
				{
					pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void*)pool);
					printf("create thread:0x%x index:%d\n",(unsigned int)pool->threads[i], i);
					add++;
					pool->live_thr_num++;
				}
			}
			pthread_mutex_unlock(&(pool->lock));
		}

		if ((busy_thr_num*2) < live_thr_num && live_thr_num > pool->min_thr_num)
		{
			pthread_mutex_lock(&(pool->lock));
			pool->wait_exit_thr_num = DEFAULT_THREAD_VARY;
			pthread_mutex_unlock(&pool->lock);

			for (i=0; i < DEFAULT_THREAD_VARY; i++)
			{
				pthread_cond_signal(&(pool->queue_not_empty));
			} 
		}
	}
	return NULL;
}

threadpool_t *threadpool_create(int iMinthr, int iMaxthr, int iQueueMaxSize)
{
	int i;
	threadpool_t *pool = NULL;
	do{
		if((pool = (threadpool_t*)malloc(sizeof(threadpool_t))) == NULL)
		{
			printf("malloc threadpool fail!\n");
			break;
		}
		pool->min_thr_num = iMinthr;
		pool->max_thr_num = iMaxthr;
		pool->busy_thr_num = 0;
		pool->live_thr_num = iMinthr;
		
		pool->queue_size = 0;
		pool->queue_max_size = iQueueMaxSize;
		pool->queue_front = 0;
		pool->queue_rear = 0;
		pool->shutdown = 0;

		/*根据最大线程上限，给工作线程数组开闭空间，并清零*/
		pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * iMaxthr);
		if(NULL == pool->threads)
		{
			printf("malloc threads error!\n");
			break;
		}
		memset(pool->threads, 0, sizeof(pthread_t)*iMaxthr);

		/*任务队列初始化*/
		pool->task_queue = (threadpool_tast_t*)malloc(sizeof(threadpool_tast_t)*iMaxthr);
		if(NULL == pool->task_queue)
		{
			printf("malloc task_queue error!\n");
			break;
		}

		/*锁和条件变量初始化*/
		if (pthread_mutex_init(&(pool->lock), NULL) != 0
			|| pthread_mutex_init(&(pool->thread_counter), NULL) != 0
			|| pthread_cond_init(&(pool->queue_not_empty), NULL) != 0
			|| pthread_cond_init(&(pool->queue_not_full), NULL) != 0)
		{
			printf("init the lock or cond fail!\n");
			break;
		}
		/*启动minthre个work_thread*/
		for(i=0; i < iMinthr; i++)
		{
			pthread_create(&pool->threads[i], NULL, threadpool_thread, (void*)pool); //pool指向当前线程池
			printf("start thread 0x%x...\n", (unsigned int)pool->threads[i]);
		}
		pthread_create(&pool->adjust_tid, NULL, adjust_thread, (void*)pool);
		return pool;
	}while(0);

	threadpool_free(pool);
}

/*添加任务*/
int threadpool_add(threadpool_t *pool, void*(*function)(void *arg), void *arg)
{
	pthread_mutex_lock(&(pool->lock));
	printf("queue_size:%d rear:%d\n", pool->queue_size, pool->queue_rear);
	while((pool->queue_size == pool->queue_max_size) && !pool->shutdown)
	{
		pthread_cond_wait(&(pool->queue_not_full), &(pool->lock));
	}
	if(pool->shutdown)
	{
		pthread_mutex_unlock(&(pool->lock));
	}

	#if 0
	if (pool->task_queue[pool->queue_rear].arg != NULL)
	{
		free(pool->task_queue[pool->queue_rear].arg);
		pool->task_queue[pool->queue_rear].arg = NULL;
	}
	#endif

	pool->task_queue[pool->queue_rear].function = function;
	pool->task_queue[pool->queue_rear].arg = arg;
	pool->queue_rear = (pool->queue_rear + 1) % pool->queue_max_size;
	pool->queue_size++;


	pthread_cond_signal(&(pool->queue_not_empty));
	pthread_mutex_unlock(&(pool->lock));
	printf("queue idx:%d\n", *(int*)arg);
	return 0;
}

int threadpool_destroy(threadpool_t *pool)
{
	int i =0;
	if (pool == NULL)
	{
		return -1;
	}
	pool->shutdown = 1;

	pthread_join(pool->adjust_tid, NULL);

	for(i=0;i< pool->live_thr_num; i++)
	{
		pthread_cond_broadcast(&(pool->queue_not_empty));
	}
	for(i=0;i < pool->live_thr_num; i++)
	{
		pthread_join(pool->threads[i], NULL);
	}
	threadpool_free(pool);
	return 0;
}




void *process(void *arg)
{
	printf("thread 0x%x working on task %d\n", (unsigned int)pthread_self(), *(int*)arg );
	sleep(1);
	printf("task %d is end\n",*(int*)arg);
	return NULL;
}

void print_pool_info(threadpool_t *pool)
{
	if(NULL == pool)
		return;

	printf("*********************thread pool info *****************\n");
	printf("busy_thr:%d live_thr:%d wait_exit_thr:%d\n", 
		pool->busy_thr_num, 
		pool->live_thr_num,
		pool->wait_exit_thr_num);
	printf("front:%d rear:%d size:%d\n",
		pool->queue_front,
		pool->queue_rear,
		pool->queue_size);
	printf("*********************end*****************\n");
}

int main(void)
{
	threadpool_t *thp = threadpool_create(3, 100,100);

	int num[200], i;
	for (i=0; i<200; i++)
	{
		num[i]= i;
		printf("add task%d\n", i);
		threadpool_add(thp, process, (void*)&num[i]);
	}
	sleep(5);
	print_pool_info(thp);
	sleep(10);
	threadpool_destroy(thp);

	return 0;

}

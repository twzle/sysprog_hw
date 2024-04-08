#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>

enum thread_task_status
{
    IS_CREATED = 1,
    IS_ENQUEUED = 2,
    IS_RUNNING = 3,
    IS_FINISHED = 4,
};

struct thread_task
{
    thread_task_f function;
    void *arg;
    void *result;
    bool is_detached;
    enum thread_task_status status;
    struct thread_pool *pool;
    struct thread_task *next_task;
    pthread_mutex_t mutex;
    pthread_cond_t is_done;
};

struct thread_pool
{
    pthread_t *threads;
    int thread_count;
    int max_thread_count;
    struct thread_task *task_queue_head;
    struct thread_task *task_queue_tail;
    int task_queue_size;
    bool is_deleted;
    pthread_cond_t task_added;
    pthread_mutex_t mutex;
};

void *thread_task_executor(void *thread_pool)
{
    struct thread_pool *pool = thread_pool;
    while (true)
    {
        pthread_mutex_lock(&pool->mutex);
        if (pool->is_deleted)
        {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }
        struct thread_task *task = pool->task_queue_head;
        if (task != NULL)
        {
            pthread_mutex_lock(&task->mutex);
            if (pool->task_queue_tail == task)
            {
                pool->task_queue_head = NULL;
                pool->task_queue_tail = NULL;
            }
            else
            {
                pool->task_queue_head = task->next_task;
            }
            pthread_mutex_unlock(&pool->mutex);

            task->next_task = NULL;
            task->status = IS_RUNNING;
            thread_task_f f = task->function;
            void* arg = task->arg;
            void* result = task->result;
            pthread_mutex_unlock(&task->mutex);
            
            result = f(arg);

            pthread_mutex_lock(&task->mutex);
            task->result = result;
            task->status = IS_FINISHED;

            if (!task->is_detached) {
                pthread_cond_signal(&task->is_done);
                pthread_mutex_unlock(&task->mutex);
            } else {
                pthread_mutex_lock(&pool->mutex);
                pool->task_queue_size--;
                pthread_mutex_unlock(&pool->mutex);
                task->pool = NULL;
                pthread_mutex_unlock(&task->mutex);
                thread_task_delete(task);
                continue;
            }
        }
        else
        {
            pthread_cond_wait(&pool->task_added, &pool->mutex);
            pthread_mutex_unlock(&pool->mutex);
        }
    }
    return NULL;
}

int thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
    if (max_thread_count <= TPOOL_MAX_THREADS && max_thread_count > 0)
    {
        struct thread_pool *new_pool = malloc(sizeof(struct thread_pool));
        new_pool->threads = NULL;
        new_pool->thread_count = 0;
        new_pool->max_thread_count = max_thread_count;
        new_pool->task_queue_head = NULL;
        new_pool->task_queue_tail = NULL;
        new_pool->task_queue_size = 0;
        new_pool->is_deleted = false;
        pthread_cond_init(&new_pool->task_added, NULL);
        pthread_mutex_init(&new_pool->mutex, NULL);

        *pool = new_pool;
        return 0;
    }
    else
    {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
}

int thread_pool_thread_count(const struct thread_pool *pool)
{
    return pool->thread_count;
}

int thread_pool_delete(struct thread_pool *pool)
{
    pthread_mutex_lock(&pool->mutex);
    if (pool->task_queue_size == 0)
    {
        pool->is_deleted = true;
        pthread_cond_broadcast(&pool->task_added);
        pthread_mutex_unlock(&pool->mutex);

        for (int i = 0; i < pool->thread_count; i++)
        {
            pthread_join(pool->threads[i], NULL);
        }

        pthread_cond_destroy(&pool->task_added);
        pthread_mutex_destroy(&pool->mutex);
        free(pool->threads);
        free(pool);
        return 0;
    }
    else
    {
        pthread_mutex_unlock(&pool->mutex);
        return TPOOL_ERR_HAS_TASKS;
    }
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
    pthread_mutex_lock(&pool->mutex);
    if (pool->task_queue_size != TPOOL_MAX_TASKS)
    {
        pool->task_queue_size++;

        pthread_mutex_lock(&task->mutex);
        task->pool = pool;
        task->status = IS_ENQUEUED;

        if (pool->task_queue_head == NULL)
        {
            pool->task_queue_head = task;
        }
        if (pool->task_queue_tail == NULL)
        {
            pool->task_queue_tail = task;
        }
        else
        {
            pool->task_queue_tail->next_task = task;
            pool->task_queue_tail = task;
        }
        pthread_mutex_unlock(&task->mutex);

        if (pool->task_queue_size > pool->thread_count && pool->thread_count != pool->max_thread_count)
        {
            pthread_t *threads = realloc(pool->threads, sizeof(pthread_t) * (pool->thread_count + 1));
            if (threads != NULL)
            {
                pool->threads = threads;
                pthread_t *last_thread = &pool->threads[pool->thread_count];
                if (pthread_create(last_thread, NULL, thread_task_executor, pool) == 0)
                {
                    pool->thread_count++;
                }
            }
        }
        pthread_cond_signal(&pool->task_added);
        pthread_mutex_unlock(&pool->mutex);
        return 0;
    }
    else
    {
        pthread_mutex_unlock(&pool->mutex);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }
}

int thread_task_new(struct thread_task **task, thread_task_f function,
                    void *arg)
{
    struct thread_task *new_task = malloc(sizeof(struct thread_task));
    new_task->status = IS_CREATED;
    new_task->pool = NULL;
    new_task->function = function;
    new_task->arg = arg;
    new_task->is_detached = false;
    new_task->next_task = NULL;
    pthread_cond_init(&new_task->is_done, NULL);
    pthread_mutex_init(&new_task->mutex, NULL);

    *task = new_task;
    return 0;
}

bool thread_task_is_finished(const struct thread_task *task)
{
    return task->status == IS_FINISHED;
}

bool thread_task_is_running(const struct thread_task *task)
{
    return task->status == IS_RUNNING;
}

int thread_task_join(struct thread_task *task, void **result)
{
    pthread_mutex_lock(&task->mutex);
    if (task->pool != NULL)
    {
        while (!thread_task_is_finished(task))
        {
            pthread_cond_wait(&task->is_done, &task->mutex);
        }

        pthread_mutex_lock(&task->pool->mutex);
        task->pool->task_queue_size--;
        pthread_mutex_unlock(&task->pool->mutex);

        *result = task->result;
        task->pool = NULL;
        pthread_mutex_unlock(&task->mutex);
        return 0;
    }
    else
    {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
}

#ifdef NEED_TIMED_JOIN

int thread_task_timed_join(struct thread_task *task, double timeout,
                           void **result)
{
    /* IMPLEMENT THIS FUNCTION */
    (void)task;
    (void)timeout;
    (void)result;
    return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif

int thread_task_delete(struct thread_task *task)
{   
    pthread_mutex_lock(&task->mutex);
    if (task->pool == NULL)
    {
        pthread_cond_destroy(&task->is_done);
        pthread_mutex_unlock(&task->mutex);
        pthread_mutex_destroy(&task->mutex);
        free(task);
        return 0;
    }
    else
    {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_IN_POOL;
    }
}

#ifdef NEED_DETACH

int thread_task_detach(struct thread_task *task)
{
    pthread_mutex_lock(&task->mutex);
    if (task->pool != NULL) {
        if (!thread_task_is_finished(task)) {
            task->is_detached = true;
            pthread_mutex_unlock(&task->mutex);
        } else {
            pthread_mutex_lock(&task->pool->mutex);
            task->pool->task_queue_size--;
            pthread_mutex_unlock(&task->pool->mutex);
            task->pool = NULL;
            pthread_mutex_unlock(&task->mutex);
            thread_task_delete(task);
        }
        return 0;
    } else {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
}

#endif
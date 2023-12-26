#include <pthread.h>
#include <threads.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "list.h"
#include "threadpool.h"

static pthread_mutex_t globalLock = PTHREAD_MUTEX_INITIALIZER;

struct thread_pool
{
    struct list globalTasks;
    pthread_t *threadList; // store ID of threads
    int numThreads; //Number of threads
    pthread_cond_t cond;
    bool shutdown; //shutdown flag
};

enum future_status {
    READY,          /* Task is ready */
    INPROGRESS,     /* Task is in progress */
    COMPLETED,      /* Task is completed */
};

struct future
{
    fork_join_task_t task; 
    void* args; //data from thread_pool_submit
    void* result; //store the task result once completed
    enum future_status status;  /* Future status. */ 
    struct list_elem elem;
    struct thread_pool *pool;
    pthread_cond_t fcond; //cond
};

//Worker function
static void * workerFunction(void *pool)
{
    struct thread_pool *threadPool = pool;

    while(true)
    {
        pthread_mutex_lock(&globalLock);
        //Wait here
        while(threadPool->shutdown == false && list_empty(&threadPool->globalTasks))
        {
            pthread_cond_wait(&threadPool->cond, &globalLock);
        }
        if (threadPool->shutdown == true)
        {
            break;
        }
        if(!list_empty(&threadPool->globalTasks)){
            struct list_elem * e;
            e = list_pop_front(&threadPool->globalTasks);
            struct future* newFuture = list_entry(e, struct future, elem);

            newFuture->status = INPROGRESS;
            pthread_mutex_unlock(&globalLock);
            newFuture->result = newFuture->task(pool, newFuture->args);
            pthread_mutex_lock(&globalLock);
            //Task done, notify thread
            newFuture->status = COMPLETED;
            pthread_cond_signal(&newFuture->fcond);
            
            pthread_mutex_unlock(&globalLock);
        }
    }
    pthread_mutex_unlock(&globalLock); 
    return NULL;
}


/* Create a new thread pool with no more than n threads. */
struct thread_pool * thread_pool_new(int nthreads)
{
    struct thread_pool *pool = (struct thread_pool *)malloc(sizeof(struct thread_pool));
    
    pthread_cond_init(&pool->cond, NULL);
    pthread_mutex_lock(&globalLock);
    pool->numThreads = nthreads;
    pool->threadList = (pthread_t *)malloc(sizeof(pthread_t) * nthreads);
    list_init(&pool->globalTasks);
    pool->shutdown = false;

    for (int i = 0; i < nthreads; i++)
    {
        int rc = pthread_create(&pool->threadList[i], NULL, workerFunction, (void*)pool);
        if (rc != 0)
        {
            perror("pthread_create");
        }
    }
    pthread_mutex_unlock(&globalLock);
    return pool;
}


/* 
 * Shutdown this thread pool in an orderly fashion.  
 * Tasks that have been submitted but not executed may or
 * may not be executed.
 *
 * Deallocate the thread pool object before returning. 
 */
void thread_pool_shutdown_and_destroy(struct thread_pool *poolToDestroy)
{
    pthread_mutex_lock(&globalLock);
    poolToDestroy->shutdown = true;
    pthread_cond_broadcast(&poolToDestroy->cond);
    pthread_mutex_unlock(&globalLock);
    //Join the threads
    int threads = poolToDestroy->numThreads;

    for (int i = 0; i < threads; i++) 
    {
        int join = pthread_join(poolToDestroy->threadList[i], NULL);
        if (join != 0) {
            perror("pthread_join");
        }
    }

    free(poolToDestroy->threadList);
    pthread_mutex_destroy(&globalLock);
    pthread_cond_destroy(&poolToDestroy->cond);
    free(poolToDestroy);
    
}

/* A function pointer representing a 'fork/join' task.
 * Tasks are represented as a function pointer to a
 * function.
 * 'pool' - the thread pool instance in which this task
 *          executes
 * 'data' - a pointer to the data provided in thread_pool_submit
 *
 * Returns the result of its computation.
 */
typedef void * (* fork_join_task_t) (struct thread_pool *pool, void * data);



/* 
 * Submit a fork join task to the thread pool and return a
 * future.  The returned future can be used in future_get()
 * to obtain the result.
 * 'pool' - the pool to which to submit
 * 'task' - the task to be submitted.
 * 'data' - data to be passed to the task's function
 *
 * Returns a future representing this computation.
 */
struct future * thread_pool_submit(struct thread_pool *pool, fork_join_task_t task, void * data)
{   
    pthread_mutex_lock(&globalLock);
    struct future *newFuture = (struct future *)malloc(sizeof(struct future));
    newFuture->task = task;
    newFuture->args = data;
    newFuture->status = READY;
    newFuture->pool = pool;
    pthread_cond_init(&newFuture->fcond, NULL);
    
    list_push_back(&pool->globalTasks, &newFuture->elem);
    //Signal to get out of wait
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&globalLock);
    return newFuture;
}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void * future_get(struct future *futureToGet)
{

    struct thread_pool *parentPool = futureToGet->pool; 
    pthread_mutex_lock(&globalLock);
    //If not started, remove and run it yourself, unlock then run then lock
    //If completed , unlock and return result
    if (futureToGet->status == READY)
    {
        list_remove(&futureToGet->elem);
        futureToGet->status = INPROGRESS;
        pthread_mutex_unlock(&globalLock);
        futureToGet->result = futureToGet->task(parentPool, futureToGet->args);
        pthread_mutex_lock(&globalLock);
        futureToGet->status = COMPLETED;
    }
    else
    {
        while(futureToGet->status != COMPLETED)
        {
            pthread_cond_wait(&futureToGet->fcond, &globalLock);
        }
    }
    pthread_mutex_unlock(&globalLock);
    return futureToGet->result;

}

/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future *futureToFree)
{
    pthread_cond_destroy(&futureToFree->fcond);
    free(futureToFree);
}
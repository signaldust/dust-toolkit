
#include "threadpool.h"

using namespace dust;

void dust::ThreadPool::Worker::run()
{
    while(true)
    {
        // wait for work
        pool->sWork.wait();

        // check for exit first
        if(pool->exit) return;

        // ok we have a real job
        ThreadTask * task;

        // critical section
        {
            Mutex::Lock lock(pool->mRead);

            unsigned qRead = pool->qRead;
            task = pool->tqueue[qRead].task;
            memfence();
            pool->qRead = (qRead + 1) % threadPool_queueSize;
        }
        // once unlocked, free the space
        pool->sFree.post();

        // then run the job
        task->threadpool_runtask();
    }
}

void dust::ThreadPool::queue_tasks(ThreadTask ** tasks, unsigned nTasks)
{
#if DUST_THREADPOOL_DEBUG
    // this is single-threaded for sanity checking
    // if this works and the normal implementation doesn't
    // there some of the assumptions are violated and
    // one should carefully re-read the documentation
    for(unsigned i = 0; i < nTasks; ++i)
    {
        tasks[i]->threadpool_runtask();
    }
#else

    // THIS VERSION is NOT re-entrant safe from workers
    Mutex::Lock l(mWrite);
    for(unsigned i = 0; i < nTasks; ++i)
    {
        // wait for space
        sFree.wait();

        // add a job into the queue
        tqueue[qWrite].task = tasks[i];
        memfence();
        qWrite = (qWrite + 1) % threadPool_queueSize;
        // post job
        sWork.post();

    }
#endif
}

void dust::ThreadPool::queue_tasks_reentrant(ThreadTask ** tasks, unsigned nTasks)
{
#if DUST_THREADPOOL_DEBUG
    // this is single-threaded for sanity checking
    // if this works and the normal implementation doesn't
    // there some of the assumptions are violated and
    // one should carefully re-read the documentation
    for(unsigned i = 0; i < nTasks; ++i)
    {
        tasks[i]->threadpool_runtask();
    }
#else
    // this no longer does any "full-set" locking,
    // so queue_tasks can then be called recursively
    for(unsigned i = 0; i < nTasks; ++i)
    {
        // nested scope for the per-task lock
        // must free this before direct processing
        // to avoid degenerate serial processing
        {
            Mutex::Lock l(mWrite);

            while(true)
            {
                // if we don't have free space then break inner loop
                // we'll then process one entry and try again
                if(!sFree.tryWait(0)) break;

                // add a job into the queue
                tqueue[qWrite].task = tasks[i];
                memfence();
                qWrite = (qWrite + 1) % threadPool_queueSize;
                // post job
                sWork.post();

                // we must check this explicitly because
                // we need to bail out both loops once we're done
                if(++i >= nTasks) return;
            }

        } // free mutex before direct process

        debugPrint("threadpool warning: queue full!\n");

        // queue was full, run directly
        tasks[i]->threadpool_runtask();
    }
#endif
}

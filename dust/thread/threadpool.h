
#pragma once

#include <algorithm>
#include "thread.h"

// if this is set to 1, then queue_task() will directly process
// all the tasks without actually placing them in the queue
#define DUST_THREADPOOL_DEBUG 0

namespace dust
{
    // Task interface for thread-pooling.
    struct ThreadTask
    {
        virtual void threadpool_runtask() = 0;
    };

    // This is the maximum amount of tasks that can be queued at any time.
    //
    // If the queue gets full we'll end up with some overhead and extra
    // slots just cost a bit of memory, so don't make this stupidly small.
    static const unsigned threadPool_queueSize = 1024;

    // This is a super-simple synchronous thread-pooling class.
    // It runs a set of parallel tasks using a fixed thread-pool.
    //
    // To use this:
    //  1. derive from ThreadTask and implement threadpool_runtask()
    //  2. create an array of pointers to the derived tasks
    //  3. call queue_tasks(...) with the pointer array
    //
    // Note that there is no built-in completion notification logic,
    // since this is easily handled by tasks/clients using semaphores.
    //
    // queue_tasks() will submit tasks as an "atomic batch" but is not
    // safe to call from the worker threads.
    //
    // queue_tasks_reentrant() is safe from workers, but submits one-by-one
    // and processes directly if the queue is already full
    //
    struct ThreadPool
    {
        ThreadPool(ThreadPool const &) = delete;
        
        // realtime sets RT-priority on the threads
        // passing nThreads = 0 will autodetect processors
        ThreadPool(bool realtime = false, unsigned nThreads = 0) : exit(false)
        {
            if(!nThreads) nThreads = Thread::getCpuCount();
            nWorkers = (std::min)(nThreads, threadPool_queueSize);

            qRead = qWrite = 0;
            sFree.post(threadPool_queueSize);

            workers = new Worker[nWorkers];
            for(unsigned i = 0; i < nWorkers; ++i)
            {
                if(realtime) workers[i].setRealtime(true);
                workers[i].pool = this;
                workers[i].start();
            }

            debugPrint("Created threadpool with %d threads\n", nWorkers);
        }

        // Virtual to allow derived classes (eg for singleton pools)
        virtual ~ThreadPool()
        {
            // signal exit, then free all workers
            exit = true;
            sWork.post(nWorkers);

            // then wait for each one of them
            for(unsigned i = 0; i < nWorkers; ++i)
            {
                workers[i].wait();
            }

            delete[] workers;
        }

        // return number of threads currently in pool
        unsigned getThreadCount() { return nWorkers; }

        // this version is NOT safe to use from workers, but guarantees
        // that all the tasks are queued together
        void queue_tasks(ThreadTask ** tasks, unsigned nTasks);

        // this version is re-entrancy safe from workers, but not atomic
        void queue_tasks_reentrant(ThreadTask ** tasks, unsigned nTasks);

    private:
        // Worker control semaphores:
        //  - sWork counts unclaimed work
        //  - sFree counts space in queue
        Semaphore   sWork, sFree;

        // Workers protect the queue from other workers using this.
        // FIXME: Find a lock-free solution?
        Mutex mRead;

        // Multiple clients can call run_tasks() at the same time.
        // This mutex protects the queue from concurrent writes.
        Mutex mWrite;

        bool exit;

        // implements a worker - sadly can't put these in a vector
        // because we don't have a copy-constructor with threads
        struct Worker : public Thread
        {
            ThreadPool * pool;

            void run();
        } * workers;
        unsigned nWorkers;

        unsigned qRead;    // worker shared read pointer
        unsigned qWrite;            // pool private

        // internal task descriptors
        struct TaskDesc
        {
            ThreadTask * task;
        } tqueue[threadPool_queueSize];

    };

};


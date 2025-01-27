
#pragma once

#include <cstdint>

#include "thread.h"

namespace dust
{

    // Servant for running lambdas in a helper thread.
    //
    // This is different from thread pools in the sense that the "servant"
    // is a single thread that executes tasks strictly in order.
    //
    // The intended use-case is pipelining planning and execution: the main
    // thread plans which tasks are to be executed, while the servant thread
    // performs the actual execution, allowing the main thread to plan ahead
    // before the previous tasks have been completed.
    //
    // NOTE: This does funky type-erasure with placement-new and reinterpret_cast
    // to support non-trivial destructors of value captures (eg. shared_ptr)
    // while at the same time avoiding heap-allocations.
    //
    // NOTE: This does NOT work with objects containing pointers to themselves,
    // because we move the object memory around; this is unavoidable, because
    // we cannot guarantee that the object in queue is in continuous memory.
    // This is technically "undefined behaviour" but not really a problem for
    // the intended use-case of sending lambdas from one thread to another.
    //
    template <unsigned QueueSizeBytes>
    struct Servant : private Thread
    {
        Servant() { start(); }

        ~Servant()
        {
            queue([this](){ exit = true; });
            wait();
        }

        // queue .. blocks if queue is full
        template <typename T>
        void queue(T && t)
        {
            auto fn = call<T>;

            // throw assert if object will never fit (don't deadlock)
            assert(sizeof(fn) + sizeof(Align<T>) <= QueueSizeBytes);

            // placement new move into a buffer to avoid destructor
            uint8_t buf[sizeof(Align<T>)]; new (buf) T(std::move(t));
            
            while(!_queue.send(
                reinterpret_cast<uint8_t*>(&fn), sizeof(fn), buf, sizeof(buf)))
            {
                semWrite.wait();
            }

            semRead.tryWait();  // clear semaphore
            semRead.post();
        }

        // queue a synchronization point
        //
        // NOTE: this does NOT stop queue execution, rather it only allows
        // the next call to sync() by queueing thread to progress
        void queueSync() { queue([this](){ semSync.post(); }); }

        // wait on a synchronization point
        void sync() { semSync.wait(); }

    private:
    
        // execute all tasks currently in the queue
        void exec()
        {
            auto bytes = _queue.recv(recvBuf, QueueSizeBytes);
            
            semWrite.tryWait(); // clear semaphore
            semWrite.post();
            
            if(!bytes)
            {
                semRead.wait();
                return;
            }

            unsigned offset = 0;
            while(offset < bytes)
            {
                auto fn = *reinterpret_cast<unsigned(**)(void*)>(&recvBuf[offset]);
                offset += sizeof(fn);
                offset += fn(reinterpret_cast<void*>(&recvBuf[offset]));
            }
        }

        virtual void run()
        {
            while(!exit)
            {
                exec();
            }
        }

        RTQueue<uint8_t, QueueSizeBytes>  _queue;
        uint8_t     recvBuf[QueueSizeBytes];

        Semaphore   semWrite;
        Semaphore   semRead;
        Semaphore   semSync;

        bool        exit = false;

        // this is just to get pointer alignment for size
        template <typename T> struct Align { union { void*p; T t; }; };

        // type-erasure magic to call the relevant type
        template <typename T>
        static unsigned call(void * t)
        {
            // call and destroy
            T *fn = reinterpret_cast<T*>(t); (*fn)(); fn->~T();
            return sizeof(Align<T>);
        }
    };


};

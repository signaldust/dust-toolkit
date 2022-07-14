
#pragma once

#include "dust/core/defs.h"

// we're stuck doing platform stuff because
// the C++11 threading stuff was designed by idiots
#if defined(_WIN32)
# include <windows.h>
# include <process.h>
# include <avrt.h>
# pragma comment(lib, "avrt.lib")
#elif defined(__APPLE__)
# include <pthread.h>
# include <dispatch/dispatch.h> // for semaphores
# include <mach/thread_act.h>   // for the affinity setting
#else
# error "Unknown platform"
#endif

#include <thread>
#include <chrono>

// stuff that can fail is wrapped in DUST_ASSERT calls,
// so if you want to handle errors, you can edit the macro
#include <cassert>

#define DUST_THREAD_ASSERT(x) assert(x)

namespace dust
{
    // short-cut to "full memory fence"
    //
    // FIXME: split this into acq/rel some day
    //
    static void inline memfence()
    {
        // use C++11 barriers so they work on ARM too
        std::atomic_thread_fence(std::memory_order_acq_rel);
    }

    static void inline memfence_acq()
    {
        // use C++11 barriers so they work on ARM too
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    static void inline memfence_rel()
    {
        // use C++11 barriers so they work on ARM too
        std::atomic_thread_fence(std::memory_order_release);
    }

    // This is a "realtime safe pointer" class
    //
    // Essentially, calling rtLock() returns the pointer
    // and guarantees that it remains valid until rtRelease().
    // These two operations are completely wait-free!
    //
    // To update the pointer, one calls swapAndWait() which sets
    // the pointer to the new value and "busy" loops until any
    // pending rtLock() that might have taken the old pointer is
    // guaranteed to have been released.
    //
    // After swapAndWait() returns (with the old pointer) it's safe
    // to free any memory related to the old pointer.
    //
    // This implementation does NOT handle multiple readers/writers!
    //
    // The intended purpose is to allow audio code to safely access
    // dynamically allocated memory structures, while simultaneously
    // providing GUI code with means to swap versions safely.
    //
    template <class T>
    struct RTPointer
    {
        RTPointer()
        {
            ptr = 0;
            readState = 0;
            // initialize lowest bit to non-zero value
            readGeneration = 1;
            memfence();
        }

        T * rtLock()
        {
            // set readState to current generation
            memfence_acq();
            readState = readGeneration;
            memfence_rel();
            
            // increment by 2, to keep the lowest bit non-zero
            // this avoids having to worry about wrap-around
            readGeneration += 2;

            // then we can load the pointer
            return ptr;
        }

        void rtRelease()
        {
            // this just needs to clear the flag
            memfence_acq();
            readState = 0;
            memfence_rel();
        }

        T * swapAndWait(T * newPtr)
        {
            memfence_acq();
            // get the old pointer
            T * oldPtr = ptr;
            // set the new pointer
            ptr = newPtr;

            memfence();

            // get the reader state
            unsigned oldState = readState;
            memfence_rel();
            
            // if it's non-zero, we need to wait
            if(oldState)
            {
                // wait until ANY change, since any new
                // reader gets the new pointer
                while(oldState == readState)
                {
                    // sleep for a "short time" ... like 1ms
                    // this shouldn't be too short, since we actually wait
                    // until real-time thread is done with the pointer
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));

                    memfence();
                }
            }
            // return old pointer to caller
            return oldPtr;
        }
    private:
        // the actual pointer stored
        T * ptr;
        // readState has the invariants that:
        //  - if it's zero, then there is no reader
        //  - the non-zero value changes on every lock
        unsigned readState;

        // readGeneration = 1 + 2*n mod 2^k
        // hence it's never zero even after wrap around
        unsigned readGeneration;
    };

    // Basic wait-free producer-consumer queue.
    // Templated on data type, which must be assignment-copyable.
    //
    // This does NOT handle multiple readers or writers, so additional
    // locking is required when this is required.
    //
    template <class DataType, unsigned QueueSize>
    struct RTQueue
    {
        // try to place an array of items into the queue
        // we either place all the items as an "atomic" block
        // or return false if there is not enough space.
        //
        // note that this can optionally take the "block" as two parts
        // either one of which can be empty, so that one can copy data
        // from a ring-buffer without having to shuffle around
        bool send(DataType * items, unsigned nItems,
            DataType * items2 = 0, unsigned nItems2 = 0)
        {
            // fetch the amount of space
            memfence();
            unsigned space = freeSpace;
            memfence();

            unsigned totalItems = nItems + nItems2;

            // not enough space, bail out
            if(space < totalItems) { return false; }

            // put stuff in the queue
            for(unsigned i = 0; i < nItems; ++i)
            {
                data[iWrite] = items[i];
                if(++iWrite == QueueSize) iWrite = 0;
            }

            // put more stuff in the queue
            for(unsigned i = 0; i < nItems2; ++i)
            {
                data[iWrite] = items2[i];
                if(++iWrite == QueueSize) iWrite = 0;
            }

            // this is clang builtin that also results in a fence
            __atomic_sub_fetch(&freeSpace, totalItems, __ATOMIC_ACQ_REL);

            return true;
        }

        // receive up to maxOut items of data from the queue
        // returns the number of items received
        unsigned recv(DataType * out, unsigned maxOut)
        {
            memfence();
            unsigned space = freeSpace;
            memfence();

            // compute the number of items in the queue
            unsigned items = QueueSize - space;

            if(items > maxOut) items = maxOut;
            for(unsigned i = 0; i < items; ++i)
            {
                out[i] = data[iRead];
                if(++iRead == QueueSize) iRead = 0;
            }

            // this is clang builtin that also results in a fence
            __atomic_add_fetch(&freeSpace, items, __ATOMIC_ACQ_REL);

            return items;
        }

    private:
        DataType data[QueueSize];

        // read and write pointers
        unsigned iRead = 0, iWrite = 0;

        // this is managed by atomic operations
        unsigned freeSpace = QueueSize;
    };


    // no standard C++11 solution since we need non-blocking
    // post() and trywait() to use these for real-time work
    struct Semaphore
    {
        Semaphore() { init(0); }
        Semaphore(Semaphore const &) = delete;
        
        Semaphore(int n) { init(n); }
        ~Semaphore()
        {
            close();
        }

        // Wait "count" times. Implemented as a loop for convenience.
        void wait(unsigned count = 1)
        {
            while(count--) platform_wait();
        }

        // Try to "wait", but return false if wait() would block.
        // One can also give optional timeout.
        bool tryWait(unsigned long timeout = 0)
        {
            bool value = platform_try_wait(timeout);
            return value;
        }

        // Post "count" times.
        void post(unsigned count = 1)
        {
            platform_signal(count);
        }
    private:
#if defined(_WIN32)
        HANDLE _s;
        void init(int n) {
            DUST_THREAD_ASSERT(_s = ::CreateSemaphore(0, n, INT_MAX, 0));
        }
        void close() { ::CloseHandle(_s); }
        void platform_wait()
        {
            ::WaitForSingleObject(_s, INFINITE);
        }
        bool platform_try_wait(unsigned long timeout)
        {
            return (WAIT_TIMEOUT != ::WaitForSingleObject(_s, timeout));
        }
        void platform_signal(unsigned count)
        {
            DUST_THREAD_ASSERT(FALSE != ::ReleaseSemaphore(_s, count, 0));
        }
#elif defined(__APPLE__)
        dispatch_semaphore_t _s;
        void init(int n) {
            // there is this wonderful bug where dispose will intentionally
            // crash if the count is not exactly as set originally.. except
            // it also appears to crash if the semaphore is never used?!?
            //
            // work around to skip the crash by initializing as zero
            DUST_THREAD_ASSERT(_s = dispatch_semaphore_create(0));
            platform_signal(n);
        }
        void close() {
            // clear whatever count might be left, see init()
            while(platform_try_wait(0));
            dispatch_release(_s);
        }
        void platform_wait()
        {
            dispatch_semaphore_wait(_s, DISPATCH_TIME_FOREVER);
        }
        void platform_signal(unsigned count)
        {
            while(count--) dispatch_semaphore_signal(_s);
        }
        bool platform_try_wait(unsigned long timeout)
        {
            dispatch_time_t dt = dispatch_time(DISPATCH_TIME_NOW,
                int64_t(timeout) * 1000 * 1000);
            return 0 == dispatch_semaphore_wait(_s, dt);
        }
#endif
    };

    // Basic RAII Mutex/Lock/TryLock (portable; uses Semaphore)
    //
    // FIXME: can we get priority-inheritance on all platforms of interest?
    //
    // NOTE: this implementation based on dispatch semaphore
    // is several times faster than pthread_mutex on macOS!
    struct Mutex
    {
        Mutex() : s(1) {}
        Mutex(Mutex const &) = delete;
        
        // sanity check that lock is free
        ~Mutex() { DUST_THREAD_ASSERT(s.tryWait()); }

        // This is the easy-use RAII lock
        struct Lock
        {
            Lock(Lock const &) = delete;
            Lock(Mutex & _m) : m(_m) { m.s.wait(); locked = true; }
            ~Lock() { if(locked) m.s.post(); }
            void abandon() { if(locked) m.s.post(); locked = false; }
        private:
            Mutex & m;
            bool locked;
        };

        // RAII trylock version
        struct TryLock
        {
            TryLock(TryLock const &) = delete;
            TryLock(Mutex & _m, unsigned long timeout = 0)
                : m(_m)
            { locked = m.s.tryWait(timeout); }

            ~TryLock() { if(locked) m.s.post(); }

            bool isLocked() { return locked; }
            void abandon() { if(locked) m.s.post(); locked = false; }
        private:
            Mutex & m;
            bool locked;
        };

    private:
        Semaphore s;
    };

    // Platform specific Thread class, since we'd need to rely
    // on "implementation defined behaviour" with C++11 anyway
    //
    // FIXME: rethink the priority stuff.. ideally we can control those
    // from outside the thread itself, but platforms make this inconvenient
    //
    struct Thread
    {
        Thread(Thread const &) = delete;

        // derived classes should implement this to actually do stuff
        virtual void run() = 0;
    
        Thread() : wantRealtime(false)
        {
#ifdef _WIN32
            avrtHandle = 0;

            memfence();
            _t = (HANDLE) ::_beginthreadex(0, 0, _internal_winThreadFunction,
                (void*) this, CREATE_SUSPENDED, &_id);
            DUST_THREAD_ASSERT(_t != (HANDLE) -1);
#endif
        }

        virtual ~Thread()
        {
#ifdef _WIN32
            ::CloseHandle(_t);
#endif
        }

        // If called with realtime=true before the thread is started,
        // then the Win32 wrapper will set "Pro Audio" task for MMCSS
        // and macOS wrapper will asks for RT scheduling.
        void setRealtime(bool realtime)
        {
            wantRealtime = realtime;
        }

        void start()
        {
#ifdef __APPLE__
#  if 1
            // play safe with thread affinity even if documentation doesn't
            // say anything about inheritance, set the affinite group to null
            pthread_create_suspended_np(
                &_t, NULL, _internal_thread_function, (void*) this);
            mach_port_t mach_thread = pthread_mach_thread_np(_t);
            // null-group = 0 = no affinity group
            thread_affinity_policy_data_t data = { 0 };
            thread_policy_set(mach_thread,
                THREAD_AFFINITY_POLICY, (thread_policy_t)&data, 1);
            thread_resume(mach_thread);
#  else
            pthread_create(&_t, 0, _internal_thread_function, (void*) this);
#  endif
#endif
#ifdef _WIN32
            DUST_THREAD_ASSERT(1 == ::ResumeThread(_t));
#endif
        }

        void wait()
        {
#ifdef _WIN32
            DUST_THREAD_ASSERT(WAIT_OBJECT_0 == ::WaitForSingleObject (_t, INFINITE));
#endif
#ifdef __APPLE__
            void * retval;
            pthread_join(_t, &retval);
#endif
        }

        // query the number of CPU cores on the system
        static unsigned getCpuCount();
    private:
        bool    wantRealtime;
#ifdef _WIN32
        // allow the thread-stub to do some setup on the thread
        static unsigned int __stdcall _internal_winThreadFunction(void * t);

        HANDLE _t;
        unsigned int _id;

        HANDLE  avrtHandle; // for MMCSS
#endif
#ifdef __APPLE__
        static void * _internal_thread_function(void *);
        pthread_t   _t;
#endif

    };

};

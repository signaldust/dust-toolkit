
#include "thread.h"

#ifdef _WIN32

// Helper function to count set bits in the processor mask.
// From http://msdn.microsoft.com/en-us/library/ms683194(VS.85).aspx example.
static DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
    DWORD i;

    for (i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest)?1:0);
        bitTest/=2;
    }

    return bitSetCount;
}
unsigned dust::Thread::getCpuCount()
{
    // Rest is also from http://msdn.microsoft.com/en-us/library/ms683194(VS.85).aspx
    // except all the useless crap is thrown away to make it a bit shorter

    typedef BOOL (WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

    LPFN_GLPI glpi;
    BOOL done = FALSE;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
    DWORD returnLength = 0;
    DWORD logicalProcessorCount = 0;
    DWORD processorCoreCount = 0;
    DWORD byteOffset = 0;

    glpi = (LPFN_GLPI) GetProcAddress(
                            GetModuleHandle(TEXT("kernel32")),
                            "GetLogicalProcessorInformation");
    if (NULL == glpi)
    {
        return 1; // not supported
    }

    while (!done)
    {
        DWORD rc = glpi(buffer, &returnLength);

        if (FALSE == rc)
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                if (buffer)
                    free(buffer);

                buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(
                        returnLength);

                if (NULL == buffer)
                {
                    // alloc failure
                    return 1;
                }
            }
            else
            {
                // some other error, whatever
                return 1;
            }
        }
        else
        {
            done = TRUE;
        }
    }

    ptr = buffer;

    while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength)
    {
        if (ptr->Relationship == RelationProcessorCore)
        {
            processorCoreCount++;

            // A hyperthreaded core supplies more than one logical processor.
            logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
        }
        byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }

    free(buffer);


    // always return logical processor count
    return logicalProcessorCount;

    //return allowHT ? logicalProcessorCount : processorCoreCount;
}

unsigned int __stdcall
dust::Thread::_internal_winThreadFunction(void * p)
{
    Thread * t = (Thread*)p;

    // set stuff to "Pro Audio" to get a proper real-time priority
    // this must be done in the actual thread because MMCSS API sucks.
    if(t->wantRealtime)
    {
        DWORD taskId = 0;
        t->avrtHandle = AvSetMmThreadCharacteristicsA("Pro Audio", &taskId);
        if(!t->avrtHandle)
        {
            char buf[123];
            sprintf(buf, "thread warning: Could not setup MMCSS, error code %d.\n",
                (int) GetLastError());
            ::OutputDebugStringA(buf);
        }
    }

    t->run();

    if(t->avrtHandle)
    {
        // not sure if this is useful, since we're going to kill the
        // thread immediately afterwards anyway.. but whatever
        AvRevertMmThreadCharacteristics(t->avrtHandle);
        t->avrtHandle = 0;
    }
    return 0;
}
#endif // WIN32


#ifdef __APPLE__

#include <sys/types.h>
#include <sys/sysctl.h>

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>

unsigned dust::Thread::getCpuCount()
{
    int count;
    size_t count_len = sizeof(count);
    sysctlbyname("hw.logicalcpu_max", &count, &count_len, NULL, 0);
    return count;
}

void * dust::Thread::_internal_thread_function(void *p)
{
    Thread * t = (Thread*) p;

    if(t->wantRealtime)
    {
        mach_timebase_info_data_t timebase_info;
        mach_timebase_info(&timebase_info);

        const uint64_t NANOS_PER_MSEC = 1000000ULL;
        double msToTicks = NANOS_PER_MSEC * (
            (double)timebase_info.denom / (double)timebase_info.numer);

        // FIXME: longer period is more efficient
        // so really should match this to audio blocks?
        double period = 3*msToTicks;

        thread_time_constraint_policy_data_t    policy;

        // apparently 75% and 85% here are reasonable
        // higher load will likely cause dropouts anyway
        policy.period = (uint32_t)(period);
        policy.computation = (uint32_t)(.75*period);
        policy.constraint = (uint32_t)(.85*period);
        policy.preemptible = 1;
        
        kern_return_t res = thread_policy_set(
            pthread_mach_thread_np(pthread_self()),
            THREAD_TIME_CONSTRAINT_POLICY,
            (thread_policy_t) &policy,
            THREAD_TIME_CONSTRAINT_POLICY_COUNT);

        // can't figure out where KERN_SUCCESS would be defined
        // but it happens to have a value of zero so whatever
        if(res)
        {
            debugPrint("thread warning: Couldn't set real-time scheduling.\n");
        }
    }

    t->run();

    return 0;
}

#endif // __APPLE__

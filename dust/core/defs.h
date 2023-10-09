
#pragma once

// set this to 1 to make debugPrint print stuff
#ifndef DUST_DEBUG_PRINT
#define DUST_DEBUG_PRINT 1
#endif

// set this to 1 to make DUST_TRACE produce output
// is only has an effect if DUST_DEBUG_PRINT is enabled
#ifndef DUST_DEBUG_TRACE
#define DUST_DEBUG_TRACE 1
#endif

// set this to 1 to make last redraw visible
// also enables debug printing of render times
#ifndef DUST_DEBUG_REDRAWS
#define DUST_DEBUG_REDRAWS 0
#endif

// set this to 1 to debug layouts
#ifndef DUST_DEBUG_LAYOUT
#define DUST_DEBUG_LAYOUT 0
#endif

// set this to 1 to enable scaling shortcut keys in window
// this is intended for layout debugging only
#ifndef DUST_SCALE_SHORTCUTS
#define DUST_SCALE_SHORTCUTS 0
#endif

#ifdef _WIN32
# define DUST_EXPORT __declspec(dllexport)
#else
# define DUST_EXPORT __attribute__((visibility("default")))
#endif

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# undef min
# undef max
#endif

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <functional>
#include <chrono>

#if defined(__i386__) || defined(__x86_64__) \
    || defined(_M_IX86) || defined(_M_AMD64)
# define DUST_ARCH_X86
#endif

#if defined(__ARM_ARCH_ISA_A64)
# define DUST_ARCH_ARM64
#endif

#if !defined(DUST_ARCH_X86) && !defined(DUST_ARCH_ARM64)
# warning 'Unknown ISA architecture'
#endif

#if defined(DUST_ARCH_X86)
# include <x86intrin.h>
#endif

#if defined(DUST_ARCH_ARM64)
# include "dust/libs/sse2neon.h"
#endif

// concatenate tokens, also with expansion of the parts
// this is used by debug traces and the macOS wrapper
#define DUST_CONCAT_TOKENS(x,y) x ## y
#define DUST_CONCAT_EXPAND(x,y) DUST_CONCAT_TOKENS(x, y)

// stringify stuff - two levels (like concat) to allow expand
#define DUST_STRINGIFY_WORKER(x) #x
#define DUST_STRINGIFY(x) DUST_STRINGIFY_WORKER(x)

// DEFER: This is just renamed (to avoid namespace clashes) from
// https://www.gingerbill.org/article/2015/08/19/defer-in-cpp/
// and using the macros above since we have them anyway
namespace dust
{
    template <typename F>
    struct privDefer {
        F f;
        privDefer(F f) : f(f) {}
        ~privDefer() { f(); }
    };
    
    template <typename F>
    privDefer<F> defer_func(F f) {
        return privDefer<F>(f);
    }
}
#define _DUST_DEFER(x) DUST_CONCAT_EXPAND(x, __COUNTER__)
#define dust_defer(code) \
    auto _DUST_DEFER(_dust_defer_) = dust::defer_func([&](){code;})
// END DEFER

namespace dust
{
    // typedef for notifications
    typedef std::function<void()>   Notify;

    // for use as a default noficication handler in widgets
    inline void doNothing() {}

    // RAII FPU state class .. only SSE version because .. well yeah
    class FPUState {
        unsigned int sse_control_store;
    public:
        // if truncate is true, we force truncation
        // otherwise we force rounding
        FPUState(bool truncate = false)
        {
#if defined(DUST_ARCH_X86)
            sse_control_store = _mm_getcsr();

            // bits:
            // 15 = flush to zero | 14:13 = round to zero | 6 = denormals are zero
            // or with exception masks 12:7 (exception flags are bits 5:0)
            _mm_setcsr((truncate ? 0xe040 : 0x8040) | 0x1f80 );
#endif
        }
        ~FPUState()
        {
#if defined(DUST_ARCH_X86)
            // clear bits 5:0 (exception flags) just in case
            _mm_setcsr(sse_control_store & (~0x3f));
#endif
        }
    };

    // printf into std::string - not particularly fast
#if defined(__clang__) || defined(__GNUC__)
        __attribute__ ((format (printf, 1, 2)))
#endif
    static inline std::string strf(const char * fmt, ...)
    {
        va_list va;

        // get length - we need the va_crap twice on x64
        va_start(va, fmt);
        int len = vsnprintf(0, 0, fmt, va);
        va_end(va);

        std::vector<char>   buf(len + 1);
        va_start(va, fmt);
        vsnprintf(&buf[0], len + 1, fmt, va);
        va_end(va);
        return std::string(&buf[0], len);
    }

    // debug break
    static inline void debugBreak()
    {
#ifdef __APPLE__
        __builtin_trap();
#endif
#ifdef _WIN32
        __debugbreak();
#endif
    }

#if (DUST_DEBUG_PRINT && DUST_DEBUG_TRACE)
    extern unsigned debugTraceNestingLevel;
#endif

    // the purpose of this wrapper is mostly to allow
    //  - easy removal of all debug code
    //  - easy redirection
    //
    // since this is only intended for debugs it doesn't
    // make any effort to actually do things efficiently
#if defined(__clang__) || defined(__GNUC__)
        __attribute__ ((format (printf, 1, 2)))
#endif
    static inline void debugPrint(const char * fmt, ...)
    {
#if DUST_DEBUG_PRINT
        std::string format = fmt;
# if DUST_DEBUG_TRACE
        // this trickery is necessary to keep the final print
        // atomic in case we're running multi-threaded
        format = strf("DEBUG:%*s %s\n",
            2*debugTraceNestingLevel, "", format.c_str());
# endif
        va_list args;
        va_start(args,fmt);
        vfprintf(stderr, format.c_str(), args);
        va_end(args);
#endif
    }

#ifdef _MSC_VER
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

#if (DUST_DEBUG_PRINT && DUST_DEBUG_TRACE)
    struct TraceHelper
    {
        TraceHelper(const char * file, unsigned line, const char * function)
        {
            debugPrint("(%s:%d) -- %s\n", file, line, function);
            ++debugTraceNestingLevel;
        }

        ~TraceHelper() { --debugTraceNestingLevel; }
    };
#define DUST_TRACE ::dust::TraceHelper DUST_CONCAT_EXPAND($dust$trace, __LINE__) \
    (__FILE__, __LINE__, __PRETTY_FUNCTION__);
#else
#define DUST_TRACE do {} while(0);
#endif

    static inline unsigned getTimeMs()
    {
        return std::chrono::time_point_cast<std::chrono::milliseconds>
            (std::chrono::steady_clock::now()).time_since_epoch().count();
    }
    
    static inline unsigned getTimeUs()
    {
        return std::chrono::time_point_cast<std::chrono::microseconds>
            (std::chrono::steady_clock::now()).time_since_epoch().count();
    }

#ifdef __APPLE__
    // returns a CFUrlRef for the application or plugin bundle
    // this is useful for plugins like AudioUnits
    void * getModuleBundleURL();
#endif

#ifdef _WIN32

    // These are for dealing with WinAPI: convert utf8 <-> utf16
    std::string to_u8(wchar_t const * in, size_t inLen = -1);
    std::wstring to_u16(char const * in, size_t inLen = -1);

    // wrappers for string
    static inline std::string to_u8(std::wstring const & in)
    { return to_u8(in.data(), in.size()); }
    static inline std::wstring to_u16(std::string const & in)
    { return to_u16(in.data(), in.size()); }
    
#endif

};

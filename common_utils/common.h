#pragma once

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <set>
#include <map>
#include <cstring>
#include <cassert>
#include <queue>
#include <x86intrin.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <immintrin.h>
#include <thread>
#include <functional>
#include <stdarg.h>
#include <utility>
#include <tuple>
#include <time.h>
#include <unordered_set>
#include <typeinfo>
#include "function_ref.h"

#define rep(i,l,r) for (int i=(l); i<=(r); i++)
#define repd(i,r,l) for (int i=(r); i>=(l); i--)
#define rept(i,c) for (__typeof((c).begin()) i=(c).begin(); i!=(c).end(); i++)

#define MEM_PREFETCH(x) _mm_prefetch((char*)(&(x)),_MM_HINT_T0);

#define PTRRAW(x) ((__typeof(x))(((uintptr_t)(x))&(~7)))
#define PTRTAG(x) (((uintptr_t)(x))&7)
#define PTRSET(x, b) ((__typeof(x))((((uintptr_t)(x))&(~7))|(b)))

#define likely(expr)     __builtin_expect((expr) != 0, 1)
#define unlikely(expr)   __builtin_expect((expr) != 0, 0)

#define TOKEN_PASTEx(x, y) x ## y
#define TOKEN_PASTE(x, y) TOKEN_PASTEx(x, y)

#define NO_RETURN __attribute__((__noreturn__))
#define ALWAYS_INLINE __attribute__((__always_inline__))
#define NO_INLINE __attribute__((__noinline__))
#define WARN_UNUSED __attribute__((__warn_unused_result__))

struct AutoTimer
{
	double *m_result;
    struct timespec m_start;

    static void gettime(struct timespec* dst)
    {
        int r = clock_gettime(CLOCK_MONOTONIC, dst);
        assert(r == 0);
        std::ignore = r;
    }

    static double tdiff(struct timespec* start, struct timespec* end) {
      return static_cast<double>(end->tv_sec - start->tv_sec) +
              double(1e-9) * static_cast<double>(end->tv_nsec - start->tv_nsec);
    }

    AutoTimer() : m_result(nullptr) { gettime(&m_start); }
    AutoTimer(double *result) : m_result(result) { gettime(&m_start); }
	~AutoTimer()
	{
        struct timespec m_end;
        gettime(&m_end);
        double timeElapsed = tdiff(&m_start, &m_end);
		if (m_result != nullptr) *m_result = timeElapsed;
		printf("AutoTimer: %.6lf second elapsed.\n", timeElapsed);
	}
};

template<typename T>
class AutoOutOfScope	
{
public:
   AutoOutOfScope(T& destructor) : m_destructor(destructor) { }
   ~AutoOutOfScope() { m_destructor(); }
private:
   T& m_destructor;
};
	
#define Auto_INTERNAL(Destructor, counter) \
    auto TOKEN_PASTE(auto_func_, counter) = [&]() { Destructor; }; \
    AutoOutOfScope<decltype(TOKEN_PASTE(auto_func_, counter))> TOKEN_PASTE(auto_, counter)(TOKEN_PASTE(auto_func_, counter))
	
#define Auto(Destructor) Auto_INTERNAL(Destructor, __COUNTER__)

struct ReleaseAssertFailure
{
    static void NO_RETURN Fire(const char *__assertion, const char *__file,
	                 unsigned int __line, const char *__function)
	{
        fprintf(stderr, "%s:%u: %s: Assertion `%s' failed.\n", __file, __line, __function, __assertion);
		abort();
	}
};

#define ReleaseAssert(expr)							\
     (static_cast <bool> (expr)						\
      ? void (0)							        \
      : ReleaseAssertFailure::Fire(#expr, __FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__))

#ifdef TESTBUILD
#define TestAssert(expr)                            \
    (static_cast <bool> (expr)						\
     ? void (0)							            \
     : ReleaseAssertFailure::Fire(#expr, __FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__))
#else
#define TestAssert(expr) (static_cast<void>(0))
#ifndef NDEBUG
static_assert(false, "NDEBUG should always be defined in non-testbuild");
#endif
#endif

#define AssertIff(a, b) assert((!!(a)) == (!!(b)))
#define AssertImp(a, b) assert((!(a)) || (b))
#define TestAssertIff(a, b) TestAssert((!!(a)) == (!!(b)))
#define TestAssertImp(a, b) TestAssert((!(a)) || (b))
#define ReleaseAssertIff(a, b) ReleaseAssert((!!(a)) == (!!(b)))
#define ReleaseAssertImp(a, b) ReleaseAssert((!(a)) || (b))

#ifdef TESTBUILD
const static bool x_isTestBuild = true;
#define TESTBUILD_ONLY(...) __VA_ARGS__
#else
const static bool x_isTestBuild = false;
#define TESTBUILD_ONLY(...)
#endif

#ifndef NDEBUG
const static bool x_isDebugBuild = true;
#define ALWAYS_INLINE_IN_NONDEBUG
#define DEBUG_ONLY(...) __VA_ARGS__
#else
const static bool x_isDebugBuild = false;
#define ALWAYS_INLINE_IN_NONDEBUG ALWAYS_INLINE
#define DEBUG_ONLY(...)
#endif

struct FalseOrNullptr
{
    operator bool() { return false; }
    template<typename T> operator T*() { return nullptr; }
};

#define CHECK(expr) do { if (unlikely(!(expr))) return FalseOrNullptr(); } while (false)
#define CHECK_ERR(expr) do { if (unlikely(!(expr))) { assert(thread_errorContext->HasError()); return FalseOrNullptr(); } } while (false)
#define RETURN_TRUE do { assert(!thread_errorContext->HasError()); return true; } while (false)
#define RETURN_FALSE do { assert(thread_errorContext->HasError()); return FalseOrNullptr(); } while (false)

#ifndef NDEBUG
template<typename T, typename U>
T assert_cast(U u)
{
    T t = dynamic_cast<T>(u);
    assert(t != nullptr);
    return t;
}
#else
#define assert_cast static_cast
#endif

class NonCopyable
{
public:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
};

class NonMovable
{
public:
    NonMovable(NonMovable&&) = delete;
    NonMovable& operator=(NonMovable&&) = delete;

protected:
    NonMovable() = default;
    ~NonMovable() = default;
};

// constexpr-if branch static_assert(false, ...) workaround:
//     static_assert(type_dependent_false<T>::value, ...)
//
template<class T> struct type_dependent_false : std::false_type {};

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
#include <unordered_map>
#include <typeinfo>
#include <random>
#include <mutex>
#include <optional>
#include "pp_common.h"
#include "function_ref.h"

#define MEM_PREFETCH(x) _mm_prefetch((char*)(&(x)),_MM_HINT_T0);

#define likely(expr)     __builtin_expect((expr) != 0, 1)
#define unlikely(expr)   __builtin_expect((expr) != 0, 0)

#define NO_RETURN __attribute__((__noreturn__))
#define ALWAYS_INLINE __attribute__((__always_inline__))
#define NO_INLINE __attribute__((__noinline__))
#define WARN_UNUSED __attribute__((__warn_unused_result__))
#define PACKED_STRUCT __attribute__((__packed__))

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
    auto PP_CAT(auto_func_, counter) = [&]() { Destructor; }; \
    AutoOutOfScope<decltype(PP_CAT(auto_func_, counter))> PP_CAT(auto_, counter)(PP_CAT(auto_func_, counter))
	
#define Auto(Destructor) Auto_INTERNAL(Destructor, __COUNTER__)

inline void NO_RETURN FireReleaseAssert(const char* assertionExpr, const char* assertionFile,
                                        unsigned int assertionLine, const char* assertionFunction)
{
    fprintf(stderr, "%s:%u: %s: Assertion `%s' failed.\n", assertionFile, assertionLine, assertionFunction, assertionExpr);
    abort();
}

#define ReleaseAssert(expr)							\
     (static_cast <bool> (expr)						\
      ? void (0)							        \
      : FireReleaseAssert(#expr, __FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__))

#ifdef TESTBUILD
#define TestAssert(expr) ReleaseAssert(expr)
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
constexpr bool x_isTestBuild = true;
#define TESTBUILD_ONLY(...) __VA_ARGS__
#else
constexpr bool x_isTestBuild = false;
#define TESTBUILD_ONLY(...)
#endif

#ifndef NDEBUG
constexpr bool x_isDebugBuild = true;
#define ALWAYS_INLINE_IN_NONDEBUG
#define DEBUG_ONLY(...) __VA_ARGS__
#else
constexpr bool x_isDebugBuild = false;
#define ALWAYS_INLINE_IN_NONDEBUG ALWAYS_INLINE
#define DEBUG_ONLY(...)
#endif

struct FalseOrNullptr
{
    operator bool() { return false; }
    template<typename T> operator T*() { return nullptr; }
};

#define CHECK(expr) do { if (unlikely(!(expr))) return FalseOrNullptr(); } while (false)
#define RETURN_TRUE do { assert(!thread_errorContext->HasError()); return true; } while (false)
#define RETURN_FALSE do { assert(thread_errorContext->HasError()); return FalseOrNullptr(); } while (false)

// Check for an error that we are unable to recover from
// Currently when the check fails we simply abort, but in the future we should
// gracefully free all memory and longjmp to a fault handler so the host application can continue
//
#define VM_FAIL_IF(expr, ...)                                                                                                   \
    do { if (unlikely((expr))) {                                                                                                \
        fprintf(stderr, "[FAIL] %s:%u: Irrecoverable error encountered due to unsatisfied check. VM is forced to abort.\n"      \
            , __FILE__, __LINE__);                                                                                              \
        __VA_OPT__(fprintf(stderr, "[FAIL] Message: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); )                  \
        fflush(stderr); abort();                                                                                                \
    } } while (false)

#define VM_FAIL_WITH_ERRNO_IF(expr, format, ...)                                                                                \
    do { if (unlikely((expr))) {                                                                                                \
        int macro_vm_check_fail_tmp = errno;                                                                                    \
        fprintf(stderr, "[FAIL] %s:%u: Irrecoverable error encountered due to unsatisfied check. VM is forced to abort.\n"      \
            , __FILE__, __LINE__);                                                                                              \
        fprintf(stderr, "[FAIL] Message: " format " (Error %d: %s)\n" __VA_OPT__(,) __VA_ARGS__                                 \
            , macro_vm_check_fail_tmp, strerror(macro_vm_check_fail_tmp));                                                      \
        fflush(stderr); abort();                                                                                                \
    } } while (false)

#define DETAIL_LOG_INFO(prefix, format, ...)                    \
    do { fprintf(stderr, prefix " %s:%u: " format "\n",         \
    __FILE__, __LINE__                                          \
    __VA_OPT__(,) __VA_ARGS__); } while (false)

#define DETAIL_LOG_INFO_WITH_ERRNO(prefix, format, ...)     \
    do { int macro_vm_log_warning_tmp = errno;              \
    fprintf(stderr, prefix " %s:%u: " format                \
    " (Error %d: %s)\n",                                    \
    __FILE__, __LINE__                                      \
    __VA_OPT__(,) __VA_ARGS__                               \
    , macro_vm_log_warning_tmp                              \
    , strerror(macro_vm_log_warning_tmp)); } while (false)

#define LOG_WARNING(format, ...) DETAIL_LOG_INFO("[WARNING]", format, __VA_ARGS__)
#define LOG_WARNING_WITH_ERRNO(format, ...) DETAIL_LOG_INFO_WITH_ERRNO("[WARNING]", format, __VA_ARGS__)

#define LOG_WARNING_IF(expr, format, ...)                   \
    do { if (unlikely((expr))) {                            \
        LOG_WARNING(format, __VA_ARGS__);                   \
    } } while (false)

#define LOG_WARNING_WITH_ERRNO_IF(expr, format, ...)        \
    do { if (unlikely((expr))) {                            \
        LOG_WARNING_WITH_ERRNO(format, __VA_ARGS__);        \
    } } while (false)

#define LOG_ERROR(format, ...) DETAIL_LOG_INFO("[ERROR]", format, __VA_ARGS__)
#define LOG_ERROR_WITH_ERRNO(format, ...) DETAIL_LOG_INFO_WITH_ERRNO("[ERROR]", format, __VA_ARGS__)

#define CHECK_LOG_ERROR(expr, ...)                                                           \
    do { if (unlikely(!(expr))) {                                                            \
        LOG_ERROR(PP_OPTIONAL_DEFAULT_PARAM("Check '" #expr "' has failed." , __VA_ARGS__)); \
        return FalseOrNullptr();                                                             \
    } } while (false)

#define CHECK_LOG_ERROR_WITH_ERRNO(expr, ...)                                                           \
    do { if (unlikely(!(expr))) {                                                                       \
        LOG_ERROR_WITH_ERRNO(PP_OPTIONAL_DEFAULT_PARAM("Check '" #expr "' has failed." , __VA_ARGS__)); \
        return FalseOrNullptr();                                                                        \
    } } while (false)

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

#define MAKE_NONCOPYABLE(ClassName)                 \
    ClassName(const ClassName&) = delete;           \
    ClassName& operator=(const ClassName&) = delete

#define MAKE_NONMOVABLE(ClassName)                  \
    ClassName(ClassName&&) = delete;                \
    ClassName& operator=(ClassName&&) = delete

// constexpr-if branch static_assert(false, ...) workaround:
//     static_assert(type_dependent_false<T>::value, ...)
//
template<class T> struct type_dependent_false : std::false_type {};

#define SUPRESS_FLOAT_EQUAL_WARNING(...)                    \
    _Pragma("clang diagnostic push")                        \
    _Pragma("clang diagnostic ignored \"-Wfloat-equal\"")   \
    __VA_ARGS__                                             \
    _Pragma("clang diagnostic pop")

#define COMPILER_REORDERING_BARRIER asm volatile("" ::: "memory")

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
#include <sstream>
#include <string>
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

inline void NO_RETURN NO_INLINE FireReleaseAssert(const char* assertionExpr, const char* assertionFile,
                                                  unsigned int assertionLine, const char* assertionFunction)
{
    fprintf(stderr, "%s:%u: %s: Assertion `%s' failed.\n", assertionFile, assertionLine, assertionFunction, assertionExpr);
    abort();
}

#define ReleaseAssert(expr)                                             \
   (static_cast <bool> (expr)                                           \
     ? void (0)                                                         \
     : FireReleaseAssert(#expr, __FILE__, __LINE__, __extension__ __PRETTY_FUNCTION__))

// DEBUG_ASSERTION defined means that Assert should be executed
// TEST_ASSERTION defined means that TestAssert should be executed
// Whether these two macros are defined is always derived from the config macros
//   NDEBUG/TESTBUILD/DISABLE_DEBUG_ASSERT/DISABLE_TEST_ASSERT
//
#ifdef DEBUG_ASSERTION
#error "You should never define DEBUG_ASSERTION yourself!"
#endif
#ifdef TEST_ASSERTION
#error "You should never define TEST_ASSERTION yourself!"
#endif

// DISABLE_TEST_ASSERT implies DISABLE_DEBUG_ASSERT
//
#ifdef DISABLE_TEST_ASSERT
#ifndef DISABLE_DEBUG_ASSERT
#define DISABLE_DEBUG_ASSERT
#endif
#endif

#ifndef NDEBUG
#define DEBUGBUILD
// Debug assertions may be disabled in debug build by defining 'DISABLE_DEBUG_ASSERT'
// This feature should only be used by deegen_common_snippets
//
#ifndef DISABLE_DEBUG_ASSERT
#define DEBUG_ASSERTION
#endif
#endif

#ifdef DEBUG_ASSERTION
#define Assert(expr) ReleaseAssert(expr)
#else
#define Assert(expr) (static_cast<void>(0))
#endif

#ifdef TESTBUILD
// Test assertions may be disabled in test builds by defining 'DISABLE_TEST_ASSERT'
// This feature should only be used by deegen_common_snippets
//
#ifndef DISABLE_TEST_ASSERT
#define TEST_ASSERTION
#endif
#else
#ifndef NDEBUG
static_assert(false, "NDEBUG should always be defined in non-testbuild");
#endif
#endif

#ifdef TEST_ASSERTION
#define TestAssert(expr) ReleaseAssert(expr)
#else
#define TestAssert(expr) (static_cast<void>(0))
#endif

#define AssertIff(a, b) Assert((!!(a)) == (!!(b)))
#define AssertImp(a, b) Assert((!(a)) || (b))
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

struct PerfTimer
{
    PerfTimer() { gettime(&m_start); }

    double GetElapsedTime()
    {
        struct timespec m_end;
        gettime(&m_end);
        return tdiff(&m_start, &m_end);
    }

private:
    struct timespec m_start;

    static void gettime(struct timespec* dst)
    {
        int r = clock_gettime(CLOCK_MONOTONIC, dst);
        Assert(r == 0);
        std::ignore = r;
    }

    static double tdiff(struct timespec* start, struct timespec* end)
    {
        return static_cast<double>(end->tv_sec - start->tv_sec) +
            double(1e-9) * static_cast<double>(end->tv_nsec - start->tv_nsec);
    }
};

struct AutoTimer
{
    AutoTimer() : m_result(nullptr), m_timer() { }
    AutoTimer(double* result) : m_result(result), m_timer() { }

    ~AutoTimer()
    {
        double timeElapsed = m_timer.GetElapsedTime();
        if (m_result != nullptr)
        {
            *m_result = timeElapsed;
        }
        else
        {
            printf("AutoTimer: %.6lf second elapsed.\n", timeElapsed);
        }
    }

private:
    double* m_result;
    PerfTimer m_timer;
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

#define Auto_INTERNAL(counter, ...) \
    auto PP_CAT(macro_auto_func_, counter) = [&]() { __VA_ARGS__; }; \
    AutoOutOfScope<decltype(PP_CAT(macro_auto_func_, counter))> PP_CAT(macro_auto_var_, counter)(PP_CAT(macro_auto_func_, counter))
	
#define Auto(...) Auto_INTERNAL(__COUNTER__, __VA_ARGS__)

struct FalseOrNullptr
{
    operator bool() { return false; }
    template<typename T> operator T*() { return nullptr; }
};

#define CHECK(expr) do { if (unlikely(!(expr))) return FalseOrNullptr(); } while (false)
#define RETURN_TRUE do { Assert(!thread_errorContext->HasError()); return true; } while (false)
#define RETURN_FALSE do { Assert(thread_errorContext->HasError()); return FalseOrNullptr(); } while (false)

// Returns an intentionally uninitialized value.
// For use in situations where we want to explicitly not initialize a value for performance reasons
//
template<typename T>
T WARN_UNUSED ALWAYS_INLINE Undef()
{
    // Trivially default constructible means that constructing T performs no action,
    // so we are indeed giving an uninitialized value without causing any runtime overhead
    // https://en.cppreference.com/w/cpp/language/default_constructor#Trivial_default_constructor
    //
    static_assert(std::is_trivially_default_constructible_v<T>);
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_trivially_move_constructible_v<T>);
    static_assert(std::is_trivially_move_assignable_v<T>);
    // Ignore uninitialized error since that's the whole point of this function
    //
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
    T t;
    return t;
#pragma clang diagnostic pop
}

// Check for an error that we are unable to recover from
// Currently when the check fails we simply abort, but in the future we should
// gracefully free all memory and longjmp to a fault handler so the host application can continue
//
#define VM_FAIL_IF(expr, ...)                                                                                                   \
    do { if (unlikely((expr))) {                                                                                                \
        fprintf(stderr, "[FAIL] %s:%u: Irrecoverable error encountered due to unsatisfied check. VM is forced to abort.\n"      \
            , __FILE__, static_cast<unsigned int>(__LINE__));                                                                   \
        __VA_OPT__(fprintf(stderr, "[FAIL] Message: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); )                  \
        fflush(stderr); abort();                                                                                                \
    } } while (false)

#define VM_FAIL_WITH_ERRNO_IF(expr, format, ...)                                                                                \
    do { if (unlikely((expr))) {                                                                                                \
        int macro_vm_check_fail_tmp = errno;                                                                                    \
        fprintf(stderr, "[FAIL] %s:%u: Irrecoverable error encountered due to unsatisfied check. VM is forced to abort.\n"      \
            , __FILE__, static_cast<unsigned int>(__LINE__));                                                                   \
        fprintf(stderr, "[FAIL] Message: " format " (Error %d: %s)\n" __VA_OPT__(,) __VA_ARGS__                                 \
            , macro_vm_check_fail_tmp, strerror(macro_vm_check_fail_tmp));                                                      \
        fflush(stderr); abort();                                                                                                \
    } } while (false)

#define DETAIL_LOG_INFO(prefix, format, ...)                    \
    do { fprintf(stderr, prefix " %s:%u: " format "\n",         \
    __FILE__, static_cast<unsigned int>(__LINE__)               \
    __VA_OPT__(,) __VA_ARGS__); } while (false)

#define DETAIL_LOG_INFO_WITH_ERRNO(prefix, format, ...)     \
    do { int macro_vm_log_warning_tmp = errno;              \
    fprintf(stderr, prefix " %s:%u: " format                \
    " (Error %d: %s)\n",                                    \
    __FILE__, static_cast<unsigned int>(__LINE__)           \
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
    static_assert(std::is_pointer_v<T> && std::is_pointer_v<U>);
    T t = dynamic_cast<T>(u);
    Assert(t != nullptr);
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

// Useful for classes with virtual destructor (resulting in the default copy/move operators disabled),
// but are nontheless default copyable/movable
//
#define MAKE_DEFAULT_COPYABLE(ClassName)            \
    ClassName(const ClassName&) = default;          \
    ClassName& operator=(const ClassName&) = default

#define MAKE_DEFAULT_MOVABLE(ClassName)             \
    ClassName(ClassName&&) = default;               \
    ClassName& operator=(ClassName&&) = default

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

#define FOLD_CONSTEXPR(...) (__builtin_constant_p(__VA_ARGS__) ? (__VA_ARGS__) : (__VA_ARGS__))

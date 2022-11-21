#pragma once

#include "common_utils.h"
#include "tvalue.h"

class CoroutineRuntimeContext;

class DeegenLibFuncCommonAPIs
{
public:
    void NO_RETURN Return();
    void NO_RETURN Return(TValue);
    void NO_RETURN Return(TValue, TValue);
    void NO_RETURN Return(TValue, TValue, TValue);
    void NO_RETURN Return(TValue, TValue, TValue, TValue);
    void NO_RETURN Return(TValue, TValue, TValue, TValue, TValue);
    void NO_RETURN Return(TValue, TValue, TValue, TValue, TValue, TValue);
    void NO_RETURN Return(TValue, TValue, TValue, TValue, TValue, TValue, TValue);
    void NO_RETURN Return(TValue, TValue, TValue, TValue, TValue, TValue, TValue, TValue);

    // Returns a range of values. 'retBegin' must be a pointer >= the base of the current stack frame.
    //
    void NO_RETURN ReturnValueRange(TValue* retBegin, size_t numRets);

    // Return to the caller of 'hdr'.
    // This is currently only used by internal library, and it is likely not useful for users due to its bizzare semantics.
    // While this is coceptually similar to C longjmp, at implementation level it has nothing to do with longjmp.
    //
    void NO_RETURN LongJump(StackFrameHeader* hdr, TValue* retBegin, size_t numRets);

    void NO_RETURN ThrowError(TValue msg);
    void NO_RETURN ThrowError(const char* msg);
    void NO_RETURN MakeInPlaceCall(TValue* argsBegin, size_t numArgs, void* returnContinuation);

    // Yield the current coroutine and transfer control to the target coroutine.
    // This function only transfer the control, it does not check anything or maintain anything.
    // The user is responsible for doing all the bookkeeping that e.g., tracks if a coroutine is alive, etc.
    //
    void NO_RETURN ALWAYS_INLINE CoroSwitch(CoroutineRuntimeContext* coro, TValue* coroStackBase, size_t numArgs);
};

template<typename CRTP>
class DeegenUserLibFunctionBase : public DeegenLibFuncCommonAPIs
{
public:
    static constexpr void* DeegenInternal_GetFakeEntryPointAddress()
    {
        return FOLD_CONSTEXPR(reinterpret_cast<void*>(ImplWrapper));
    }

protected:
    size_t GetNumArgs()
    {
        return m_numArgs;
    }

    TValue GetArg(size_t i)
    {
        assert(i < GetNumArgs());
        return m_stackBase[i];
    }

    TValue* GetStackBase()
    {
        return m_stackBase;
    }

    StackFrameHeader* GetStackFrameHeader()
    {
        return reinterpret_cast<StackFrameHeader*>(m_stackBase - x_numSlotsForStackFrameHeader);
    }

    CoroutineRuntimeContext* GetCurrentCoroutine()
    {
        return m_coroCtx;
    }

private:
    static void NO_RETURN ImplWrapper(CoroutineRuntimeContext* coroCtx, TValue* stackBase, size_t numArgs)
    {
        CRTP ctx;
        DeegenUserLibFunctionBase<CRTP>& self = *static_cast<DeegenUserLibFunctionBase<CRTP>*>(&ctx);
        self.m_coroCtx = coroCtx;
        self.m_stackBase = stackBase;
        self.m_numArgs = numArgs;
        ctx.Impl();
    }

    CoroutineRuntimeContext* m_coroCtx;
    size_t m_numArgs;
    TValue* m_stackBase;
};

template<typename CRTP>
class DeegenUserLibRetContBase : public DeegenLibFuncCommonAPIs
{
public:
    static constexpr void* DeegenInternal_GetFakeEntryPointAddress()
    {
        return FOLD_CONSTEXPR(reinterpret_cast<void*>(ImplWrapper));
    }

protected:
    TValue GetArg(size_t i)
    {
        return m_stackBase[i];
    }

    TValue* GetStackBase()
    {
        return m_stackBase;
    }

    StackFrameHeader* GetStackFrameHeader()
    {
        return reinterpret_cast<StackFrameHeader*>(m_stackBase - x_numSlotsForStackFrameHeader);
    }

    TValue* GetReturnValuesBegin()
    {
        return m_retStart;
    }

    size_t GetNumReturnValues()
    {
        return m_numRets;
    }

    CoroutineRuntimeContext* GetCurrentCoroutine()
    {
        return m_coroCtx;
    }

private:
    static void NO_RETURN ImplWrapper(CoroutineRuntimeContext* coroCtx, TValue* stackBase, TValue* retStart, size_t numRets)
    {
        CRTP ctx;
        DeegenUserLibRetContBase<CRTP>& self = *static_cast<DeegenUserLibRetContBase<CRTP>*>(&ctx);
        self.m_coroCtx = coroCtx;
        self.m_stackBase = stackBase;
        self.m_retStart = retStart;
        self.m_numRets = numRets;
        ctx.Impl();
    }

    CoroutineRuntimeContext* m_coroCtx;
    TValue* m_stackBase;
    TValue* m_retStart;
    size_t m_numRets;
};

namespace detail
{

struct deegen_lib_func_definition_info_descriptor
{
    void* impl;
    void* wrapper;
    bool isRc;
};

template<int v> struct deegen_end_lib_func_definitions_macro_used : deegen_end_lib_func_definitions_macro_used<v-1> { };
template<> struct deegen_end_lib_func_definitions_macro_used<-1> { static constexpr bool value = false; };

template<int v> struct deegen_lib_func_definition_info : deegen_lib_func_definition_info<v-1> { };
template<> struct deegen_lib_func_definition_info<-1> {
    static constexpr std::array<deegen_lib_func_definition_info_descriptor, 0> value { };
};

}   // namespace detail

#define DEEGEN_FORWARD_DECLARE_LIB_FUNC(name) extern "C" void DeegenInternal_UserLibFunctionTrueEntryPoint_ ## name()
#define DEEGEN_CODE_POINTER_FOR_LIB_FUNC(name) (FOLD_CONSTEXPR(reinterpret_cast<void*>(DeegenInternal_UserLibFunctionTrueEntryPoint_ ## name)))
#define DEEGEN_FORWARD_DECLARE_LIB_FUNC_RETURN_CONTINUATION(name) extern "C" void DeegenInternal_UserLibFunctionReturnContinuationTrueEntryPoint_ ## name()
#define DEEGEN_LIB_FUNC_RETURN_CONTINUATION(name) (FOLD_CONSTEXPR(reinterpret_cast<void*>(DeegenInternal_UserLibFunctionReturnContinuationTrueEntryPoint_ ## name)))

#ifndef DEEGEN_ANNOTATED_SOURCE_FOR_USER_BUILTIN_LIBRARY
#define DEEGEN_ANNOTATED_SOURCE_FOR_USER_BUILTIN_LIBRARY_GUARD static_assert(false, "This macro must not be used except inside annotated source code for builtin library");
#else
#define DEEGEN_ANNOTATED_SOURCE_FOR_USER_BUILTIN_LIBRARY_GUARD
#endif

// DEEGEN_END_LIB_FUNC_DEFINITIONS:
//   Must be used exactly once per translation unit
//   Must be put after all uses of 'DEEGEN_DEFINE_LIB_FUNC' and 'DEEGEN_DEFINE_LIB_FUNC_CONTINUATION'
//
#define DEEGEN_END_LIB_FUNC_DEFINITIONS DEEGEN_END_LIB_FUNC_DEFINITIONS_IMPL(__COUNTER__)
#define DEEGEN_END_LIB_FUNC_DEFINITIONS_IMPL(counter)                                                                                                                                                       \
    DEEGEN_ANNOTATED_SOURCE_FOR_USER_BUILTIN_LIBRARY_GUARD                                                                                                                                                  \
    static_assert(!detail::deegen_end_lib_func_definitions_macro_used<counter>::value, "DEEGEN_END_LIB_FUNC_DEFINITIONS should only be used once per translation unit, after all DEEGEN_DEFINE_LIB_FUNC");  \
    namespace detail { template<> struct deegen_end_lib_func_definitions_macro_used<counter + 1> { static constexpr bool value = true; }; }                                                                 \
    __attribute__((__used__)) inline constexpr auto x_deegen_impl_all_lib_func_defs_in_this_tu = detail::std_array_to_llvm_friendly_array(detail::deegen_lib_func_definition_info<counter>::value);

// DEEGEN_DEFINE_LIB_FUNC:
//   Define a library function
//
#define DEEGEN_DEFINE_LIB_FUNC(name) DEEGEN_DEFINE_LIB_FUNC_IMPL(name, __COUNTER__)
#define DEEGEN_DEFINE_LIB_FUNC_IMPL(name, counter)                                                                                                                          \
    DEEGEN_ANNOTATED_SOURCE_FOR_USER_BUILTIN_LIBRARY_GUARD                                                                                                                  \
    static_assert(!detail::deegen_end_lib_func_definitions_macro_used<counter>::value, "DEEGEN_DEFINE_LIB_FUNC should not be used after DEEGEN_END_LIB_FUNC_DEFINITIONS");  \
    namespace {                                                                                                                                                             \
        /* define in anonymous namespace to trigger compiler warning if user forgot to write 'DEEGEN_END_LIB_FUNC_DEFINITIONS' at the end of the file */                    \
        class DeegenUserLibFuncDefinitionImpl_ ## name final : public DeegenUserLibFunctionBase< DeegenUserLibFuncDefinitionImpl_ ## name > {                               \
        public: void NO_RETURN ALWAYS_INLINE Impl();                                                                                                                        \
        };                                                                                                                                                                  \
    }   /* anonymous namespace */                                                                                                                                           \
    DEEGEN_FORWARD_DECLARE_LIB_FUNC(name);                                                                                                                                  \
    namespace detail {                                                                                                                                                      \
        template<> struct deegen_lib_func_definition_info<counter> {                                                                                                        \
            static constexpr auto value = constexpr_std_array_concat(                                                                                                       \
                deegen_lib_func_definition_info<counter-1>::value, std::array<deegen_lib_func_definition_info_descriptor, 1> { deegen_lib_func_definition_info_descriptor { \
                    .impl = DeegenUserLibFuncDefinitionImpl_ ## name :: DeegenInternal_GetFakeEntryPointAddress(),                                                          \
                    .wrapper = DEEGEN_CODE_POINTER_FOR_LIB_FUNC(name),                                                                                                      \
                    .isRc = false }});                                                                                                                                      \
        };                                                                                                                                                                  \
    }   /* namespace detail */                                                                                                                                              \
    void NO_RETURN ALWAYS_INLINE DeegenUserLibFuncDefinitionImpl_ ## name :: Impl()

// DEEGEN_DEFINE_LIB_FUNC_CONTINUATION:
//   Define a library function's continuation after a MakeCall
//
#define DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(name) DEEGEN_DEFINE_LIB_FUNC_CONTINUATION_IMPL(name, __COUNTER__)
#define DEEGEN_DEFINE_LIB_FUNC_CONTINUATION_IMPL(name, counter)                                                                                                             \
    DEEGEN_ANNOTATED_SOURCE_FOR_USER_BUILTIN_LIBRARY_GUARD                                                                                                                  \
    static_assert(!detail::deegen_end_lib_func_definitions_macro_used<counter>::value,                                                                                      \
        "DEEGEN_DEFINE_LIB_FUNC_CONTINUATION should not be used after DEEGEN_END_LIB_FUNC_DEFINITIONS");                                                                    \
    namespace {                                                                                                                                                             \
        /* define in anonymous namespace to trigger compiler warning if user forgot to write 'DEEGEN_END_LIB_FUNC_DEFINITIONS' at the end of the file */                    \
        class DeegenUserLibFuncContinuationDefinitionImpl_ ## name final : public DeegenUserLibRetContBase< DeegenUserLibFuncContinuationDefinitionImpl_ ## name > {        \
        public: void NO_RETURN ALWAYS_INLINE Impl();                                                                                                                        \
        };                                                                                                                                                                  \
    }   /* anonymous namespace */                                                                                                                                           \
    DEEGEN_FORWARD_DECLARE_LIB_FUNC_RETURN_CONTINUATION(name);                                                                                                              \
    namespace detail {                                                                                                                                                      \
        template<> struct deegen_lib_func_definition_info<counter> {                                                                                                        \
            static constexpr auto value = constexpr_std_array_concat(                                                                                                       \
                deegen_lib_func_definition_info<counter-1>::value, std::array<deegen_lib_func_definition_info_descriptor, 1> { deegen_lib_func_definition_info_descriptor { \
                    .impl = DeegenUserLibFuncContinuationDefinitionImpl_ ## name :: DeegenInternal_GetFakeEntryPointAddress(),                                              \
                    .wrapper = DEEGEN_LIB_FUNC_RETURN_CONTINUATION(name),                                                                                                   \
                    .isRc = true }});                                                                                                                                       \
        };                                                                                                                                                                  \
    }   /* namespace detail */                                                                                                                                              \
    void NO_RETURN ALWAYS_INLINE DeegenUserLibFuncContinuationDefinitionImpl_ ## name :: Impl()

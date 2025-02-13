#pragma once

#include "common_utils.h"
#include "dfg_codegen_protocol.h"
#include "x64_register_info.h"

namespace dfg {

struct JITCodeSizeInfo
{
    JITCodeSizeInfo()
    {
        Reset();
    }

    void Reset()
    {
        m_fastPathLength = 0;
        m_slowPathLength = 0;
        m_dataSecLength = 0;
    }

    void Update(const CodegenFnJitCodeSizeInfo& info)
    {
        m_fastPathLength += info.m_fastPathCodeLen;
        m_slowPathLength += info.m_slowPathCodeLen;
        m_dataSecLength = RoundUpToPowerOfTwoMultipleOf(m_dataSecLength, static_cast<uint32_t>(info.m_dataSecAlignment));
        m_dataSecLength += info.m_dataSecLen;
    }

    uint32_t m_fastPathLength;
    uint32_t m_slowPathLength;
    uint32_t m_dataSecLength;
};

#define DFG_CODEGEN_OPERATION_LIST                  \
    /* Move between regs */                         \
    CodegenRegMove                                  \
    /* Load a spilled value to reg */               \
  , CodegenRegLoad                                  \
    /* Spill a register to stack */                 \
  , CodegenRegSpill                                 \
    /* Codegen a CodegenImplFn operation */         \
    /* that supports reg alloc */                   \
  , CodegenOpRegAllocEnabled                        \
    /* Codegen a CodegenImplFn operation */         \
    /* that does not support reg alloc */           \
  , CodegenOpRegAllocDisabled                       \
    /* Codegen a CustomBuiltinNodeCodegenImplFn */  \
    /* operation that supports reg alloc */         \
  , CodegenCustomOpRegAllocEnabled                  \
    /* Codegen a CustomBuiltinNodeCodegenImplFn */  \
    /* operation that does not support reg alloc */ \
  , CodegenCustomOpRegAllocDisabled                 \


// Forward declare all structs
//
#define macro(cgOp) struct cgOp;
PP_FOR_EACH(macro, DFG_CODEGEN_OPERATION_LIST)
#undef macro

// The base class of all codegen ops
// Note that all CodegenOp classes must be trivially copyable, since currently we simply save the list of operations by memcpy.
//
struct CodegenOpBase
{
public:
    enum class Kind : uint8_t
    {
        DFG_CODEGEN_OPERATION_LIST
    };

    // Usage:
    //     ... = opBase.Dispatch([&]<typename T>(T* op) ALWAYS_INLINE { ... });
    //
    // where 'op' will be the actual CodegenOp type of 'opBase'
    //
    template<typename Lambda>
    std::invoke_result_t<decltype(Lambda::template operator()<PP_TUPLE_GET_1((DFG_CODEGEN_OPERATION_LIST))>), Lambda, PP_TUPLE_GET_1((DFG_CODEGEN_OPERATION_LIST))*>
        ALWAYS_INLINE Dispatch(const Lambda& lambda)
    {
        // Check that all specializations have the same return type
        //
        using RetTy = std::invoke_result_t<decltype(Lambda::template operator()<PP_TUPLE_GET_1((DFG_CODEGEN_OPERATION_LIST))>), Lambda, PP_TUPLE_GET_1((DFG_CODEGEN_OPERATION_LIST))*>;
#define macro(cgOp) static_assert(std::is_same_v<RetTy, std::invoke_result_t<decltype(Lambda::template operator()<cgOp>), Lambda, cgOp*>>);
        PP_FOR_EACH(macro, DFG_CODEGEN_OPERATION_LIST)
#undef macro

        // Dispatch based on actual type
        // Note that we can't use static_cast since the derived classes haven't been defined yet,
        // but using a reinterpret_cast is safe because this class must be at offset 0 (which is the precondition of this scheme),
        //
        switch (GetCodegenOpKind())
        {
#define macro(cgOp) case Kind::cgOp: { return lambda.template operator()<cgOp>(reinterpret_cast<cgOp*>(this)); }
            PP_FOR_EACH(macro, DFG_CODEGEN_OPERATION_LIST)
#undef macro
        }   /*switch*/
    }

    Kind GetCodegenOpKind() const { return m_codegenOpKind; }

protected:
    template<typename T>
    static constexpr Kind GetCodegenOp()
    {
#define macro(cgOp)                                 \
        if constexpr(std::is_same_v<T, cgOp>) {     \
            return Kind::cgOp;                      \
        } else

        PP_FOR_EACH(macro, DFG_CODEGEN_OPERATION_LIST)
#undef macro

        // else clause
        {
            static_assert(type_dependent_false<T>::value, "unexpected CodegenOp!");
        }
    }

    template<typename T>
    CodegenOpBase([[maybe_unused]] T* thisPtr)
        : m_codegenOpKind(GetCodegenOp<T>())
    {
        static_assert(std::is_base_of_v<CodegenOpBase, T>);
        static_assert(offsetof_base_v<CodegenOpBase, T> == 0);
        TestAssert(static_cast<void*>(this) == static_cast<void*>(thisPtr));
    }

private:
    Kind m_codegenOpKind;
};

}   // namespace dfg

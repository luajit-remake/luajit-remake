#pragma once

#include "common.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Module.h"

#include "cxx_symbol_demangler.h"

namespace dast
{

template<typename T>
inline llvm::Type* WARN_UNUSED llvm_type_of(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    if constexpr(std::is_same_v<T, void>)
    {
        return Type::getVoidTy(ctx);
    }
    else if constexpr(std::is_same_v<T, bool>)
    {
        // Note that despite that bool has type 'i1' (which we return from this function), it is stored in 'i8'
        //
        return IntegerType::get(ctx, 1 /*numBits*/);
    }
    else if constexpr(std::is_integral_v<T>)
    {
        return IntegerType::get(ctx, static_cast<unsigned>(sizeof(T)) * 8 /*numBits*/);
    }
    else if constexpr(std::is_same_v<T, float>)
    {
        return Type::getFloatTy(ctx);
    }
    else if constexpr(std::is_same_v<T, double>)
    {
        return Type::getDoubleTy(ctx);
    }
    else
    {
        static_assert(std::is_pointer_v<T>, "unhandled type");
        using PointeeType = std::remove_pointer_t<T>;
        // Special case:
        // (1) We store a bool value in i8. This is required to maintain compatibility with C++ ABI.
        //     So, type for 'bool' will be i1. But type for 'bool*' will be 'i8*', 'bool**' be 'i8**', etc
        // (2) void* has type i8* instead of void* in LLVM. (void** is i8**, etc).
        //     This is also required to maintain compatibility with C++ ABI.
        //
        if constexpr(std::is_same_v<PointeeType, bool> || std::is_same_v<PointeeType, void>)
        {
            return Type::getInt8PtrTy(ctx);
        }
        else
        {
            return llvm_type_of<PointeeType>(ctx)->getPointerTo();
        }
    }
}

// For value that is directly forwarded (e.g., passed from the caller to the callee), clang generates the following pattern:
//   %0 = alloca T
//   store val, %0
//   %1 = load %0
//
// This function deals with (and can only handle) this simple pattern. Given %1, it returns 'val'.
//
inline llvm::Value* WARN_UNUSED LLVMBacktrackForSourceOfForwardedValue(llvm::LoadInst* loadInst)
{
    using namespace llvm;
    ReleaseAssert(loadInst != nullptr);
    Value* loadFrom = loadInst->getPointerOperand();
    AllocaInst* local = dyn_cast<AllocaInst>(loadFrom);
    ReleaseAssert(local != nullptr);

    // This function only deals with the simple case where the alloca is only used by is one LOAD and one STORE and nothing else,
    // so it should have exactly two users
    //
    {
        auto uit = local->user_begin();
        ReleaseAssert(uit != local->user_end());
        uit++;
        ReleaseAssert(uit != local->user_end());
        uit++;
        ReleaseAssert(uit == local->user_end());
    }

    User* u1 = *(local->user_begin());
    User* u2 = *(++local->user_begin());
    StoreInst* storeInst = dyn_cast<StoreInst>(u1);
    if (storeInst == nullptr)
    {
        ReleaseAssert(dyn_cast<LoadInst>(u1) == loadInst);
        storeInst = dyn_cast<StoreInst>(u2);
        ReleaseAssert(storeInst != nullptr);
    }
    else
    {
        ReleaseAssert(dyn_cast<LoadInst>(u2) == loadInst);
    }

    ReleaseAssert(storeInst->getPointerOperand() == local);
    Value* valStored = storeInst->getValueOperand();
    ReleaseAssert(valStored != nullptr);
    return valStored;
}

template<typename T>
T WARN_UNUSED GetValueOfLLVMConstantInt(llvm::Constant* value)
{
    using namespace llvm;
    static_assert(!std::is_same_v<T, bool> && std::is_integral_v<T>);
    static_assert(sizeof(T) <= 8);
    ReleaseAssert(value != nullptr);
    ReleaseAssert(value->getType() == llvm_type_of<T>(value->getContext()));
    ConstantInt* ci = dyn_cast<ConstantInt>(value);
    ReleaseAssert(ci != nullptr);
    ReleaseAssert(ci->getBitWidth() == sizeof(T) * 8);
    if constexpr(std::is_signed_v<T>)
    {
        return static_cast<T>(ci->getSExtValue());
    }
    else
    {
        return static_cast<T>(ci->getZExtValue());
    }
}

}   // namespace dast

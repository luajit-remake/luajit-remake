#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"

namespace dast
{

class CXXLambda;

// A map from each Deegen API call to the vector of lambdas used by that call
//
using DAPILambdaMap = std::unordered_map<llvm::CallInst*, std::vector<CXXLambda>>;

// A lambda passed to our API
//
class CXXLambda
{
public:
    CXXLambda() : m_code(nullptr), m_capture(nullptr) { }
    CXXLambda(llvm::Function* code, llvm::AllocaInst* capture) : m_code(code), m_capture(capture) { }

    // The function for the logic of the lambda
    //
    llvm::Function* m_code;
    // The 'alloca' in the lambda creator that contains the capture of the function
    //
    llvm::AllocaInst* m_capture;

    llvm::Type* WARN_UNUSED ReturnType()
    {
        return m_code->getReturnType();
    }

    static DAPILambdaMap WARN_UNUSED ParseDastLambdaMap(llvm::Module* module)
    {
        using namespace llvm;
        DAPILambdaMap result;
        for (Function& func : *module)
        {
            if (IsLambdaPreserverFunction(&func))
            {
                Function* code = GetCodeFromLLVM(&func);
                ReleaseAssert(code != nullptr);
                AllocaInst* capture = GetCaptureFromLLVM(&func);
                ReleaseAssert(capture != nullptr);
                CallInst* user = GetAPIUserOfCaptureAlloca(capture);
                ReleaseAssert(user != nullptr);
                result[user].push_back({ code, capture });
            }
        }
        return result;
    }

private:
    static constexpr const char* x_lambda_preserver_signature = "void detail::PreserveLambdaInfo<";

    static bool IsLambdaPreserverFunction(llvm::Function* func)
    {
        return IsCXXSymbol(func->getName().str()) && GetDemangledName(func).starts_with(x_lambda_preserver_signature);
    }

    // 'func' must be a 'PreserveLambdaInfo' function
    // The IR looks like the following:
    //
    //   define internal void @"_ZN9DeegenAPI6detail18PreserveLambdaInfoIZ6testfniiiE3$_0EEvRKT_"(ptr noundef nonnull align 8 dereferenceable(16) %0) #15 {
    //      %2 = alloca ptr, align 8
    //      store ptr %0, ptr %2, align 8, !tbaa !5
    //      %3 = load ptr, ptr %2, align 8, !tbaa !5
    //      call void @_ZN9DeegenAPI6detail22ImplPreserveLambdaInfoEPKvS2_(ptr noundef %3, ptr noundef @"_ZN9DeegenAPI6detail31lambda_functor_member_pointer_vIZ6testfniiiE3$_0EE")
    //      ret void
    //   }
    //
    // We want to parse out and return the first i64 in the second parameter in the call.
    //
    static llvm::Function* WARN_UNUSED GetCodeFromLLVM(llvm::Function* func)
    {
        using namespace llvm;
        ReleaseAssert(func->getBasicBlockList().size() == 1);
        LLVMContext& ctx = func->getContext();
        BasicBlock& bb = func->getEntryBlock();
        bool foundCallInst = false;
        Function* result = nullptr;
        for (Instruction& inst : bb)
        {
            if (CallInst* callInst = dyn_cast<CallInst>(&inst))
            {
                ReleaseAssert(!foundCallInst);
                foundCallInst = true;

                Function* callee = callInst->getCalledFunction();
                ReleaseAssert(callee != nullptr);
                ReleaseAssert(GetDemangledName(callee) == "detail::ImplPreserveLambdaInfo(void const*, void const*)");
                ReleaseAssert(callee->arg_size() == 2);
                Value* arg = callInst->getArgOperand(1);
                GlobalVariable* gv = dyn_cast<GlobalVariable>(arg);
                ReleaseAssert(gv != nullptr);
                ReleaseAssert(gv->isConstant());
                Constant* iv =  gv->getInitializer();
                ConstantStruct* ivs = dyn_cast<ConstantStruct>(iv);
                ReleaseAssert(ivs != nullptr);
                StructType* cstType = dyn_cast<StructType>(ivs->getType());
                ReleaseAssert(cstType != nullptr);
                ReleaseAssert(cstType->getNumElements() == 2);
                ReleaseAssert(cstType->getStructElementType(0) == Type::getInt64Ty(ctx));
                ReleaseAssert(cstType->getStructElementType(1) == Type::getInt64Ty(ctx));

                Constant* v1 = ivs->getAggregateElement(1);
                ReleaseAssert(v1->isZeroValue());

                Constant* v0 = ivs->getAggregateElement(static_cast<unsigned int>(0));
                ConstantExpr* expr = dyn_cast<ConstantExpr>(v0);
                ReleaseAssert(expr != nullptr);
                ReleaseAssert(expr->getOpcode() == Instruction::PtrToInt);
                ReleaseAssert(expr->getNumOperands() == 1);
                Constant* x = expr->getOperand(0);
                result = dyn_cast<Function>(x);
                ReleaseAssert(result != nullptr);
            }
        }
        ReleaseAssert(foundCallInst);
        ReleaseAssert(result != nullptr);
        return result;
    }

    static llvm::AllocaInst* WARN_UNUSED GetCaptureFromLLVM(llvm::Function* func)
    {
        using namespace llvm;
        uint32_t paramOrd = 0;
        while (true)
        {
            // 'func' should always have exactly one caller
            //
            ReleaseAssert(func->user_begin() != func->user_end());
            ReleaseAssert(++func->user_begin() == func->user_end());

            User* u = *func->user_begin();
            Instruction* inst = dyn_cast<Instruction>(u);
            ReleaseAssert(inst != nullptr);
            CallInst* callInst = dyn_cast<CallInst>(inst);
            ReleaseAssert(callInst != nullptr);
            ReleaseAssert(callInst->getCalledFunction() == func);
            ReleaseAssert(func->arg_size() > paramOrd);
            Value* passedArg = callInst->getArgOperand(paramOrd);

            AllocaInst* allocaInst = dyn_cast<AllocaInst>(passedArg);
            if (allocaInst)
            {
                return allocaInst;
            }

            LoadInst* loadInst = dyn_cast<LoadInst>(passedArg);
            ReleaseAssert(loadInst != nullptr);
            Value* valueSeenByLoad = LLVMBacktrackForSourceOfForwardedValue(loadInst);
            Argument* funcArg = dyn_cast<Argument>(valueSeenByLoad);
            ReleaseAssert(funcArg != nullptr);

            ReleaseAssert(funcArg->getParent() == callInst->getParent()->getParent());
            paramOrd = funcArg->getArgNo();
            func = funcArg->getParent();
        }
    }

    static llvm::CallInst* WARN_UNUSED GetAPIUserOfCaptureAlloca(llvm::AllocaInst* local)
    {
        using namespace llvm;
        bool foundCallInst = false;
        CallInst* callInst = nullptr;
        for (auto it = local->user_begin(); it != local->user_end(); it++)
        {
            User* u = *it;
            if (isa<CallInst>(u))
            {
                // ignore 'llvm.lifetime.start' and 'llvm.lifetime.end' intrinsic calls
                //
                {
                    CallInst* tmp = cast<CallInst>(u);
                    Function* calledFn = tmp->getCalledFunction();
                    if (calledFn != nullptr)
                    {
                        std::string fnName = calledFn->getName().str();
                        if (fnName.starts_with("llvm.lifetime.start") || fnName.starts_with("llvm.lifetime.end"))
                        {
                            continue;
                        }
                    }
                }

                ReleaseAssert(!foundCallInst);
                foundCallInst = true;
                callInst = cast<CallInst>(u);
                ReleaseAssert(callInst != nullptr);
            }
        }
        ReleaseAssert(foundCallInst);
        ReleaseAssert(callInst != nullptr);
        return callInst;
    }
};

}   // namespace dast

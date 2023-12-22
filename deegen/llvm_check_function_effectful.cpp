#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "llvm/IR/InlineAsm.h"
#include "deegen_osr_exit_placeholder.h"

namespace dast {

struct CheckLLVMFunctionEffectfulImpl
{
    CheckLLVMFunctionEffectfulImpl(std::function<bool(llvm::Function*, const std::vector<bool>&)> externalFnChecker)
        : m_externalFnChecker(externalFnChecker)
    { }

    bool WARN_UNUSED HandleExternalFunction(llvm::Function* func, std::vector<bool> excludeArgs)
    {
        using namespace llvm;
        ReleaseAssert(excludeArgs.size() == func->arg_size());

        if (func->hasFnAttribute(Attribute::ReadNone) || func->hasFnAttribute(Attribute::ReadOnly))
        {
            return false;
        }

        // Handle all the Deegen intrinsics that are known to be not effectful
        //
        std::string fnName = func->getName().str();

        if (fnName == "DeegenImpl_GetFEnvGlobalObject" ||
            fnName == "DeegenImpl_GetOutputBytecodeSlotOrdinal" ||
            fnName == "DeegenImpl_GetVarArgsStart" ||
            fnName == "DeegenImpl_GetNumVarArgs" ||
            fnName == x_osrExitPlaceholderName ||
            fnName == "__assert_fail" ||
            fnName == "abort" ||
            fnName.starts_with("llvm.lifetime.start") ||
            fnName.starts_with("llvm.lifetime.end"))
        {
            return false;
        }

        if (IsCXXSymbol(fnName))
        {
            std::string demangledName = DemangleCXXSymbol(fnName);
            if (demangledName.starts_with("DeegenImpl_GetReturnValueAtOrd(") ||
                demangledName.starts_with("DeegenImpl_GetNumReturnValues(") ||
                demangledName.starts_with("DeegenImpl_UpvalueAccessor_GetImmutable(") ||
                demangledName.starts_with("DeegenImpl_UpvalueAccessor_GetMutable(") ||
                demangledName.starts_with("FireReleaseAssert("))
            {
                return false;
            }
        }

        if (!m_externalFnChecker)
        {
            // Conservatively return true
            //
            return true;
        }
        else
        {
            return m_externalFnChecker(func, excludeArgs);
        }
    }

    struct PtrBaseInfo
    {
        // True if this pointer might be not based on an alloca nor any of the arguments
        //
        bool m_isPotentiallyNotAllocaNorArguments;
        // Index k is true if this pointer is potentially based on argument k
        // Only useful if m_isPotentiallyNotAllocaNorArguments is false
        //
        std::vector<bool> m_isPotentiallyBasedOnArg;
    };

    PtrBaseInfo MergePtrBaseInfo(const PtrBaseInfo& lhs, const PtrBaseInfo& rhs)
    {
        if (lhs.m_isPotentiallyNotAllocaNorArguments)
        {
            return lhs;
        }
        if (rhs.m_isPotentiallyNotAllocaNorArguments)
        {
            return rhs;
        }
        if (lhs.m_isPotentiallyBasedOnArg.size() == 0)
        {
            return rhs;
        }
        if (rhs.m_isPotentiallyBasedOnArg.size() == 0)
        {
            return lhs;
        }
        ReleaseAssert(lhs.m_isPotentiallyBasedOnArg.size() == rhs.m_isPotentiallyBasedOnArg.size());
        std::vector<bool> tmp = lhs.m_isPotentiallyBasedOnArg;
        for (size_t i = 0; i < tmp.size(); i++)
        {
            tmp[i] = tmp[i] || rhs.m_isPotentiallyBasedOnArg[i];
        }
        return { .m_isPotentiallyNotAllocaNorArguments = false, .m_isPotentiallyBasedOnArg = std::move(tmp) };
    }

    PtrBaseInfo TryGetPtrInfoImpl(llvm::Instruction* inst)
    {
        using namespace llvm;
        if (isa<GetElementPtrInst>(inst))
        {
            GetElementPtrInst* gep = cast<GetElementPtrInst>(inst);
            Value* ptrOperand = gep->getPointerOperand();
            return TryGetPtrInfo(ptrOperand);
        }

        if (isa<AllocaInst>(inst))
        {
            return PtrBaseInfo { .m_isPotentiallyNotAllocaNorArguments = false, .m_isPotentiallyBasedOnArg = {} };
        }

        if (isa<SelectInst>(inst))
        {
            SelectInst* si = cast<SelectInst>(inst);
            PtrBaseInfo tv = TryGetPtrInfo(si->getTrueValue());
            if (tv.m_isPotentiallyNotAllocaNorArguments)
            {
                return tv;
            }
            PtrBaseInfo fv = TryGetPtrInfo(si->getFalseValue());
            return MergePtrBaseInfo(tv, fv);
        }

        if (isa<PHINode>(inst))
        {
            PHINode* phi = cast<PHINode>(inst);
            uint32_t num = phi->getNumIncomingValues();
            ReleaseAssert(num > 0);
            PtrBaseInfo result;
            for (uint32_t i = 0; i < num; i++)
            {
                Value* incomingVal = phi->getIncomingValue(i);
                PtrBaseInfo info = TryGetPtrInfo(incomingVal);
                if (info.m_isPotentiallyNotAllocaNorArguments)
                {
                    return info;
                }
                if (i == 0)
                {
                    result = std::move(info);
                }
                else
                {
                    result = MergePtrBaseInfo(result, info);
                }
            }
            return result;
        }

        // Conservatively report that the pointer may be based on anything
        //
        return PtrBaseInfo { .m_isPotentiallyNotAllocaNorArguments = true, .m_isPotentiallyBasedOnArg = {} };
    }

    PtrBaseInfo WARN_UNUSED TryGetPtrInfo(llvm::Value* value)
    {
        using namespace llvm;
        ReleaseAssert(llvm_value_has_type<void*>(value));

        if (isa<Argument>(value))
        {
            Argument* arg = cast<Argument>(value);
            size_t argOrd = arg->getArgNo();
            Function* func = arg->getParent();
            ReleaseAssert(func != nullptr);
            size_t numArgs = func->arg_size();
            ReleaseAssert(argOrd < numArgs);
            std::vector<bool> tmp;
            tmp.resize(numArgs, false);
            tmp[argOrd] = true;
            return PtrBaseInfo { .m_isPotentiallyNotAllocaNorArguments = false, .m_isPotentiallyBasedOnArg = std::move(tmp) };
        }

        Instruction* inst = dyn_cast<Instruction>(value);
        if (inst == nullptr)
        {
            // If this value is not an instruction, there is no way this pointer is based on an alloca or any argument:
            // the only way to get a pointer this way is through a ConstantExpr, but neither alloca nor argument is constant
            //
            return PtrBaseInfo { .m_isPotentiallyNotAllocaNorArguments = true, .m_isPotentiallyBasedOnArg = {} };
        }

        if (!m_ptrInfoCache.count(inst))
        {
            m_ptrInfoCache[inst] = TryGetPtrInfoImpl(inst);
        }
        return m_ptrInfoCache[inst];
    }

    // Return true if the pointer is definitely based on an alloca or one of the arguments in allowedArgs
    //
    bool WARN_UNUSED TryTracePointer(llvm::Value* ptr, std::vector<bool> allowedArgs)
    {
        using namespace llvm;
        ReleaseAssert(llvm_value_has_type<void*>(ptr));

        PtrBaseInfo ptrInfo = TryGetPtrInfo(ptr);
        if (ptrInfo.m_isPotentiallyNotAllocaNorArguments)
        {
            return false;
        }

        if (ptrInfo.m_isPotentiallyBasedOnArg.size() == 0)
        {
            // A vector of length 0 is a shorthand to say that the pointer must not be based on any argument
            //
            return true;
        }

        ReleaseAssert(allowedArgs.size() == ptrInfo.m_isPotentiallyBasedOnArg.size());

        for (size_t i = 0; i < allowedArgs.size(); i++)
        {
            if (!allowedArgs[i] && ptrInfo.m_isPotentiallyBasedOnArg[i])
            {
                return false;
            }
        }
        return true;
    }

    std::unordered_map<llvm::Instruction*, PtrBaseInfo> m_ptrInfoCache;

    bool WARN_UNUSED CheckImpl(llvm::Function* func, std::vector<bool> excludeArgs)
    {
        using namespace llvm;
        ReleaseAssert(excludeArgs.size() == func->arg_size());
        if (func->empty())
        {
            return HandleExternalFunction(func, excludeArgs);
        }

        for (BasicBlock& bb : *func)
        {
            for (Instruction& inst : bb)
            {
                uint32_t opcode = inst.getOpcode();
                if (opcode == Instruction::Invoke ||
                    opcode == Instruction::Resume ||
                    opcode == Instruction::CleanupRet ||
                    opcode == Instruction::CatchRet ||
                    opcode == Instruction::CatchSwitch ||
                    opcode == Instruction::CleanupPad ||
                    opcode == Instruction::CatchPad ||
                    opcode == Instruction::LandingPad)
                {
                    // Conservatively lock down all exception-related instructions for simplicity: our code is always compiled with -fno-exception
                    //
                    return true;
                }

                if (opcode == Instruction::Store)
                {
                    StoreInst* si = dyn_cast<StoreInst>(&inst);
                    ReleaseAssert(si != nullptr);
                    if (!TryTracePointer(si->getPointerOperand(), excludeArgs))
                    {
                        return true;
                    }
                }

                if (opcode == Instruction::AtomicCmpXchg)
                {
                    AtomicCmpXchgInst* cxi = dyn_cast<AtomicCmpXchgInst>(&inst);
                    ReleaseAssert(cxi != nullptr);
                    if (!TryTracePointer(cxi->getPointerOperand(), excludeArgs))
                    {
                        return true;
                    }
                }

                if (opcode == Instruction::AtomicRMW)
                {
                    AtomicRMWInst* rmw = dyn_cast<AtomicRMWInst>(&inst);
                    ReleaseAssert(rmw != nullptr);
                    if (!TryTracePointer(rmw->getPointerOperand(), excludeArgs))
                    {
                        return true;
                    }
                }

                if (opcode == Instruction::Call || opcode == Instruction::CallBr)
                {
                    auto handleCall = [&](CallBase* cb) ALWAYS_INLINE WARN_UNUSED -> bool
                    {
                        if (cb->hasFnAttr(Attribute::ReadNone) || cb->hasFnAttr(Attribute::ReadOnly))
                        {
                            return false;
                        }

                        Function* callee = cb->getCalledFunction();
                        if (callee != nullptr)
                        {
                            if (callee->hasFnAttribute(Attribute::ReadNone) || callee->hasFnAttribute(Attribute::ReadOnly))
                            {
                                return false;
                            }

                            size_t numArgs = callee->arg_size();
                            std::vector<bool> calleeExcludeArgs;
                            for (size_t i = 0; i < numArgs; i++)
                            {
                                Value* argOperand = cb->getArgOperand(static_cast<uint32_t>(i));
                                if (!llvm_value_has_type<void*>(argOperand))
                                {
                                    calleeExcludeArgs.push_back(false);
                                }
                                else
                                {
                                    calleeExcludeArgs.push_back(TryTracePointer(argOperand, excludeArgs));
                                }
                            }
                            ReleaseAssert(calleeExcludeArgs.size() == numArgs);

                            return Check(callee, calleeExcludeArgs);
                        }
                        else if (cb->isInlineAsm())
                        {
                            InlineAsm* ia = dyn_cast<InlineAsm>(cb->getCalledOperand());
                            ReleaseAssert(ia != nullptr);
                            if (ia->getAsmString() != "")
                            {
                                std::string constraintString = ia->getConstraintString();
                                if (constraintString.find("~{memory}") != std::string::npos ||
                                    constraintString.find("*") != std::string::npos)
                                {
                                    // If the inline ASM clobbers memory or has an indirect operand (which means it writes memory),
                                    // conservatively return true.
                                    // For simplicity, for now we do not try to figure out which indirect operand it exactly is.
                                    //
                                    return true;
                                }
                            }
                            return false;
                        }
                        else
                        {
                            // This is an indirect call, conservatively return true
                            //
                            return true;
                        }
                    };

                    CallBase* cb = dyn_cast<CallBase>(&inst);
                    ReleaseAssert(cb != nullptr);
                    if (handleCall(cb))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool WARN_UNUSED Check(llvm::Function* func, std::vector<bool> excludeArgs)
    {
        auto key = std::make_pair(func, excludeArgs);
        if (!m_cache.count(key))
        {
            m_cache[key] = CheckImpl(func, excludeArgs);
        }
        return m_cache[key];
    }

    std::map<std::pair<llvm::Function*, std::vector<bool>>, bool> m_cache;
    std::function<bool(llvm::Function*, const std::vector<bool>&)> m_externalFnChecker;
};

bool WARN_UNUSED DetermineIfLLVMFunctionMightBeEffectful(
    llvm::Function* func,
    std::vector<bool> excludePointerArgs,
    std::function<bool(llvm::Function*, const std::vector<bool>&)> externalFuncMightBeEffectfulChecker)
{
    using namespace llvm;
    ReleaseAssert(func != nullptr);
    if (excludePointerArgs.size() == 0)
    {
        excludePointerArgs.resize(func->arg_size(), false);
    }
    ReleaseAssert(excludePointerArgs.size() == func->arg_size());

    for (size_t i = 0; i < excludePointerArgs.size(); i++)
    {
        if (excludePointerArgs[i])
        {
            ReleaseAssert(llvm_value_has_type<void*>(func->getArg(static_cast<uint32_t>(i))));
        }
    }

    CheckLLVMFunctionEffectfulImpl checker(externalFuncMightBeEffectfulChecker);
    return checker.Check(func, excludePointerArgs);
}

}   // namespace dast

#include "deegen_ast_return_value_accessor.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "bytecode.h"

namespace dast {

AstReturnValueAccessor::AstReturnValueAccessor(Kind kind, llvm::CallInst* origin)
    : m_kind(kind), m_origin(origin)
{
    switch (m_kind)
    {
    case Kind::GetOneReturnValue:
    {
        ReleaseAssert(m_origin->arg_size() == 1);
        ReleaseAssert(llvm_value_has_type<size_t>(m_origin->getArgOperand(0)));
        ReleaseAssert(llvm_value_has_type<uint64_t>(m_origin));
        break;
    }
    case Kind::GetNumReturns:
    {
        ReleaseAssert(m_origin->arg_size() == 0);
        ReleaseAssert(llvm_value_has_type<size_t>(m_origin));
        break;
    }
    case Kind::StoreFirstKFillNil:
    {
        ReleaseAssert(m_origin->arg_size() == 2);
        ReleaseAssert(llvm_value_has_type<void*>(m_origin->getArgOperand(0)));
        ReleaseAssert(llvm_value_has_type<size_t>(m_origin->getArgOperand(1)));
        ReleaseAssert(llvm_value_has_type<void>(m_origin));
        break;
    }
    case Kind::StoreAsVariadicResults:
    {
        ReleaseAssert(m_origin->arg_size() == 0);
        ReleaseAssert(llvm_value_has_type<void>(m_origin));
        break;
    }
    }
}

std::vector<AstReturnValueAccessor> WARN_UNUSED AstReturnValueAccessor::GetAllUseInFunction(llvm::Function* func)
{
    using namespace llvm;
    std::vector<AstReturnValueAccessor> result;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                Function* callee = callInst->getCalledFunction();
                if (callee != nullptr && IsCXXSymbol(callee->getName().str()))
                {
                    std::string demangledName = DemangleCXXSymbol(callee->getName().str());
                    if (demangledName.starts_with("DeegenImpl_GetReturnValueAtOrd("))
                    {
                        result.push_back({ Kind::GetOneReturnValue, callInst });
                    }
                    else if (demangledName.starts_with("DeegenImpl_GetNumReturnValues("))
                    {
                        result.push_back({ Kind::GetNumReturns, callInst });
                    }
                    else if (demangledName.starts_with("DeegenImpl_StoreReturnValuesTo("))
                    {
                        result.push_back({ Kind::StoreFirstKFillNil, callInst });
                    }
                    else if (demangledName.starts_with("DeegenImpl_StoreReturnValuesAsVariadicResults("))
                    {
                        result.push_back({ Kind::StoreAsVariadicResults, callInst });
                    }
                }
            }
        }
    }
    return result;
}

void AstReturnValueAccessor::DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi)
{
    using namespace llvm;
    ReleaseAssert(ifi->IsReturnContinuation());
    LLVMContext& ctx = ifi->GetModule()->getContext();
    if (m_kind == Kind::GetOneReturnValue)
    {
        ReleaseAssert(m_origin->arg_size() == 1);
        Value* ord = m_origin->getArgOperand(0);
        ReleaseAssert(llvm_value_has_type<size_t>(ord));
        bool canDoQuickLoad = false;
        if (isa<Constant>(ord))
        {
            uint64_t ordVal = GetValueOfLLVMConstantInt<uint64_t>(cast<Constant>(ord));
            if (ordVal < x_minNilFillReturnValues)
            {
                // The callee is responsible to fill nil at this position even if there are less return values. So we can load directly.
                //
                canDoQuickLoad = true;
            }
        }
        Instruction* result;
        if (canDoQuickLoad)
        {
            GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetRetStart(), { ord }, "", m_origin);
            result = new LoadInst(llvm_type_of<uint64_t>(ctx), gep, "", m_origin);
        }
        else
        {
            result = ifi->CallDeegenCommonSnippet(
                "GetReturnValueAtSpecifiedOrdinal",
                {
                    ifi->GetRetStart(),
                    ifi->GetNumRet(),
                    ord
                },
                m_origin);
        }
        ReleaseAssert(llvm_value_has_type<uint64_t>(result));
        ReleaseAssert(llvm_value_has_type<uint64_t>(m_origin));
        // Important to use ReplaceInstructionWithValue here (not ReplaceInstWithInst!), since 'result' has already been inserted into the BB
        //
        ReplaceInstructionWithValue(m_origin, result);
        m_origin = nullptr;
    }
    else if (m_kind == Kind::GetNumReturns)
    {
        ReleaseAssert(m_origin->arg_size() == 0);
        ReleaseAssert(llvm_value_has_type<size_t>(m_origin));
        ReplaceInstructionWithValue(m_origin, ifi->GetNumRet());
        m_origin = nullptr;
    }
    else if (m_kind == Kind::StoreFirstKFillNil)
    {
        ReleaseAssert(m_origin->arg_size() == 2);
        Value* dst = m_origin->getArgOperand(0);
        ReleaseAssert(llvm_value_has_type<void*>(dst));
        Value* numToStore = m_origin->getArgOperand(1);
        ReleaseAssert(llvm_value_has_type<size_t>(numToStore));
        bool canDoQuickCopy = false;
        uint64_t knownNumForQuickCopy = static_cast<uint64_t>(-1);
        if (isa<Constant>(numToStore))
        {
            knownNumForQuickCopy = GetValueOfLLVMConstantInt<uint64_t>(cast<Constant>(numToStore));
            if (knownNumForQuickCopy <= x_minNilFillReturnValues)
            {
                canDoQuickCopy = true;
            }
        }

        if (canDoQuickCopy)
        {
            std::vector<Value*> loadedValues;
            for (size_t i = 0; i < knownNumForQuickCopy; i++)
            {
                GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetRetStart(), { CreateLLVMConstantInt<uint64_t>(ctx, i) }, "", m_origin);
                LoadInst* loadInst = new LoadInst(llvm_type_of<uint64_t>(ctx), gep, "", m_origin);
                loadedValues.push_back(loadInst);
            }
            for (size_t i = 0; i < knownNumForQuickCopy; i++)
            {
                GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), dst, { CreateLLVMConstantInt<uint64_t>(ctx, i) }, "", m_origin);
                std::ignore = new StoreInst(loadedValues[i], gep, m_origin);
            }
        }
        else
        {
            ifi->CallDeegenCommonSnippet(
                "StoreFirstKReturnValuesPaddingNil",
                {
                    ifi->GetRetStart(),
                    ifi->GetNumRet(),
                    dst,
                    numToStore
                },
                m_origin);
        }

        ReleaseAssert(llvm_value_has_type<void>(m_origin));
        ReleaseAssert(m_origin->use_begin() == m_origin->use_end());
        m_origin->eraseFromParent();
        m_origin = nullptr;
    }
    else if (m_kind == Kind::StoreAsVariadicResults)
    {
        ReleaseAssert(m_origin->arg_size() == 0);
        ifi->CallDeegenCommonSnippet(
            "StoreReturnValuesAsVariadicResults",
            {
                ifi->GetCoroutineCtx(),
                ifi->GetStackBase(),
                ifi->GetRetStart(),
                ifi->GetNumRet()
            },
            m_origin);

        ReleaseAssert(llvm_value_has_type<void>(m_origin));
        ReleaseAssert(m_origin->use_begin() == m_origin->use_end());
        m_origin->eraseFromParent();
        m_origin = nullptr;
    }
    else
    {
        ReleaseAssert(false);
    }
}

}   // namespace dast

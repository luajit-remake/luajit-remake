#include "deegen_interpreter_interface.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_make_call.h"

#include "llvm/Linker/Linker.h"

// TODO: extract out needed logic
#include "bytecode.h"

namespace dast {

InterpreterFunctionInterface::InterpreterFunctionInterface(BytecodeVariantDefinition* bytecodeDef, llvm::Function* impl, bool isReturnContinuation)
    : m_bytecodeDef(bytecodeDef)
    , m_module(nullptr)
    , m_isReturnContinuation(isReturnContinuation)
    , m_impl(nullptr)
    , m_wrapper(nullptr)
    , m_didEmitWrapper(false)
    , m_didLowerAPIs(false)
    , m_didGetResult(false)
    , m_coroutineCtx(nullptr)
    , m_stackBase(nullptr)
    , m_curBytecode(nullptr)
    , m_codeBlock(nullptr)
    , m_retStart(nullptr)
    , m_numRet(nullptr)
{
    using namespace llvm;
    m_module = llvm::CloneModule(*impl->getParent());
    m_impl = m_module->getFunction(impl->getName());
    ReleaseAssert(m_impl != nullptr);

    LLVMContext& ctx = m_module->getContext();

    // This is not the final interface, we will change this interface
    // to the final version after we inline 'impl' but before we lower the APIs.
    //
    // We are doing this because certain values (e.g., 'bytecode' in the return
    // continuation) needs to be computed from the arguments.
    // After inlining in 'impl', we want to run simplifcation phase so everything is cleaned up,
    // but that also makes it impossible to find where those values are in the simplified function.
    //
    // So we put everything we want to track in the arguments, then run the simplifcation phase,
    // and finally remove the fake arguments and replace them with the concrete computation.
    //
    std::vector<Type*> paramTypes {
        llvm_type_of<void*>(ctx) /*coroutineCtx*/,
        llvm_type_of<void*>(ctx) /*stackBase*/,
        llvm_type_of<void*>(ctx) /*bytecode*/,
        llvm_type_of<void*>(ctx) /*codeblock*/
    };
    if (isReturnContinuation)
    {
        paramTypes.push_back(llvm_type_of<void*>(ctx) /*retStart*/);
        paramTypes.push_back(llvm_type_of<uint64_t>(ctx) /*numRet*/);
    }

    // For isReturnContinuation, our caller should have set up the desired function name for us
    // For !isReturnContinuation, we should rename by ourselves.
    //
    if (!isReturnContinuation)
    {
        std::string desiredFnName = std::string("__deegen_interpreter_op_") + m_bytecodeDef->m_bytecodeName + "_" + std::to_string(m_bytecodeDef->m_variantOrd) + "_impl";
        ReleaseAssert(m_module->getGlobalVariable(desiredFnName) == nullptr);
        m_impl->setName(desiredFnName);
    }
    else
    {
        ReleaseAssert(m_impl->getName().str().ends_with("_impl"));
    }

    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx) /*result*/, paramTypes, false /*isVarArg*/);
    std::string wrapperFnName = m_impl->getName().str() + "_wrapper";
    ReleaseAssert(m_module->getNamedValue(wrapperFnName) == nullptr);
    m_wrapper = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, wrapperFnName, m_module.get());

    InvalidateContextValues();
    m_coroutineCtx = m_wrapper->getArg(0);
    m_stackBase = m_wrapper->getArg(1);
    m_curBytecode = m_wrapper->getArg(2);
    m_codeBlock = m_wrapper->getArg(3);
    if (isReturnContinuation)
    {
        m_retStart = m_wrapper->getArg(4);
        m_numRet = m_wrapper->getArg(5);
    }

    m_wrapper->addFnAttr(Attribute::AttrKind::NoReturn);
    m_wrapper->addFnAttr(Attribute::AttrKind::NoUnwind);
    CopyFunctionAttributes(m_wrapper /*dst*/, m_impl /*src*/);

    // TODO: add alias attributes
}

void InterpreterFunctionInterface::EmitWrapperBody()
{
    using namespace llvm;
    ReleaseAssert(m_wrapper->empty());
    ReleaseAssert(!m_didEmitWrapper);
    m_didEmitWrapper = true;

    LLVMContext& ctx = GetModule()->getContext();
    BasicBlock* entryBlock = BasicBlock::Create(ctx, "", m_wrapper);

    BasicBlock* currentBlock = entryBlock;

    std::vector<Value*> opcodeValues;
    for (auto& operand : m_bytecodeDef->m_list)
    {
        opcodeValues.push_back(operand->GetOperandValueFromBytecodeStruct(this, currentBlock));
    }

    std::vector<Value*> usageValues;
    {
        size_t ord = 0;
        for (auto& operand : m_bytecodeDef->m_list)
        {
            usageValues.push_back(operand->EmitUsageValueFromBytecodeValue(this, currentBlock, opcodeValues[ord]));
            // Set name to make dump a bit more readable
            //
            usageValues.back()->setName(std::string("bc_operand_") + operand->OperandName());
            ord++;
        }
        ReleaseAssert(ord == opcodeValues.size() && ord == usageValues.size());
    }

    if (m_isReturnContinuation)
    {
        usageValues.push_back(GetRetStart());
        usageValues.push_back(GetNumRet());
    }

    FunctionType* fty = m_impl->getFunctionType();
    ReleaseAssert(llvm_type_has_type<void>(fty->getReturnType()));
    ReleaseAssert(fty->getNumParams() == usageValues.size());
    for (size_t i = 0; i < usageValues.size(); i++)
    {
        ReleaseAssert(fty->getParamType(static_cast<uint32_t>(i)) == usageValues[i]->getType());
    }

    CallInst::Create(m_impl, usageValues, "", currentBlock);
    new UnreachableInst(ctx, currentBlock);

    ValidateLLVMFunction(m_wrapper);
}

struct ReturnContinuationFinder
{
    ReturnContinuationFinder(llvm::Function* from)
    {
        m_count = 0;
        std::vector<AstMakeCall> list = AstMakeCall::GetAllUseInFunction(from);
        for (AstMakeCall& amc : list)
        {
            dfs(amc.m_continuation);
        }
        // It is impossible for the entry function itself to also be a continuation,
        // since they have different function prototypes.
        // Our frontend should have rejected this, but doesn't hurt to assert.
        //
        ReleaseAssert(!m_labelMap.count(from));
    }

    std::unordered_map<llvm::Function*, size_t> m_labelMap;
    size_t m_count;

private:
    void dfs(llvm::Function* cur)
    {
        if (m_labelMap.count(cur))
        {
            return;
        }
        m_labelMap[cur] = m_count;
        m_count++;

        std::vector<AstMakeCall> list = AstMakeCall::GetAllUseInFunction(cur);
        for (AstMakeCall& amc : list)
        {
            dfs(amc.m_continuation);
        }
    }
};

std::unique_ptr<llvm::Module> WARN_UNUSED InterpreterFunctionInterface::ProcessReturnContinuation(llvm::Function* rc)
{
    ReleaseAssert(!m_isReturnContinuation);
    InterpreterFunctionInterface ifi(m_bytecodeDef, rc, true /*isReturnContinuation*/);
    ifi.EmitWrapperBody();
    ifi.LowerAPIs();
    return std::move(ifi.m_module);
}

llvm::FunctionType* WARN_UNUSED InterpreterFunctionInterface::GetInterfaceFunctionType(llvm::LLVMContext& ctx, bool forReturnContinuation)
{
    using namespace llvm;
    return FunctionType::get(
        llvm_type_of<void>(ctx) /*result*/,
        {
            llvm_type_of<void*>(ctx) /*coroutineCtx*/,
            llvm_type_of<void*>(ctx) /*stackBase*/,
            llvm_type_of<void*>(ctx) /*bytecodeOrRetStart*/,
            forReturnContinuation ? llvm_type_of<uint64_t>(ctx) /*numRets*/ : llvm_type_of<void*>(ctx) /*codeblock*/
        } /*params*/,
        false /*isVarArg*/);
}

void InterpreterFunctionInterface::LowerAPIs()
{
    using namespace llvm;
    ReleaseAssert(m_didEmitWrapper && !m_didLowerAPIs);
    m_didLowerAPIs = true;

    LLVMContext& ctx = m_module->getContext();

    std::string finalFnName = m_impl->getName().str();
    ReleaseAssert(finalFnName.ends_with("_impl"));
    finalFnName = finalFnName.substr(0, finalFnName.length() - strlen("_impl"));
    auto getRetContFinalNameForOrdinal = [this, finalFnName](size_t ord) -> std::string
    {
        ReleaseAssert(!m_isReturnContinuation);
        return finalFnName + "_retcont_" + std::to_string(ord);
    };

    // First step: if we are the main function (i.e., not return continuation), we shall parse out all the needed return
    // continuations, and process each of them
    //
    std::vector<std::unique_ptr<llvm::Module>> allRetConts;
    if (!m_isReturnContinuation)
    {
        // Find all the return continuations, give each of them a unique name, and create the declarations.
        // This is necessary for us to later link them together.
        //
        ReturnContinuationFinder rcFinder(m_impl);
        FunctionType* rcFty = GetInterfaceFunctionType(ctx, true /*forReturnContinuation*/);
        std::vector<Function*> rcList;
        rcList.resize(rcFinder.m_count, nullptr);
        ReleaseAssert(rcFinder.m_labelMap.size() == rcFinder.m_count);
        for (auto& it : rcFinder.m_labelMap)
        {
            ReleaseAssert(it.second < rcList.size());
            Function* rc = it.first;
            std::string rcFinalName = getRetContFinalNameForOrdinal(it.second);
            std::string rcImplName = rcFinalName + "_impl";
            ReleaseAssert(m_module->getNamedValue(rcImplName) == nullptr);
            rc->setName(rcImplName);
            ReleaseAssert(rcList[it.second] == nullptr);
            rcList[it.second] = rc;

            ReleaseAssert(m_module->getNamedValue(rcFinalName) == nullptr);
            Function::Create(rcFty, GlobalValue::LinkageTypes::ExternalLinkage, rcFinalName, m_module.get());
        }

        // After all the renaming and function declarations, process each of the return continuation
        //
        for (Function* targetRc : rcList)
        {
            allRetConts.push_back(ProcessReturnContinuation(targetRc));
        }
    }

    // Now we can process our own function
    // Inline 'm_impl' into 'm_wrapper'
    //
    if (m_impl->hasFnAttribute(Attribute::AttrKind::NoInline))
    {
        m_impl->removeFnAttr(Attribute::AttrKind::NoInline);
    }
    m_impl->addFnAttr(Attribute::AttrKind::AlwaysInline);
    m_impl->setLinkage(GlobalVariable::LinkageTypes::InternalLinkage);

    InvalidateContextValues();
    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::PerFunctionSimplifyOnly);
    m_impl = nullptr;

    // Fix up the function prototype of 'm_wrapper' before lowering the APIs
    //
    {
        FunctionType* fty = m_wrapper->getFunctionType();
        if (m_isReturnContinuation)
        {
            ReleaseAssert(fty->getNumParams() == 6);
        }
        else
        {
            ReleaseAssert(fty->getNumParams() == 4);
        }

        FunctionType* newFty = GetInterfaceFunctionType(ctx, m_isReturnContinuation);
        ReleaseAssert(m_module->getNamedValue(finalFnName) == nullptr);
        Function* newFunc = Function::Create(newFty, GlobalValue::LinkageTypes::ExternalLinkage, finalFnName, m_module.get());

        ValueToValueMapTy vmap;
        std::vector<Instruction*> preheaderSeq;

        m_coroutineCtx = newFunc->getArg(0);
        m_stackBase = newFunc->getArg(1);

        vmap[m_wrapper->getArg(0)] = m_coroutineCtx;
        vmap[m_wrapper->getArg(1)] = m_stackBase;

        if (!m_isReturnContinuation)
        {
            m_curBytecode = newFunc->getArg(2);
            m_codeBlock = newFunc->getArg(3);

            vmap[m_wrapper->getArg(2)] = m_curBytecode;
            vmap[m_wrapper->getArg(3)] = m_codeBlock;
        }
        else
        {
            Function* getCbFunc = LinkInDeegenCommonSnippet(m_module.get(), "GetCodeBlockFromStackFrameBase");
            ReleaseAssert(getCbFunc->arg_size() == 1 && llvm_type_has_type<void*>(getCbFunc->getFunctionType()->getParamType(0)));
            ReleaseAssert(llvm_type_has_type<void*>(getCbFunc->getFunctionType()->getReturnType()));
            Instruction* codeblock = CallInst::Create(getCbFunc, { m_stackBase }, "" /*name*/);

            Function* getBytecodePtrFunc = LinkInDeegenCommonSnippet(m_module.get(), "GetBytecodePtrAfterReturnFromCall");
            ReleaseAssert(getBytecodePtrFunc->arg_size() == 2 &&
                          llvm_type_has_type<void*>(getBytecodePtrFunc->getFunctionType()->getParamType(0)) &&
                          llvm_type_has_type<void*>(getBytecodePtrFunc->getFunctionType()->getParamType(1)));
            ReleaseAssert(llvm_type_has_type<void*>(getBytecodePtrFunc->getFunctionType()->getReturnType()));
            Instruction* bytecodePtr = CallInst::Create(getBytecodePtrFunc, { m_stackBase, codeblock }, "" /*name*/);

            m_curBytecode = bytecodePtr;
            m_codeBlock = codeblock;

            vmap[m_wrapper->getArg(2)] = m_curBytecode;
            vmap[m_wrapper->getArg(3)] = m_codeBlock;

            preheaderSeq = {
                codeblock,
                bytecodePtr
            };

            m_retStart = newFunc->getArg(2);
            m_numRet = newFunc->getArg(3);

            vmap[m_wrapper->getArg(4)] = m_retStart;
            vmap[m_wrapper->getArg(5)] = m_numRet;
        }

        for (auto it = vmap.begin(); it != vmap.end(); it++)
        {
            ReleaseAssert(it->first->getType() == it->second->getType());
        }

        for (uint32_t i = 0; i < m_wrapper->arg_size(); i++)
        {
            ReleaseAssert(vmap.count(m_wrapper->getArg(i)));
        }

        SmallVector<ReturnInst*, 8> ignoredReturns;
        CloneFunctionInto(newFunc /*dst*/, m_wrapper /*src*/, vmap, CloneFunctionChangeType::LocalChangesOnly, ignoredReturns);

        ReleaseAssert(!newFunc->empty());

        // Now, insert the preheader instructions to the first basic block, but after all the allocas
        //
        if (!preheaderSeq.empty())
        {
            BasicBlock* entryBB = &newFunc->front();
            ReleaseAssert(entryBB != nullptr);

            Instruction* firstNonAllocaInst = nullptr;
            for (Instruction& inst : *entryBB)
            {
                if (dyn_cast<AllocaInst>(&inst) == nullptr)
                {
                    firstNonAllocaInst = &inst;
                    break;
                }
            }
            ReleaseAssert(firstNonAllocaInst != nullptr);

            for (Instruction* inst : preheaderSeq)
            {
                inst->insertBefore(firstNonAllocaInst);
            }
        }

        // Set parameter names just to make dumps more readable
        //
        ReleaseAssert(newFunc->arg_size() == 4);
        newFunc->getArg(0)->setName("coroCtx");
        newFunc->getArg(1)->setName("stackBase");
        if (m_isReturnContinuation)
        {
            newFunc->getArg(2)->setName("retStart");
            newFunc->getArg(3)->setName("numRets");
        }
        else
        {
            newFunc->getArg(2)->setName("curBytecode");
            newFunc->getArg(3)->setName("codeBlock");
        }

        ValidateLLVMFunction(newFunc);

        m_wrapper->deleteBody();
        m_wrapper = newFunc;
    }

    // Now we can do the lowerings
    //

    // Run optimization pass
    //
    RunLLVMOptimizePass(m_module.get());

    std::vector<std::string> extractTargets;
    // Just sanity check that the function we just created is there
    //
    ReleaseAssert(m_module->getFunction(finalFnName) != nullptr);
    ReleaseAssert(!m_module->getFunction(finalFnName)->empty());
    extractTargets.push_back(finalFnName);

    // If we are the main function, we also need to link in all the return continuations
    //
    ReleaseAssertImp(m_isReturnContinuation, allRetConts.size() == 0);
    for (size_t rcOrdinal = 0; rcOrdinal < allRetConts.size(); rcOrdinal++)
    {
        std::unique_ptr<Module> rcModule = std::move(allRetConts[rcOrdinal]);
        std::string expectedRcName = getRetContFinalNameForOrdinal(rcOrdinal);
        ReleaseAssert(rcModule->getFunction(expectedRcName) != nullptr);
        ReleaseAssert(!rcModule->getFunction(expectedRcName)->empty());
        // Optimization pass may have stripped the return continuation function if it's not directly used by the main function
        // But if it exists, it should always be a declaration at this point
        //
        if (m_module->getFunction(expectedRcName) != nullptr)
        {
            ReleaseAssert(m_module->getFunction(expectedRcName)->empty());
        }

        Linker linker(*m_module.get());
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(rcModule)) == false);

        ReleaseAssert(m_module->getFunction(expectedRcName) != nullptr);
        ReleaseAssert(!m_module->getFunction(expectedRcName)->empty());
        extractTargets.push_back(expectedRcName);
    }

    // Finally, extract out the target functions
    //
    m_module = ExtractFunctions(m_module.get(), extractTargets);
    m_wrapper = nullptr;
    InvalidateContextValues();
}

}   // namespace dast

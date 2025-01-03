#include "deegen_process_lib_func.h"
#include "deegen_register_pinning_scheme.h"
#include "api_define_lib_function.h"
#include "deegen_ast_throw_error.h"
#include "runtime_utils.h"
#include "tag_register_optimization.h"

namespace dast {

namespace {

class UserLibReturnAPI
{
public:
    UserLibReturnAPI()
        : m_origin(nullptr)
        , m_isFixedNum(false)
        , m_isLongJump(false)
        , m_retRangeBegin(nullptr)
        , m_retRangeNum(nullptr)
        , m_longJumpFromHeader(nullptr)
    { }

    static void LowerAllForFunction(DeegenLibFuncInstance* ifi, llvm::Function* func)
    {
        std::vector<UserLibReturnAPI> allUses = FindAllUseInFunction(func);
        for (auto& it : allUses)
        {
            it.DoLowering(ifi);
        }
    }

    static std::vector<UserLibReturnAPI> WARN_UNUSED FindAllUseInFunction(llvm::Function* func);
    void DoLowering(DeegenLibFuncInstance* ifi);

    llvm::CallInst* m_origin;
    bool m_isFixedNum;
    bool m_isLongJump;
    // Holds the return values for fixed-num return
    //
    std::vector<llvm::Value*> m_returnValues;
    // Hold the return value range/num for range return
    //
    llvm::Value* m_retRangeBegin;
    llvm::Value* m_retRangeNum;
    llvm::Value* m_longJumpFromHeader;
};

std::vector<UserLibReturnAPI> WARN_UNUSED UserLibReturnAPI::FindAllUseInFunction(llvm::Function* func)
{
    using namespace llvm;
    std::vector<UserLibReturnAPI> result;
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
                    if (demangledName.starts_with("DeegenLibFuncCommonAPIs::Return("))
                    {
                        UserLibReturnAPI r;
                        r.m_origin = callInst;
                        r.m_isFixedNum = true;
                        for (uint32_t i = 1; i < callInst->arg_size(); i++)
                        {
                            Value* val = callInst->getArgOperand(i);
                            ReleaseAssert(llvm_value_has_type<uint64_t>(val));
                            r.m_returnValues.push_back(val);
                        }
                        result.push_back(r);
                    }
                    else if (demangledName.starts_with("DeegenLibFuncCommonAPIs::ReturnValueRange("))
                    {
                        UserLibReturnAPI r;
                        r.m_origin = callInst;
                        r.m_isFixedNum = false;
                        ReleaseAssert(callInst->arg_size() == 3);
                        r.m_retRangeBegin = callInst->getArgOperand(1);
                        ReleaseAssert(llvm_value_has_type<void*>(r.m_retRangeBegin));
                        r.m_retRangeNum = callInst->getArgOperand(2);
                        ReleaseAssert(llvm_value_has_type<uint64_t>(r.m_retRangeNum));
                        result.push_back(r);
                    }
                    else if (demangledName.starts_with("DeegenLibFuncCommonAPIs::LongJump("))
                    {
                        UserLibReturnAPI r;
                        r.m_origin = callInst;
                        r.m_isFixedNum = false;
                        r.m_isLongJump = true;
                        ReleaseAssert(callInst->arg_size() == 4);
                        r.m_longJumpFromHeader = callInst->getArgOperand(1);
                        ReleaseAssert(llvm_value_has_type<void*>(r.m_longJumpFromHeader));
                        r.m_retRangeBegin = callInst->getArgOperand(2);
                        ReleaseAssert(llvm_value_has_type<void*>(r.m_retRangeBegin));
                        r.m_retRangeNum = callInst->getArgOperand(3);
                        ReleaseAssert(llvm_value_has_type<uint64_t>(r.m_retRangeNum));
                        result.push_back(r);
                    }
                }
            }
        }
    }
    return result;
}

void UserLibReturnAPI::DoLowering(DeegenLibFuncInstance* ifi)
{
    using namespace llvm;
    LLVMContext& ctx = m_origin->getContext();

    if (m_isFixedNum)
    {
        Constant* numReturns = CreateLLVMConstantInt<uint64_t>(ctx, m_returnValues.size());
        Value* stackbase = ifi->GetStackBase();

        if (m_returnValues.size() < x_minNilFillReturnValues)
        {
            uint64_t val = TValue::Nil().m_value;
            while (m_returnValues.size() < x_minNilFillReturnValues)
            {
                m_returnValues.push_back(CreateLLVMConstantInt<uint64_t>(ctx, val));
            }
        }

        for (size_t i = 0; i < m_returnValues.size(); i++)
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(m_returnValues[i]));
            GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackbase, { CreateLLVMConstantInt<uint64_t>(ctx, i) }, "", m_origin);
            std::ignore = new StoreInst(m_returnValues[i], gep, m_origin);
        }

        m_retRangeBegin = stackbase;
        m_retRangeNum = numReturns;
    }
    else
    {
        CreateCallToDeegenCommonSnippet(ifi->GetModule(), "PopulateNilForReturnValues", { m_retRangeBegin, m_retRangeNum }, m_origin);
    }

    Value* stackbase;
    if (m_isLongJump)
    {
        stackbase = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), m_longJumpFromHeader, { CreateLLVMConstantInt<int64_t>(ctx, x_numSlotsForStackFrameHeader) }, "", m_origin);
    }
    else
    {
        stackbase = ifi->GetStackBase();
    }

    Value* retAddr = CreateCallToDeegenCommonSnippet(ifi->GetModule(), "GetRetAddrFromStackBase", { stackbase }, m_origin);

    ifi->GetFuncContext()->PrepareDispatch<ReturnContinuationInterface>()
        .Set<RPV_StackBase>(stackbase)
        .Set<RPV_RetValsPtr>(m_retRangeBegin)
        .Set<RPV_NumRetVals>(m_retRangeNum)
        .Dispatch(retAddr, m_origin);

    AssertInstructionIsFollowedByUnreachable(m_origin);
    Instruction* unreachableInst = m_origin->getNextNode();
    m_origin->eraseFromParent();
    unreachableInst->eraseFromParent();
    m_origin = nullptr;
}

void DeegenLibLowerThrowErrorAPIs(DeegenLibFuncInstance* ifi, llvm::Function* func)
{
    using namespace llvm;
    LLVMContext& ctx = func->getContext();
    std::vector<CallInst*> listOfUses;
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
                    if (demangledName.starts_with("DeegenLibFuncCommonAPIs::ThrowError("))
                    {
                        listOfUses.push_back(callInst);
                    }
                }
            }
        }
    }

    for (CallInst* callInst : listOfUses)
    {
        ReleaseAssert(callInst->arg_size() == 2);
        std::string demangledName = DemangleCXXSymbol(callInst->getCalledFunction()->getName().str());
        Value* errorObject = callInst->getArgOperand(1);

        Value* errorObjectAsPtr = nullptr;
        Function* target;
        if (demangledName.starts_with("DeegenLibFuncCommonAPIs::ThrowError(TValue"))
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(errorObject));
            target = GetThrowTValueErrorDispatchTargetFunction(ifi->GetModule());
            errorObjectAsPtr = new IntToPtrInst(errorObject, llvm_type_of<void*>(ctx), "", callInst /*insertBefore*/);
        }
        else
        {
            ReleaseAssert(demangledName.starts_with("DeegenLibFuncCommonAPIs::ThrowError(char const*"));
            ReleaseAssert(llvm_value_has_type<void*>(errorObject));
            target = GetThrowCStringErrorDispatchTargetFunction(ifi->GetModule());
            errorObjectAsPtr = errorObject;
        }
        ReleaseAssert(errorObjectAsPtr != nullptr && llvm_value_has_type<void*>(errorObjectAsPtr));

        ifi->GetFuncContext()->PrepareDispatch<FunctionEntryInterface>()
            .Set<RPV_StackBase>(ifi->GetStackBase())
            .Set<RPV_InterpCodeBlockHeapPtrAsPtr>(UndefValue::get(llvm_type_of<void*>(ctx)))
            .Set<RPV_NumArgsAsPtr>(errorObjectAsPtr)    // numArgs repurposed as errorObj
            .Set<RPV_IsMustTailCall>(UndefValue::get(llvm_type_of<uint64_t>(ctx)))
            .Dispatch(target, callInst /*insertBefore*/);

        AssertInstructionIsFollowedByUnreachable(callInst);
        Instruction* unreachableInst = callInst->getNextNode();
        callInst->eraseFromParent();
        unreachableInst->eraseFromParent();
    }
}

void DeegenLibLowerCoroutineSwitchAPIs(DeegenLibFuncInstance* ifi, llvm::Function* func)
{
    using namespace llvm;
    std::vector<CallInst*> listOfUses;
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
                    if (demangledName.starts_with("DeegenLibFuncCommonAPIs::CoroSwitch("))
                    {
                        listOfUses.push_back(callInst);
                    }
                }
            }
        }
    }

    for (CallInst* callInst : listOfUses)
    {
        ReleaseAssert(callInst->arg_size() == 4);
        Value* dstCoro = callInst->getArgOperand(1);
        ReleaseAssert(llvm_value_has_type<void*>(dstCoro));
        Value* dstStackBase = callInst->getArgOperand(2);
        ReleaseAssert(llvm_value_has_type<void*>(dstStackBase));
        Value* numArgs = callInst->getArgOperand(3);
        ReleaseAssert(llvm_value_has_type<uint64_t>(numArgs));

        Value* retAddr = CreateCallToDeegenCommonSnippet(ifi->GetModule(), "GetRetAddrFromStackBase", { dstStackBase }, callInst);
        ReturnContinuationInterface& dispatchCtx = ifi->GetFuncContext()->PrepareDispatch<ReturnContinuationInterface>();
        dispatchCtx.RPV_CoroContext::ClearValue();
        dispatchCtx.Set<RPV_CoroContext>(dstCoro)
            .Set<RPV_StackBase>(dstStackBase)
            .Set<RPV_RetValsPtr>(dstStackBase) /*retStart*/
            .Set<RPV_NumRetVals>(numArgs)
            .Dispatch(retAddr, callInst /*insertBefore*/);

        AssertInstructionIsFollowedByUnreachable(callInst);
        Instruction* unreachableInst = callInst->getNextNode();
        callInst->eraseFromParent();
        unreachableInst->eraseFromParent();
    }
}

void DeegenLibLowerInPlaceCallAPIs(DeegenLibFuncInstance* ifi, llvm::Function* func)
{
    using namespace llvm;
    LLVMContext& ctx = func->getContext();
    std::vector<CallInst*> listOfUses;
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
                    if (demangledName.starts_with("DeegenLibFuncCommonAPIs::MakeInPlaceCall("))
                    {
                        listOfUses.push_back(callInst);
                    }
                }
            }
        }
    }

    for (CallInst* callInst : listOfUses)
    {
        ReleaseAssert(callInst->arg_size() == 4);
        Value* argsBegin = callInst->getArgOperand(1);
        ReleaseAssert(llvm_value_has_type<void*>(argsBegin));
        Value* numArgs = callInst->getArgOperand(2);
        ReleaseAssert(llvm_value_has_type<uint64_t>(numArgs));
        Value* rc = callInst->getArgOperand(3);
        ReleaseAssert(llvm_value_has_type<void*>(rc));

        CreateCallToDeegenCommonSnippet(
            ifi->GetModule(),
            "PopulateNewCallFrameHeaderForCallFromCFunc",
            {
                argsBegin /*newStackBase*/,
                ifi->GetStackBase() /*oldStackBase*/,
                rc /*returnContinuation*/
            },
            callInst /*insertBefore*/);

        Value* calleeSlot = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), argsBegin, { CreateLLVMConstantInt<int64_t>(ctx, -static_cast<int64_t>(x_numSlotsForStackFrameHeader))}, "", callInst /*insertBefore*/);
        Value* calleeValue = new LoadInst(llvm_type_of<uint64_t>(ctx), calleeSlot, "", false /*isVolatile*/, Align(8), callInst /*insertBefore*/);

        Value* codeBlockAndEntryPoint = CreateCallToDeegenCommonSnippet(func->getParent(), "GetCalleeEntryPoint", { calleeValue }, callInst /*insertBefore*/);
        ReleaseAssert(codeBlockAndEntryPoint->getType()->isAggregateType());

        Value* calleeCbHeapPtr = ExtractValueInst::Create(codeBlockAndEntryPoint, { 0 /*idx*/ }, "", callInst /*insertBefore*/);
        Value* codePointer = ExtractValueInst::Create(codeBlockAndEntryPoint, { 1 /*idx*/ }, "", callInst /*insertBefore*/);
        ReleaseAssert(llvm_value_has_type<void*>(codePointer));

        ReleaseAssert(llvm_value_has_type<HeapPtr<void>>(calleeCbHeapPtr));
        Value* calleeCbHeapPtrAsPtr = new AddrSpaceCastInst(calleeCbHeapPtr, llvm_type_of<void*>(ctx), "", callInst);

        ReleaseAssert(llvm_value_has_type<uint64_t>(numArgs));
        Value* numArgsAsPtr = new IntToPtrInst(numArgs, llvm_type_of<void*>(ctx), "", callInst /*insertBefore*/);

        ifi->GetFuncContext()->PrepareDispatch<FunctionEntryInterface>()
            .Set<RPV_StackBase>(argsBegin)
            .Set<RPV_NumArgsAsPtr>(numArgsAsPtr)
            .Set<RPV_InterpCodeBlockHeapPtrAsPtr>(calleeCbHeapPtrAsPtr)
            .Set<RPV_IsMustTailCall>(CreateLLVMConstantInt<uint64_t>(ctx, 0))
            .Dispatch(codePointer, callInst /*insertBefore*/);

        AssertInstructionIsFollowedByUnreachable(callInst);
        Instruction* unreachableInst = callInst->getNextNode();
        callInst->eraseFromParent();
        unreachableInst->eraseFromParent();
    }
}

}   // anonymous namespace

DeegenLibFuncInstance::DeegenLibFuncInstance(llvm::Function* impl, llvm::Function* target, bool isRc)
    : m_module(impl->getParent()), m_impl(impl), m_target(target), m_isReturnContinuation(isRc)
{
    using namespace llvm;
    LLVMContext& ctx = m_target->getContext();
    ReleaseAssert(m_target->empty());
    ReleaseAssert(m_target->getLinkage() == GlobalValue::ExternalLinkage);
    ReleaseAssert(!m_impl->empty());
    ReleaseAssert(m_impl->getLinkage() == GlobalValue::InternalLinkage);
    ReleaseAssert(m_impl->hasFnAttribute(Attribute::NoReturn));

    ReleaseAssert(!m_impl->hasFnAttribute(Attribute::NoInline));
    m_impl->addFnAttr(Attribute::AlwaysInline);

    {
        std::string funName = m_target->getName().str();
        // It's fine even if this setName cause a auto-renaming due to conflict:
        // we just need to make the current name avaiable
        //
        m_target->setName(funName + "_tmp");

        if (m_isReturnContinuation)
        {
            m_funcCtx = ExecutorFunctionContext::Create(DeegenEngineTier::Interpreter, false /*isJitCode*/, true /*isRetCont*/);
        }
        else
        {
            m_funcCtx = ExecutorFunctionContext::CreateForFunctionEntry(DeegenEngineTier::Interpreter);
        }

        Function* wrapper = m_funcCtx->CreateFunction(m_module, funName);
        ReleaseAssert(wrapper->getName() == funName);

        wrapper->addFnAttr(Attribute::NoReturn);

        {
            // Just sanity check that the fake function is never being called
            //
            for (auto usr : m_target->users())
            {
                CallInst* callInst = dyn_cast<CallInst>(usr);
                if (callInst != nullptr)
                {
                    ReleaseAssert(callInst->getCalledFunction() != m_target);
                }
            }
        }

        // Replace the fake function with the true definition
        //
        m_target->replaceAllUsesWith(wrapper);
        m_target->eraseFromParent();
        m_target = wrapper;
    }

    BasicBlock* bb = BasicBlock::Create(ctx, "", m_target);
    m_valuePreserver.Preserve(x_coroutineCtx, m_funcCtx->GetValueAtEntry<RPV_CoroContext>());

    if (m_isReturnContinuation)
    {
        Value* stackBase = CreateCallToDeegenCommonSnippet(
            m_module, "GetCallerStackBaseFromStackBase", { m_funcCtx->GetValueAtEntry<RPV_StackBase>() }, bb);
        m_valuePreserver.Preserve(x_stackBase, stackBase);
        m_valuePreserver.Preserve(x_retStart, m_funcCtx->GetValueAtEntry<RPV_RetValsPtr>());
        m_valuePreserver.Preserve(x_numRet, m_funcCtx->GetValueAtEntry<RPV_NumRetVals>());

        ReleaseAssert(m_impl->arg_size() == 4);
        CallInst::Create(m_impl, { GetCoroutineCtx(), GetStackBase(), GetRetStart(), GetNumRet() }, "", bb);
    }
    else
    {
        m_valuePreserver.Preserve(x_stackBase, m_funcCtx->GetValueAtEntry<RPV_StackBase>());
        PtrToIntInst* numArgs = new PtrToIntInst(m_funcCtx->GetValueAtEntry<RPV_NumArgsAsPtr>(), llvm_type_of<uint64_t>(ctx), "" /*name*/, bb);
        m_valuePreserver.Preserve(x_numArgs, numArgs);

        ReleaseAssert(m_impl->arg_size() == 3);
        CallInst::Create(m_impl, { GetCoroutineCtx(), GetStackBase(), GetNumArgs() }, "", bb);
    }
    std::ignore = new UnreachableInst(ctx, bb);

    ValidateLLVMFunction(m_target);
}

void DeegenLibFuncInstance::DoLowering()
{
    using namespace llvm;

    m_valuePreserver.RefreshAfterTransform();
    m_impl = nullptr;

    UserLibReturnAPI::LowerAllForFunction(this, m_target);
    DeegenLibLowerThrowErrorAPIs(this, m_target);
    DeegenLibLowerCoroutineSwitchAPIs(this, m_target);
    DeegenLibLowerInPlaceCallAPIs(this, m_target);

    m_target->removeFnAttr(Attribute::NoReturn);
    m_valuePreserver.Cleanup();
}

std::vector<DeegenLibFuncInstanceInfo> WARN_UNUSED DeegenLibFuncProcessor::ParseInfo(llvm::Module* module)
{
    using namespace llvm;
    ReleaseAssert(module != nullptr);
    std::vector<DeegenLibFuncInstanceInfo> result;
    ReleaseAssert(module->getGlobalVariable(x_allDefsHolderSymbolName) != nullptr);

    Constant* defList;
    {
        Constant* wrappedDefList = GetConstexprGlobalValue(module, x_allDefsHolderSymbolName);
        LLVMConstantStructReader reader(module, wrappedDefList);
        defList = reader.Dewrap();
    }

    using Desc = ::detail::deegen_lib_func_definition_info_descriptor;

    LLVMConstantArrayReader defListReader(module, defList);
    uint64_t numDefsInThisTU = defListReader.GetNumElements<Desc>();

    for (size_t i = 0; i < numDefsInThisTU; i++)
    {
        Constant* descCst = defListReader.Get<Desc>(i);
        LLVMConstantStructReader reader(module, descCst);

        Constant* implCst = reader.Get<&Desc::impl>();
        ReleaseAssert(isa<Function>(implCst));
        Function* implFunc = cast<Function>(implCst);
        std::string implFuncName = implFunc->getName().str();
        ReleaseAssert(implFuncName != "");

        Constant* wrapperCst = reader.Get<&Desc::wrapper>();
        ReleaseAssert(isa<Function>(wrapperCst));
        Function* wrapperFunc = cast<Function>(wrapperCst);
        std::string wrapperFuncName = wrapperFunc->getName().str();
        ReleaseAssert(wrapperFuncName != "");

        bool isRc = reader.GetValue<&Desc::isRc>();

        result.push_back({
            .m_implName = implFuncName,
            .m_wrapperName = wrapperFuncName,
            .m_isRc = isRc
        });
    }
    return result;
}

void DeegenLibFuncProcessor::DoLowering(llvm::Module* module)
{
    using namespace llvm;
    std::vector<DeegenLibFuncInstanceInfo> allInfo = ParseInfo(module);

    // Lower the definitions
    //
    std::vector<std::unique_ptr<DeegenLibFuncInstance>> allInstances;
    for (DeegenLibFuncInstanceInfo& item : allInfo)
    {
        Function* implFunc = module->getFunction(item.m_implName);
        ReleaseAssert(implFunc != nullptr);

        Function* wrapperFunc = module->getFunction(item.m_wrapperName);
        ReleaseAssert(wrapperFunc != nullptr);

        allInstances.push_back(std::make_unique<DeegenLibFuncInstance>(implFunc, wrapperFunc, item.m_isRc));
    }

    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::InlineGeneralFunctions);

    for (auto& instance : allInstances)
    {
        instance->DoLowering();
    }

    // Delete the symbol that holds all the definitions from 'llvm.compiler.used', as it is no longer needed
    // This would allow all the implementation functions (which are no longer useful) to be deleted
    //
    {
        GlobalVariable* gv = module->getGlobalVariable(x_allDefsHolderSymbolName);
        ReleaseAssert(gv != nullptr);
        RemoveGlobalValueUsedAttributeAnnotation(gv);
    }

    // This should delete all the symbols made dead by the above change
    //
    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::PerFunctionSimplifyOnly);

    // Assert that the functions and globals that ought to be dead at this point have been deleted
    //
    ReleaseAssert(module->getNamedValue(x_allDefsHolderSymbolName) == nullptr);
    for (DeegenLibFuncInstanceInfo& item : allInfo)
    {
        ReleaseAssert(module->getNamedValue(item.m_implName) == nullptr);
        Function* func = module->getFunction(item.m_wrapperName);
        ReleaseAssert(func != nullptr);
        ReleaseAssert(!func->empty());
        ReleaseAssert(func->getLinkage() == GlobalValue::ExternalLinkage);
        ReleaseAssert(func->getCallingConv() == CallingConv::GHC);
        ReleaseAssert(func->getFunctionType() == RegisterPinningScheme::GetFunctionType(module->getContext()));
    }

    DesugarAndSimplifyLLVMModule(module, DesugaringLevel::Top);

    // Run tag register optimization pass
    //
    for (DeegenLibFuncInstanceInfo& item : allInfo)
    {
        Function* func = module->getFunction(item.m_wrapperName);
        ReleaseAssert(func != nullptr);
        RunTagRegisterOptimizationPass(func);
    }
}

}   // namespace dast

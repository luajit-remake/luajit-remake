#include "deegen_bytecode_ir_components.h"
#include "deegen_ast_inline_cache.h"
#include "deegen_ast_make_call.h"
#include "deegen_ast_slow_path.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "tvalue_typecheck_optimization.h"
#include "base64_util.h"
#include "llvm/IRReader/IRReader.h"

namespace dast {

ReturnContinuationAndSlowPathFinder::ReturnContinuationAndSlowPathFinder(llvm::Function* from, bool ignoreSlowPaths)
{
    m_ignoreSlowPaths = ignoreSlowPaths;

    FindCalls(from);
    FindSlowPaths(from);

    // We disallow the entry function itself to also be a continuation,
    // since the entry function cannot access return values while the continuation function can.
    //
    ReleaseAssert(!m_returnContinuations.count(from));

    // The entry function should not be a slow path either: it just doesn't make sense
    //
    ReleaseAssert(!m_slowPaths.count(from));

    ReleaseAssertImp(m_ignoreSlowPaths, m_slowPaths.size() == 0);

    m_allResults = m_returnContinuations;
    for (llvm::Function* func : m_slowPaths)
    {
        ReleaseAssert(!m_allResults.count(func));
        m_allResults.insert(func);
    }
}

void ReturnContinuationAndSlowPathFinder::FindCalls(llvm::Function* from)
{
    std::vector<AstMakeCall> list = AstMakeCall::GetAllUseInFunction(from);
    for (AstMakeCall& amc : list)
    {
        dfs(amc.m_continuation, true /*isCall*/);
    }
}

void ReturnContinuationAndSlowPathFinder::FindSlowPaths(llvm::Function* from)
{
    if (m_ignoreSlowPaths) { return; }
    std::vector<AstSlowPath> list = AstSlowPath::GetAllUseInFunction(from);
    for (AstSlowPath& sp : list)
    {
        dfs(sp.GetImplFunction(), false /*isCall*/);
    }
}

void ReturnContinuationAndSlowPathFinder::dfs(llvm::Function* cur, bool isCall)
{
    if (cur == nullptr)
    {
        return;
    }
    if (isCall)
    {
        ReleaseAssert(!m_slowPaths.count(cur));
        if (m_returnContinuations.count(cur))
        {
            return;
        }
        m_returnContinuations.insert(cur);
    }
    else
    {
        ReleaseAssert(!m_returnContinuations.count(cur));
        if (m_slowPaths.count(cur))
        {
            return;
        }
        m_slowPaths.insert(cur);
    }

    FindCalls(cur);
    FindSlowPaths(cur);
}


BytecodeIrComponent::BytecodeIrComponent(BytecodeIrComponent::ProcessFusedInIcEffectTag,
                                         BytecodeVariantDefinition* bytecodeDef,
                                         llvm::Function* implTmp,
                                         size_t icEffectOrd)
    : BytecodeIrComponent(bytecodeDef, implTmp, BytecodeIrComponentKind::FusedInInlineCacheEffect)
{
    using namespace llvm;
    ReleaseAssert(m_module != nullptr && m_impl != nullptr);
    LLVMContext& ctx = m_module->getContext();

    ReleaseAssert(m_impl->getLinkage() == GlobalValue::ExternalLinkage);

    {
        std::string implFuncName = m_impl->getName().str();
        ReleaseAssert(implFuncName.ends_with("_impl"));
        m_identFuncName = implFuncName.substr(0, implFuncName.length() - strlen("_impl")) + "_fused_ic_" + std::to_string(icEffectOrd);
    }

    // Populate the implementation of the inline cache adaption placeholder functions
    //
    {
        Function* func = m_module->getFunction(x_adapt_ic_hit_check_behavior_placeholder_fn);
        ReleaseAssert(func != nullptr);
        ReleaseAssert(func->empty());
        func->setLinkage(GlobalValue::InternalLinkage);
        func->addFnAttr(Attribute::AttrKind::AlwaysInline);
        BasicBlock* bb = BasicBlock::Create(ctx, "", func);
        ReleaseAssert(func->arg_size() == 1);
        Value* input = func->getArg(0);
        ReleaseAssert(llvm_value_has_type<bool>(input) && llvm_type_has_type<bool>(func->getReturnType()));
        ReturnInst::Create(ctx, input, bb);
        CopyFunctionAttributes(func /*dst*/, m_impl /*src*/);
        ValidateLLVMFunction(func);
    }

    {
        Function* func = m_module->getFunction(x_adapt_get_ic_effect_ord_behavior_placeholder_fn);
        ReleaseAssert(func != nullptr);
        ReleaseAssert(func->empty());
        func->setLinkage(GlobalValue::InternalLinkage);
        func->addFnAttr(Attribute::AttrKind::AlwaysInline);
        BasicBlock* bb = BasicBlock::Create(ctx, "", func);
        ReleaseAssert(func->arg_size() == 1);
        ReleaseAssert(llvm_value_has_type<uint8_t>(func->getArg(0)));
        ReleaseAssert(llvm_type_has_type<uint8_t>(func->getReturnType()));
        uint8_t icEffectOrdU8 = SafeIntegerCast<uint8_t>(icEffectOrd);
        ReturnInst::Create(ctx, CreateLLVMConstantInt<uint8_t>(ctx, icEffectOrdU8), bb);
        CopyFunctionAttributes(func /*dst*/, m_impl /*src*/);
        ValidateLLVMFunction(func);
    }

    DoOptimization();
}

BytecodeIrComponent::BytecodeIrComponent(BytecodeVariantDefinition* bytecodeDef, llvm::Function* implTmp, BytecodeIrComponentKind processKind)
    : m_processKind(processKind)
    , m_bytecodeDef(bytecodeDef)
    , m_module(nullptr)
    , m_impl(nullptr)
    , m_isSlowPathRetCont(false)
    , m_hasDeterminedIsSlowPathRetCont(false)
{
    using namespace llvm;
    m_module = llvm::CloneModule(*implTmp->getParent());
    m_impl = m_module->getFunction(implTmp->getName());
    ReleaseAssert(m_impl != nullptr);

    if (m_impl->getLinkage() != GlobalValue::InternalLinkage)
    {
        // We require the implementation function to be marked 'static', so they can be automatically dropped
        // after we finished the transformation and made them dead
        //
        fprintf(stderr, "The implementation function of the bytecode (or any of its return continuation) must be marked 'static'!\n");
        abort();
    }

    // Figure out the result function name
    //
    if (m_processKind == BytecodeIrComponentKind::SlowPath)
    {
        m_identFuncName = AstSlowPath::GetPostProcessSlowPathFunctionNameForInterpreter(m_impl);
    }
    else
    {
        std::string implFuncName = m_impl->getName().str();
        ReleaseAssert(implFuncName.ends_with("_impl"));
        m_identFuncName = implFuncName.substr(0, implFuncName.length() - strlen("_impl"));
    }

    // At this stage we can drop the bytecode definition global symbol, which will render all bytecode definitions dead.
    // We temporarily change 'm_impl' to external linkage to keep it alive, then run DCE to strip the dead symbols,
    // so that all bytecode defintions except 'm_impl' are stripped and later processing is faster
    //
    std::string implNameBak = m_impl->getName().str();
    BytecodeVariantDefinition::RemoveUsedAttributeOfBytecodeDefinitionGlobalSymbol(m_module.get());
    ReleaseAssert(m_impl->getLinkage() == GlobalValue::InternalLinkage);
    m_impl->setLinkage(GlobalValue::ExternalLinkage);

    RunLLVMDeadGlobalElimination(m_module.get());

    // Sanity check 'm_impl' is still there
    //
    ReleaseAssert(m_module->getFunction(implNameBak) == m_impl);

    // Run the general inlining pass to inline general (non-API) functions into 'm_impl' and canonicalize it
    // Note that we cannot change 'm_impl' back to internal linkage right now, as otherwise it will be dead and get removed
    //
    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::InlineGeneralFunctions);
    ReleaseAssert(m_module->getFunction(implNameBak) == m_impl);

    // Assert that the inline caching APIs only show up where they are allowed to
    //
    if (m_processKind == BytecodeIrComponentKind::ReturnContinuation || m_processKind == BytecodeIrComponentKind::SlowPath)
    {
        // Inline caching API is not allowed in return continuations or in manual slow paths
        //
        ReleaseAssert(AstInlineCache::GetAllUseInFunction(m_impl).size() == 0);
    }
    else if (m_processKind == BytecodeIrComponentKind::QuickeningSlowPath)
    {
        // TODO FIXME: for now we simply fail if inline caching API is used, but this is unreasonable.
        // Instead, we should lower them to a naive implementation (i.e., no inline caching actually happens)
        //
        ReleaseAssert(AstInlineCache::GetAllUseInFunction(m_impl).size() == 0);
    }

    // For FusedInInlineCacheEffect, our caller constructor needs do something further
    //
    if (m_processKind != BytecodeIrComponentKind::FusedInInlineCacheEffect)
    {
        DoOptimization();
    }
}

void BytecodeIrComponent::DoOptimization()
{
    using namespace llvm;
    ReleaseAssert(m_impl != nullptr);

    // Run TValue typecheck strength reduction
    //
    DesugarAndSimplifyLLVMModule(m_module.get(), DesugarUpToExcluding(DesugaringLevel::TypeSpecialization));
    if (m_processKind == BytecodeIrComponentKind::Main || m_processKind == BytecodeIrComponentKind::FusedInInlineCacheEffect)
    {
        if (m_bytecodeDef->HasQuickeningSlowPath())
        {
            // In this case, we are the fast path
            //
            TValueTypecheckOptimizationPass::DoOptimizationForBytecodeQuickeningFastPath(m_bytecodeDef, m_impl);
        }
        else
        {
            TValueTypecheckOptimizationPass::DoOptimizationForBytecode(m_bytecodeDef, m_impl);
        }
    }
    else if (m_processKind == BytecodeIrComponentKind::QuickeningSlowPath)
    {
        TValueTypecheckOptimizationPass::DoOptimizationForBytecodeQuickeningSlowPath(m_bytecodeDef, m_impl);
    }
    else if (m_processKind == BytecodeIrComponentKind::ReturnContinuation || m_processKind == BytecodeIrComponentKind::SlowPath)
    {
        TValueTypecheckOptimizationPass::DoOptimizationForBytecode(m_bytecodeDef, m_impl);
    }
    else
    {
        ReleaseAssert(false);
    }

    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);
}

BytecodeIrComponent::BytecodeIrComponent(llvm::LLVMContext& ctx, BytecodeVariantDefinition* bytecodeDef, json& j)
{
    using namespace llvm;

    {
        int processKindInt = JSONCheckedGet<int>(j, "kind");
        m_processKind = static_cast<BytecodeIrComponentKind>(processKindInt);
    }

    m_bytecodeDef = bytecodeDef;
    JSONCheckedGet(j, "ident_func_name", m_identFuncName);
    std::string moduleStr = base64_decode(JSONCheckedGet<std::string>(j, "llvm_module"));
    m_module = ParseLLVMModuleFromString(ctx, "bytecode_ir_component_module" /*moduleName*/, moduleStr);
    ReleaseAssert(m_module.get() != nullptr);

    std::string implFuncName = JSONCheckedGet<std::string>(j, "impl_func_name");
    Function* func = m_module->getFunction(implFuncName);
    ReleaseAssert(func != nullptr);
    m_impl = func;

    if (m_processKind == BytecodeIrComponentKind::ReturnContinuation)
    {
        m_hasDeterminedIsSlowPathRetCont = true;
        m_isSlowPathRetCont = JSONCheckedGet<bool>(j, "is_slowpath_ret_cont");
    }
    else
    {
        m_hasDeterminedIsSlowPathRetCont = false;
        ReleaseAssert(!j.count("is_slowpath_ret_cont"));
    }
}

json WARN_UNUSED BytecodeIrComponent::SaveToJSON()
{
    json j;
    j["kind"] = static_cast<int>(m_processKind);
    j["ident_func_name"] = m_identFuncName;
    std::string implFuncName = m_impl->getName().str();
    ReleaseAssert(m_module->getFunction(implFuncName) == m_impl);
    j["impl_func_name"] = implFuncName;
    std::string b64modStr = base64_encode(DumpLLVMModuleAsString(m_module.get()));
    j["llvm_module"] = b64modStr;
    if (m_processKind == BytecodeIrComponentKind::ReturnContinuation)
    {
        j["is_slowpath_ret_cont"] = IsReturnContinuationUsedBySlowPathOnly();
    }
    else
    {
        ReleaseAssert(!m_hasDeterminedIsSlowPathRetCont);
    }
    return j;
}

static llvm::Function* CreateGetBytecodeMetadataPtrPlaceholderFnDeclaration(llvm::Module* module)
{
    using namespace llvm;
    std::string fnName = x_get_bytecode_metadata_ptr_placeholder_fn_name;
    Function* f = module->getFunction(fnName);
    if (f != nullptr)
    {
        ReleaseAssert(f->empty() && f->arg_size() == 0 && llvm_type_has_type<void*>(f->getReturnType()));
        return f;
    }
    ReleaseAssert(module->getNamedValue(fnName) == nullptr);
    FunctionType* fty = FunctionType::get(llvm_type_of<void*>(module->getContext()), { }, false /*isVarArg*/);
    f = Function::Create(fty, GlobalValue::ExternalLinkage, fnName, module);
    ReleaseAssert(f->getName() == fnName);
    return f;
}

BytecodeIrInfo WARN_UNUSED BytecodeIrInfo::Create(BytecodeVariantDefinition* bytecodeDef, llvm::Function* mainImplTmp)
{
    using namespace llvm;
    std::unique_ptr<Module> module = CloneModule(*mainImplTmp->getParent());
    Function* mainImplFn = module->getFunction(mainImplTmp->getName());
    ReleaseAssert(mainImplFn != nullptr);
    LLVMContext& ctx = module->getContext();

    BytecodeIrInfo r;
    r.m_bytecodeDef = bytecodeDef;

    std::string resultMainFnName = GetBaseName(bytecodeDef);
    std::string implFuncName = resultMainFnName + "_impl";
    ReleaseAssert(module->getNamedValue(implFuncName) == nullptr);
    mainImplFn->setName(implFuncName);

    // Parse out all the needed return continuations, in preparation of processing each of them later
    //
    {
        // Find all the return continuations and slow paths.
        // For return continuations, give each of them a unique name right now, and create the declarations.
        // This is necessary for us to later link them together.
        // For slow paths, we will rename them in the end since that's easier.
        //
        // Note that return continuations and slow paths may arbitrarily call each other, so the search needs
        // to be recursive.
        //
        ReturnContinuationAndSlowPathFinder finder(mainImplFn, false /*ignoreSlowPaths*/);

        // Sort the return continuations by their original name for determinism
        //
        std::map<std::string /*fnName*/, Function*> sortedReturnContFns;
        for (Function* fn : finder.m_returnContinuations)
        {
            std::string fnName = fn->getName().str();
            ReleaseAssert(!sortedReturnContFns.count(fnName));
            sortedReturnContFns[fnName] = fn;
        }

        std::vector<Function*> rcList;
        {
            size_t rcOrd = 0;
            for (auto& it : sortedReturnContFns)
            {
                Function* rc = it.second;
                rcList.push_back(rc);
                std::string rcFinalName = GetRetContFuncName(bytecodeDef, rcOrd);
                std::string rcImplName = rcFinalName + "_impl";
                ReleaseAssert(module->getNamedValue(rcFinalName) == nullptr);
                ReleaseAssert(module->getNamedValue(rcImplName) == nullptr);
                rc->setName(rcImplName);
                rcOrd++;
            }
        }

        // After all the renaming and function declarations, create the InterpreterBytecodeImplCreator class so we can process each of the return continuation later
        // Note that creating the InterpreterBytecodeImplCreator also clones the module, so we want to do it after all the renaming but before our own processing begins
        //
        for (Function* targetRc : rcList)
        {
            ReleaseAssert(targetRc != nullptr);
            r.m_allRetConts.push_back(std::make_unique<BytecodeIrComponent>(bytecodeDef, targetRc, BytecodeIrComponentKind::ReturnContinuation));
        }

        // Process the quickening slow path if needed
        //
        if (bytecodeDef->HasQuickeningSlowPath())
        {
            // Temporarily change 'm_impl' to the function name for the slowpath
            //
            std::string oldName = mainImplFn->getName().str();
            std::string desiredName = GetQuickeningSlowPathFuncName(bytecodeDef) + "_impl";
            mainImplFn->setName(desiredName);
            ReleaseAssert(mainImplFn->getName().str() == desiredName);

            // Construct the slowpath creator. This clones the module in the process, and the impl function in the cloned module has the new name, as desired.
            //
            r.m_quickeningSlowPath = std::make_unique<BytecodeIrComponent>(bytecodeDef, mainImplFn, BytecodeIrComponentKind::QuickeningSlowPath);

            // Revert the name
            //
            mainImplFn->setName(oldName);
            ReleaseAssert(mainImplFn->getName().str() == oldName);
        }

        // Sanity check that all the slow path calls have the right types
        //
        for (Function* fn : finder.m_allResults)
        {
            std::vector<AstSlowPath> slowPaths = AstSlowPath::GetAllUseInFunction(fn);
            for (AstSlowPath& sp : slowPaths)
            {
                sp.CheckWellFormedness(mainImplFn);
            }
        }

        // Process the slow paths if any.
        //
        if (finder.m_slowPaths.size() > 0)
        {
            // Sort all the functions by function name and process them in sorted order for determinism
            //
            std::map<std::string /*fnName*/, Function*> sortedFns;
            for (Function* fn : finder.m_slowPaths)
            {
                std::string fnName = fn->getName().str();
                ReleaseAssert(!sortedFns.count(fnName));
                sortedFns[fnName] = fn;
            }

            for (auto& it : sortedFns)
            {
                Function* implFn = it.second;
                r.m_slowPaths.push_back(std::make_unique<BytecodeIrComponent>(bytecodeDef, implFn, BytecodeIrComponentKind::SlowPath));
            }
        }
    }

    r.m_jitMainComponent = std::make_unique<BytecodeIrComponent>(bytecodeDef, mainImplFn, BytecodeIrComponentKind::Main);

    // Process all the inline caches
    //
    std::vector<AstInlineCache> allIcUses = AstInlineCache::GetAllUseInFunction(mainImplFn);

    bool hasICFusedIntoInterpreterOpcode = false;
    size_t numFusedIcEffects = static_cast<size_t>(-1);
    for (size_t icOrd = 0; icOrd < allIcUses.size(); icOrd++)
    {
        AstInlineCache& ic = allIcUses[icOrd];
        if (ic.m_shouldFuseICIntoInterpreterOpcode)
        {
            if (hasICFusedIntoInterpreterOpcode)
            {
                fprintf(stderr, "[ERROR] Function '%s' contained more than one IC with FuseICIntoInterpreterOpcode attribute!\n", resultMainFnName.c_str());
                abort();
            }
            hasICFusedIntoInterpreterOpcode = true;
            numFusedIcEffects = ic.m_totalEffectKinds;
        }
    }

    if (hasICFusedIntoInterpreterOpcode && bytecodeDef->HasQuickeningSlowPath())
    {
        fprintf(stderr, "[LOCKDOWN] FuseICIntoInterpreterOpcode() currently cannot be used together with EnableHotColdSplitting().\n");
        abort();
    }

    size_t numTotalGenericIcEffectKinds = 0;

    for (size_t icOrd = 0; icOrd < allIcUses.size(); icOrd++)
    {
        AstInlineCache& ic = allIcUses[icOrd];

        numTotalGenericIcEffectKinds += ic.m_totalEffectKinds;

        // Lower the inline caching APIs
        //
        ic.DoLoweringForInterpreter();

        // Append required data into the bytecode metadata struct
        //
        bytecodeDef->AddBytecodeMetadata(std::move(ic.m_icStruct));

        // Note that AstInlineCache::DoLoweringForInterpreter by design does not lower the GetIcPtr calls
        // (and we cannot lower them at this stage because the relavent interpreter context isn't available yet).
        // Therefore, here we only change the dummy function name to a special one, so we can locate them and lower them later.
        //
        CallInst* icPtrGetter = ic.m_icPtrOrigin;
        ReleaseAssert(icPtrGetter->arg_size() == 0 && llvm_value_has_type<void*>(icPtrGetter));
        ReleaseAssert(AstInlineCache::IsIcPtrGetterFunctionCall(icPtrGetter));
        Function* replacementFn = CreateGetBytecodeMetadataPtrPlaceholderFnDeclaration(module.get());
        icPtrGetter->setCalledFunction(replacementFn);

        // Rename and record the IC body name. If LLVM chooses to not inline it, we need to extract it as well.
        // Note that we don't need to do anything to the IC effect functions, since they have been marked always_inline
        //
        std::string icBodyNewName = resultMainFnName + "_icbody_" + std::to_string(icOrd);
        ReleaseAssert(module->getNamedValue(icBodyNewName) == nullptr);
        ic.m_bodyFn->setName(icBodyNewName);
        ReleaseAssert(ic.m_bodyFn->getName() == icBodyNewName);
        ReleaseAssert(ic.m_bodyFn->getLinkage() == GlobalValue::InternalLinkage);
        r.m_icBodyNames.push_back(icBodyNewName);

        // Change the calling convention of the IC body to PreserveMost, since it's the slow path and we want it to be less intrusive
        //
        ic.m_bodyFn->setCallingConv(CallingConv::PreserveMost);
        for (User* usr : ic.m_bodyFn->users())
        {
            CallInst* callInst = dyn_cast<CallInst>(usr);
            if (callInst != nullptr)
            {
                ReleaseAssert(callInst->getCalledFunction() == ic.m_bodyFn);
                callInst->setCallingConv(CallingConv::PreserveMost);
            }
        }

        if (hasICFusedIntoInterpreterOpcode)
        {
            // The body fn should never be inlined since it's going to be called by multiple bytecodes
            //
            ic.m_bodyFn->setLinkage(GlobalValue::ExternalLinkage);
            ic.m_bodyFn->addFnAttr(Attribute::AttrKind::NoInline);
        }
        else
        {
            // We need to give it one more chance to inline, because previously when the inline pass was run,
            // the call from the bytecode implementation to the IC body was not visible: it only becomes visible after the lowering.
            //
            LLVMRepeatedInliningInhibitor::GiveOneMoreChance(ic.m_bodyFn);
        }
    }

    ReleaseAssert(bytecodeDef->m_numJitGenericICs == static_cast<size_t>(-1));
    bytecodeDef->m_numJitGenericICs = allIcUses.size();

    ReleaseAssert(bytecodeDef->m_totalGenericIcEffectKinds == static_cast<size_t>(-1));
    bytecodeDef->m_totalGenericIcEffectKinds = numTotalGenericIcEffectKinds;

    if (hasICFusedIntoInterpreterOpcode)
    {
        // Create the processors for each of the specialized implementation
        //
        for (size_t i = 0; i < numFusedIcEffects; i++)
        {
            std::unique_ptr<BytecodeIrComponent> processor = std::make_unique<BytecodeIrComponent>(
                BytecodeIrComponent::ProcessFusedInIcEffectTag(), bytecodeDef, mainImplFn, i /*effectKindOrd*/);
            r.m_affliatedBytecodeFnNames.push_back(processor->m_identFuncName);
            r.m_fusedICs.push_back(std::move(processor));
        }

        // Populate the implementation of the inline cache adaption placeholder functions
        // Since we are only called for the empty IC case, we know that the IC hit check will always fail
        //
        {
            Function* func = module->getFunction(x_adapt_ic_hit_check_behavior_placeholder_fn);
            ReleaseAssert(func != nullptr);
            ReleaseAssert(func->empty());
            func->setLinkage(GlobalValue::InternalLinkage);
            func->addFnAttr(Attribute::AttrKind::AlwaysInline);
            BasicBlock* bb = BasicBlock::Create(ctx, "", func);
            ReleaseAssert(func->arg_size() == 1);
            ReleaseAssert(llvm_value_has_type<bool>(func->getArg(0)));
            ReleaseAssert(llvm_type_has_type<bool>(func->getReturnType()));
            ReturnInst::Create(ctx, CreateLLVMConstantInt<bool>(ctx, false), bb);
            CopyFunctionAttributes(func /*dst*/, mainImplFn /*src*/);
            ValidateLLVMFunction(func);
        }

        {
            Function* func = module->getFunction(x_adapt_get_ic_effect_ord_behavior_placeholder_fn);
            ReleaseAssert(func != nullptr);
            ReleaseAssert(func->empty());
            func->setLinkage(GlobalValue::InternalLinkage);
            func->addFnAttr(Attribute::AttrKind::AlwaysInline);
            BasicBlock* bb = BasicBlock::Create(ctx, "", func);
            ReleaseAssert(func->arg_size() == 1);
            Value* input = func->getArg(0);
            ReleaseAssert(llvm_value_has_type<uint8_t>(input));
            ReleaseAssert(llvm_type_has_type<uint8_t>(func->getReturnType()));
            ReturnInst::Create(ctx, input, bb);
            CopyFunctionAttributes(func /*dst*/, mainImplFn /*src*/);
            ValidateLLVMFunction(func);
        }
    }

    r.m_interpreterMainComponent = std::make_unique<BytecodeIrComponent>(bytecodeDef, mainImplFn, BytecodeIrComponentKind::Main);

    ReleaseAssert(!bytecodeDef->IsBytecodeStructLengthTentativelyFinalized());

    // Figure out whether this bytecode has metadata.
    // This is a bit tricky. We relying on the fact that whether or not the bytecode metadata exists depends
    // solely on the logic of the Main processor.
    // We probably should refactor it so that it's less tricky. But let's stay with it for now.
    //
    {
        // Figure out if we need Call IC. We need it if there are any calls used in the main function.
        // Note that we must do this check here after all optimizations have happened, as optimizations could have
        // deduced that calls are unreachable and removed them.
        //
        bool needInterpreterCallIc = false;
        size_t numMakeCallsInMainFn = AstMakeCall::GetAllUseInFunction(r.m_interpreterMainComponent->m_impl).size();
        ReleaseAssert(bytecodeDef->m_numJitCallICs == static_cast<size_t>(-1));
        bytecodeDef->m_numJitCallICs = numMakeCallsInMainFn;
        if (numMakeCallsInMainFn > 0)
        {
            needInterpreterCallIc = true;
        }
        if (bytecodeDef->m_isInterpreterCallIcExplicitlyDisabled)
        {
            needInterpreterCallIc = false;
        }
        if (needInterpreterCallIc)
        {
            bytecodeDef->AddInterpreterCallIc();
        }
    }

    // At this point we should have determined everything that needs to sit in the bytecode metadata (if any)
    //
    bytecodeDef->TentativelyFinalizeBytecodeStructLength();

    return r;
}

BytecodeIrInfo::BytecodeIrInfo(llvm::LLVMContext& ctx, json& j)
{
    ReleaseAssert(j.count("bytecode_variant_definition"));
    m_bytecodeDefHolder = std::make_unique<BytecodeVariantDefinition>(j["bytecode_variant_definition"]);
    m_bytecodeDef = m_bytecodeDefHolder.get();
    ReleaseAssert(j.count("jit_main_component"));
    m_jitMainComponent = std::make_unique<BytecodeIrComponent>(ctx, m_bytecodeDef, j["jit_main_component"]);

    {
        ReleaseAssert(j.count("all_return_continuations") && j["all_return_continuations"].is_array());
        // TODO: avoid copy
        std::vector<json> allRetConts = j["all_return_continuations"];
        for (auto& it : allRetConts)
        {
            m_allRetConts.push_back(std::make_unique<BytecodeIrComponent>(ctx, m_bytecodeDef, it));
        }
    }

    if (j.count("quickening_slow_path"))
    {
        m_quickeningSlowPath = std::make_unique<BytecodeIrComponent>(ctx, m_bytecodeDef, j["quickening_slow_path"]);
    }

    {
        ReleaseAssert(j.count("all_slow_paths") && j["all_slow_paths"].is_array());
        std::vector<json> allSlowPaths = j["all_slow_paths"];
        for (auto& it : allSlowPaths)
        {
            m_slowPaths.push_back(std::make_unique<BytecodeIrComponent>(ctx, m_bytecodeDef, it));
        }
    }
}

json WARN_UNUSED BytecodeIrInfo::SaveToJSON()
{
    json j;
    j["bytecode_variant_definition"] = m_bytecodeDef->SaveToJSON();
    j["jit_main_component"] = m_jitMainComponent->SaveToJSON();
    {
        std::vector<json> allRetConts;
        for (auto& it : m_allRetConts)
        {
            allRetConts.push_back(it->SaveToJSON());
        }
        j["all_return_continuations"] = std::move(allRetConts);
    }
    if (m_quickeningSlowPath.get() != nullptr)
    {
        j["quickening_slow_path"] = m_quickeningSlowPath->SaveToJSON();
    }
    {
        std::vector<json> allSlowPaths;
        for (auto& it : m_slowPaths)
        {
            allSlowPaths.push_back(it->SaveToJSON());
        }
        j["all_slow_paths"] = std::move(allSlowPaths);
    }
    return j;
}

}   // namespace dast

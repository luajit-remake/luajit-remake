#include "deegen_jit_codegen_logic_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_dfg_jit_impl_creator.h"
#include "llvm/Linker/Linker.h"
#include "llvm/IR/ReplaceConstant.h"
#include "deegen_jit_slow_path_data.h"
#include "drt/baseline_jit_codegen_helper.h"
#include "deegen_ast_inline_cache.h"
#include "deegen_stencil_fixup_cross_reference_helper.h"
#include "drt/dfg_codegen_protocol.h"
#include "drt/dfg_slowpath_register_config_helper.h"
#include "deegen_dfg_jit_regalloc_rt_call_wrapper.h"

namespace dast {

struct MergedStencilSection
{
    MergedStencilSection() : m_alignment(1) { }

    using CondBrLatePatchRecord = DeegenStencilCodegenResult::CondBrLatePatchRecord;

    size_t m_alignment;
    std::vector<uint8_t> m_code;
    std::vector<bool> m_relocMarker;
    std::vector<CondBrLatePatchRecord> m_condBrRecords;

    // Return the offset of the added section in the merged section.
    // Note that currently for simplicity, we do not attempt to minimize padding. We just add the sections in the provided order.
    //
    size_t WARN_UNUSED Add(size_t alignment,
                           const std::vector<uint8_t>& code,
                           const std::vector<bool>& relocMarker,
                           const std::vector<CondBrLatePatchRecord>& condBrRecords)
    {
        ReleaseAssert(m_code.size() == m_relocMarker.size());
        ReleaseAssert(code.size() == relocMarker.size());
        ReleaseAssert(alignment > 0 && m_alignment > 0);
        if (alignment > m_alignment)
        {
            ReleaseAssert(alignment % m_alignment == 0);
            m_alignment = alignment;
        }
        else
        {
            ReleaseAssert(m_alignment % alignment == 0);
        }

        size_t offset = (m_code.size() + alignment - 1) / alignment * alignment;
        ReleaseAssert(offset >= m_code.size());
        size_t numPadding = offset - m_code.size();
        for (size_t i = 0; i < numPadding; i++)
        {
            m_code.push_back(0);
            m_relocMarker.push_back(false);
        }

        m_code.insert(m_code.end(), code.begin(), code.end());
        m_relocMarker.insert(m_relocMarker.end(), relocMarker.begin(), relocMarker.end());

        for (auto& rec : condBrRecords)
        {
            ReleaseAssert(rec.m_offset < code.size());
            ReleaseAssert(rec.m_offset + (rec.m_is64Bit ? 8ULL : 4ULL) <= code.size());
            m_condBrRecords.push_back({ .m_offset = rec.m_offset + offset, .m_is64Bit = rec.m_is64Bit });
        }

        return offset;
    }
};

static llvm::GlobalVariable* WARN_UNUSED GetBaselineJitCodegenFnDispatchTable(llvm::Module* module)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    constexpr const char* x_dispatchTableSymbolName = "__deegen_baseline_jit_codegen_dispatch_table";
    llvm::ArrayType* dispatchTableTy = llvm::ArrayType::get(llvm_type_of<void*>(ctx), 0 /*numElements*/);

    {
        GlobalVariable* gv = module->getGlobalVariable(x_dispatchTableSymbolName);
        if (gv != nullptr)
        {
            ReleaseAssert(gv->getValueType() == dispatchTableTy);
            return gv;
        }
    }

    ReleaseAssert(module->getNamedValue(x_dispatchTableSymbolName) == nullptr);
    GlobalVariable* dispatchTable = new GlobalVariable(*module,
                                                       dispatchTableTy /*valueType*/,
                                                       true /*isConstant*/,
                                                       GlobalValue::ExternalLinkage,
                                                       nullptr /*initializer*/,
                                                       x_dispatchTableSymbolName /*name*/);
    ReleaseAssert(dispatchTable->getName().str() == x_dispatchTableSymbolName);
    ReleaseAssert(dispatchTable->getValueType() == dispatchTableTy);
    dispatchTable->setAlignment(MaybeAlign(8));
    dispatchTable->setDSOLocal(true);

    return dispatchTable;
}

static llvm::Value* WARN_UNUSED GetBaselineJitCodegenFnDispatchTableEntry(llvm::Module* module, llvm::Value* index, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = index->getContext();
    ReleaseAssert(llvm_value_has_type<uint64_t>(index));

    llvm::GlobalVariable* dispatchTable = GetBaselineJitCodegenFnDispatchTable(module);
    Value* addr = GetElementPtrInst::CreateInBounds(
            dispatchTable->getValueType() /*pointeeType*/, dispatchTable,
            { CreateLLVMConstantInt<uint64_t>(ctx, 0), index }, "", insertBefore);

    Value* result = new LoadInst(llvm_type_of<void*>(ctx), addr, "", false /*isVolatile*/, Align(8), insertBefore);
    ReleaseAssert(llvm_value_has_type<void*>(result));
    return result;
}

static llvm::Value* WARN_UNUSED GetBaselineJitCodegenFnDispatchTableEntry(llvm::Module* module, llvm::Value* index, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(module->getContext(), insertAtEnd);
    Value* res = GetBaselineJitCodegenFnDispatchTableEntry(module, index, dummy);
    dummy->eraseFromParent();
    return res;
}

JitCodeGenLogicCreator WARN_UNUSED JitCodeGenLogicCreator::CreateForBaselineJIT(BytecodeIrInfo& bii, const DeegenGlobalBytecodeTraitAccessor& bcTraitAccessor)
{
    using namespace llvm;
    ReleaseAssert(bii.m_jitMainComponent.get() != nullptr);

    JitCodeGenLogicCreator res;
    res.SetBII(&bii);
    res.m_gbta = &bcTraitAccessor;

    BaselineJitImplCreator mainJic(&bii, *bii.m_jitMainComponent.get());
    res.DoAllLowering(&mainJic);
    res.GenerateLogic(&mainJic);
    return res;
}

void JitCodeGenLogicCreator::DoAllLowering(JitImplCreatorBase* mainComponentJic)
{
    using namespace llvm;

    ReleaseAssert(!m_didLowering);
    m_didLowering = true;

    BytecodeVariantDefinition* bytecodeDef = mainComponentJic->GetBytecodeDef();
    ReleaseAssert(bytecodeDef == GetBII()->m_bytecodeDef);

    ReleaseAssertIff(mainComponentJic->IsDfgJIT(), IsDfgVariant());
    ReleaseAssertIff(mainComponentJic->IsBaselineJIT(), !IsDfgVariant());

    ReleaseAssert(m_rcJicList.empty());
    for (auto& rc : GetBII()->m_allRetConts)
    {
        if (!rc->IsReturnContinuationUsedBySlowPathOnly())
        {
            if (IsDfgVariant())
            {
                // Reg alloc is always disabled if the fast path makes a guest language call
                //
                ReleaseAssert(mainComponentJic->AsDfgJIT()->IsRegAllocDisabled());
                ReleaseAssert(mainComponentJic->AsDfgJIT()->IsFastPathRegAllocAlwaysDisabled());
                std::unique_ptr<DfgJitImplCreator> jic = std::make_unique<DfgJitImplCreator>(GetBII(), *rc.get(), nullptr /*gbta*/);
                jic->SetIsFastPathRegAllocAlwaysDisabled(mainComponentJic->AsDfgJIT()->IsFastPathRegAllocAlwaysDisabled());
                jic->DisableRegAlloc(DfgJitImplCreator::RegAllocDisableReason::NotFastPath);
                m_rcJicList.push_back(std::move(jic));
            }
            else
            {
                m_rcJicList.push_back(std::unique_ptr<BaselineJitImplCreator>(new BaselineJitImplCreator(GetBII(), *rc.get())));
            }
        }
    }

    auto lowerOneComponent = [&](JitImplCreatorBase* component)
    {
        if (component->IsBaselineJIT())
        {
            // For baseline JIT, the SlowPathDataLayout is automatically set up by the BaselineJitImplCreator, no work for us.
            //
            component->AsBaselineJIT()->DoLowering(GetBII(), *GetGBTA());
        }
        else
        {
            ReleaseAssert(component->IsDfgJIT());
            // For DFG, the DfgSlowPathDataLayout must be set up by us before DoLowering. The mainComponent should already have it set up,
            // and each return continuation component should use the layout object used in the main component
            //
            if (component == mainComponentJic)
            {
                ReleaseAssert(component->AsDfgJIT()->IsDfgJitSlowPathDataLayoutSetUp());
            }
            else
            {
                component->AsDfgJIT()->SetDfgJitSlowPathDataLayout(mainComponentJic->AsDfgJIT()->GetDfgJitSlowPathDataLayout());
            }
            component->AsDfgJIT()->DoLowering(false /*forRegisterDemandTest*/);
        }
        m_implModulesForAudit.push_back(std::make_pair(CloneModule(*component->GetModule()), component->GetResultFunctionName()));
    };

    lowerOneComponent(mainComponentJic);
    for (auto& rcJic : m_rcJicList)
    {
        lowerOneComponent(rcJic.get());
    }

    if (!IsDfgVariant())
    {
        // The SlowPathDataLayout used by all the BaselineJitImplCreators resides in the BytecodeVariantDefinition
        // After lowering all JIT components, we can finalize the SlowPathDataLayout
        //
        // Note that for DFG variants, the SlowPathDataLayout cannot be finalized now (since there may be other reg alloc variants),
        // and our caller is responsible for finalizing the layout
        //
        mainComponentJic->AsBaselineJIT()->GetBaselineJitSlowPathDataLayout()->FinalizeLayout();
    }
}

void JitCodeGenLogicCreator::GenerateLogic(JitImplCreatorBase* mainComponentJic, std::string dfgFnNameExtraSuffix)
{
    using namespace llvm;
    LLVMContext& ctx = GetBII()->m_jitMainComponent->m_module->getContext();

    ReleaseAssert(!m_generated && m_didLowering);
    m_generated = true;

    ReleaseAssert(mainComponentJic->GetJitSlowPathDataLayoutBase()->IsLayoutFinalized());

    BytecodeVariantDefinition* bytecodeDef = mainComponentJic->GetBytecodeDef();
    ReleaseAssert(bytecodeDef == GetBII()->m_bytecodeDef);

    ReleaseAssertIff(mainComponentJic->IsDfgJIT(), IsDfgVariant());
    ReleaseAssertIff(mainComponentJic->IsBaselineJIT(), !IsDfgVariant());
    ReleaseAssertImp(!IsDfgVariant(), dfgFnNameExtraSuffix == "");

    // For now, stay simple and always layout all the return continuations in the order given, and let the last
    // return continuation fallthrough to the next bytecode.
    // This is good enough, because almost all bytecodes have only 1 possible (non-slowpath) return continuation
    //
    std::vector<JitImplCreatorBase*> stencilGeneratorList;
    stencilGeneratorList.push_back(mainComponentJic);
    for (auto& rcJic : m_rcJicList)
    {
        stencilGeneratorList.push_back(rcJic.get());
    }

    std::vector<DeegenStencil> stencilList;
    for (JitImplCreatorBase* jic : stencilGeneratorList)
    {
        stencilList.push_back(jic->GetStencil());
    }

    ReleaseAssert(stencilList.size() == stencilGeneratorList.size());
    ReleaseAssert(stencilList.size() > 0);

    std::unordered_map<std::string, size_t> stencilToFastPathOffsetMap;
    {
        size_t offset = 0;
        for (size_t i = 0; i < stencilList.size(); i++)
        {
            std::string name = stencilGeneratorList[i]->GetResultFunctionName();
            ReleaseAssert(!stencilToFastPathOffsetMap.count(name));
            stencilToFastPathOffsetMap[name] = offset;
            offset += stencilList[i].m_fastPathCode.size();
        }
    }

    std::string bytecodeIdName = bytecodeDef->GetBytecodeIdName();

    if (IsDfgVariant())
    {
        // DFG does not use call IC
        //
        ReleaseAssert(mainComponentJic->GetAllCallIcInfo().size() == 0);
    }
    else
    {
        ReleaseAssert(mainComponentJic->GetAllCallIcInfo().size() == bytecodeDef->GetNumCallICsInBaselineJitTier());
        ReleaseAssert(GetGBTA()->GetNumJitCallICSites(bytecodeIdName) == bytecodeDef->GetNumCallICsInBaselineJitTier());

        // For sanity, assert that the ordinals of callIcInfo should exactly cover [0, numCallIc)
        //
        {
            std::unordered_set<uint64_t> checkSet;
            for (size_t i = 0; i < bytecodeDef->GetNumCallICsInBaselineJitTier(); i++)
            {
                checkSet.insert(i);
            }
            for (auto& callIcInfo : mainComponentJic->GetAllCallIcInfo())
            {
                uint64_t ord = callIcInfo.m_uniqueOrd;
                ReleaseAssert(checkSet.count(ord));
                checkSet.erase(checkSet.find(ord));
            }
            ReleaseAssert(checkSet.empty());
        }
    }

    std::unique_ptr<Module> icCodegenSlowpathModule;

    auto storeIcCodegenFunction = [&](std::unique_ptr<Module> cgFnModule)
    {
        ReleaseAssert(cgFnModule.get() != nullptr);
        SetSectionsForIcCodegenModule(mainComponentJic->GetTier(), cgFnModule.get());

        if (icCodegenSlowpathModule.get() == nullptr)
        {
            icCodegenSlowpathModule = std::move(cgFnModule);
        }
        else
        {
            Linker linker(*icCodegenSlowpathModule.get());
            ReleaseAssert(linker.linkInModule(std::move(cgFnModule)) == false);
        }
    };

    if (!IsDfgVariant())
    {
        for (auto& callIcInfo : mainComponentJic->GetAllCallIcInfo())
        {
            DeegenCallIcLogicCreator::BaselineJitCodegenResult callIcCgRes =
                DeegenCallIcLogicCreator::CreateBaselineJitCallIcCreator(mainComponentJic->AsBaselineJIT(),
                                                                         stencilToFastPathOffsetMap,
                                                                         stencilList[0] /*inout*/,
                                                                         callIcInfo,
                                                                         *GetGBTA());

            // Dump audit log
            //
            {
                // CompileLLVMModuleToAssemblyFile unfortunately can make subtle changes to the module..
                // Even though hopefully those changes are harmless, just don't take unnecessary risks only to dump an audit log
                //
                std::unique_ptr<Module> clonedModule = CloneModule(*callIcCgRes.m_module.get());
                std::string asmFile = CompileLLVMModuleToAssemblyFile(clonedModule.get(), Reloc::Static, CodeModel::Small);

                std::string icAuditLog = callIcCgRes.m_disasmForAudit + asmFile;
                std::string icAuditLogFileName = bytecodeDef->GetBytecodeIdName() + dfgFnNameExtraSuffix + "_call_ic_" + std::to_string(callIcCgRes.m_uniqueOrd) + ".s";
                m_extraAuditFiles.push_back(std::make_pair(icAuditLogFileName, icAuditLog));
            }

            // Emit info for the call IC trait table
            //
            m_allCallIcTraitDescs.push_back({
                .m_ordInTraitTable = GetGBTA()->GetJitCallIcTraitOrd(bytecodeIdName, callIcCgRes.m_uniqueOrd, true /*isDirectCall*/),
                .m_allocationLength = callIcCgRes.m_dcIcSize,
                .m_isDirectCall = true,
                .m_codePtrPatchRecords = callIcCgRes.m_dcIcCodePtrPatchRecords
            });

            m_allCallIcTraitDescs.push_back({
                .m_ordInTraitTable = GetGBTA()->GetJitCallIcTraitOrd(bytecodeIdName, callIcCgRes.m_uniqueOrd, false /*isDirectCall*/),
                .m_allocationLength = callIcCgRes.m_ccIcSize,
                .m_isDirectCall = false,
                .m_codePtrPatchRecords = callIcCgRes.m_ccIcCodePtrPatchRecords
            });

            ReleaseAssert(callIcCgRes.m_module->getFunction(callIcCgRes.m_resultFnName) != nullptr);

            // Store the codegen function
            //
            storeIcCodegenFunction(std::move(callIcCgRes.m_module));
        }
    }

    auto& genericIcInfo = mainComponentJic->GetGenericIcLoweringResult();
    if (genericIcInfo.m_icBodyModule.get() != nullptr)
    {
        genericIcInfo.LateFixSlowPathDataLength(mainComponentJic->GetJitSlowPathDataLayoutBase()->GetTotalLength());

        storeIcCodegenFunction(std::move(genericIcInfo.m_icBodyModule));

        std::string icAuditLogFileName = bytecodeDef->GetBytecodeIdName() + dfgFnNameExtraSuffix + "_generic_ic.s";
        m_extraAuditFiles.push_back(std::make_pair(icAuditLogFileName, genericIcInfo.m_disasmForAudit));
        m_allGenericIcTraitDescs = genericIcInfo.m_icTraitInfo;
    }

    struct StencilCgInfo
    {
        JitImplCreatorBase* origin;
        DeegenStencilCodegenResult cgRes;
        size_t offsetInFastPath;
        size_t offsetInSlowPath;
        size_t offsetInDataSec;
        Function* fastPathPatchFn;
        Function* slowPathPatchFn;
        Function* dataSecPatchFn;
    };

    std::vector<StencilCgInfo> stencilCgInfos;
    for (size_t i = 0; i < stencilGeneratorList.size(); i++)
    {
        JitImplCreatorBase* jic = stencilGeneratorList[i];

        // Only the last stencil may eliminate the tail jmp to fallthrough.
        // The baseline/DFG JitImplCreator has already done optimizations based on assumptions of which stencil is the last stencil
        // Assert that we are agreeing with them on the stencil ordering.
        //
        bool isLastStencilInBytecode = (i + 1 == stencilGeneratorList.size());
        ReleaseAssertIff(isLastStencilInBytecode, jic->IsLastJitStencilInBytecode());

        DeegenStencilCodegenResult cgResult = stencilList[i].PrintCodegenFunctions(
            bytecodeDef->m_list.size() /*numBytecodeOperands*/,
            jic->GetNumTotalGenericIcCaptures(),
            jic->GetStencilRcDefinitions() /*placeholderDefs*/);

        stencilCgInfos.push_back(StencilCgInfo {
            .origin = jic,
            .cgRes = cgResult,
            .offsetInFastPath = static_cast<size_t>(-1),
            .offsetInSlowPath = static_cast<size_t>(-1),
            .offsetInDataSec = static_cast<size_t>(-1),
            .fastPathPatchFn = nullptr,
            .slowPathPatchFn = nullptr,
            .dataSecPatchFn = nullptr
        });
    }

    MergedStencilSection fastPath, slowPath, dataSec;
    for (StencilCgInfo& cgi : stencilCgInfos)
    {
        cgi.offsetInFastPath = fastPath.Add(1 /*alignment*/, cgi.cgRes.m_fastPathPreFixupCode, cgi.cgRes.m_fastPathRelocMarker, cgi.cgRes.m_condBrFixupOffsetsInFastPath);
        cgi.offsetInSlowPath = slowPath.Add(1 /*alignment*/, cgi.cgRes.m_slowPathPreFixupCode, cgi.cgRes.m_slowPathRelocMarker, cgi.cgRes.m_condBrFixupOffsetsInSlowPath);
        cgi.offsetInDataSec = dataSec.Add(cgi.cgRes.m_dataSecAlignment, cgi.cgRes.m_dataSecPreFixupCode, cgi.cgRes.m_dataSecRelocMarker, cgi.cgRes.m_condBrFixupOffsetsInDataSec);
    }

    m_fastPathCodeLen = fastPath.m_code.size();
    m_slowPathCodeLen = slowPath.m_code.size();
    m_dataSectionCodeLen = dataSec.m_code.size();
    m_dataSectionAlignment = dataSec.m_alignment;

    ReleaseAssert(stencilCgInfos.size() == stencilGeneratorList.size());
    for (size_t i = 0; i < stencilGeneratorList.size(); i++)
    {
        std::string name = stencilGeneratorList[i]->GetResultFunctionName();
        ReleaseAssert(stencilToFastPathOffsetMap.count(name));
        ReleaseAssert(stencilCgInfos[i].offsetInFastPath == stencilToFastPathOffsetMap[name]);
    }

    // Generate the audit log
    //
    {
        Triple targetTriple = stencilList[0].m_triple;

        std::string fastPathAuditLog;
        if (IsDfgVariant() && !mainComponentJic->AsDfgJIT()->IsRegAllocDisabled())
        {
            ReleaseAssert(stencilCgInfos.size() == 1);
            fastPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                targetTriple, false /*isDataSection*/,
                fastPath.m_code, fastPath.m_relocMarker,
                stencilCgInfos[0].cgRes.m_fastPathRegPatches,
                mainComponentJic->AsDfgJIT()->GetRegisterPurposeContext(),
                "# " /*linePrefix*/);
        }
        else
        {
            fastPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                targetTriple, false /*isDataSection*/, fastPath.m_code, fastPath.m_relocMarker, "# " /*linePrefix*/);
        }

        std::string finalAuditLog = std::string("# Fast Path:\n") + fastPathAuditLog;

        if (slowPath.m_code.size() > 0)
        {
            std::string slowPathAuditLog;
            if (IsDfgVariant() && !mainComponentJic->AsDfgJIT()->IsRegAllocDisabled())
            {
                ReleaseAssert(stencilCgInfos.size() == 1);
                slowPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                    targetTriple, false /*isDataSection*/,
                    slowPath.m_code, slowPath.m_relocMarker,
                    stencilCgInfos[0].cgRes.m_slowPathRegPatches,
                    mainComponentJic->AsDfgJIT()->GetRegisterPurposeContext(),
                    "# " /*linePrefix*/);
            }
            else
            {
                slowPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                    targetTriple, false /*isDataSection*/, slowPath.m_code, slowPath.m_relocMarker, "# " /*linePrefix*/);
            }

            finalAuditLog += std::string("#\n# Slow Path:\n") + slowPathAuditLog;
        }

        if (dataSec.m_code.size() > 0)
        {
            std::string dataSecAuditLog = DumpStencilDisassemblyForAuditPurpose(
                targetTriple, true /*isDataSection*/, dataSec.m_code, dataSec.m_relocMarker, "# " /*linePrefix*/);

            finalAuditLog += std::string("#\n# Data Section:\n") + dataSecAuditLog;
        }

        finalAuditLog += std::string("#\n\n");
        m_disasmForAudit = finalAuditLog;
    }

    std::unique_ptr<Module> module;

    for (size_t cgiOrd = 0; cgiOrd < stencilCgInfos.size(); cgiOrd++)
    {
        StencilCgInfo& cgi = stencilCgInfos[cgiOrd];

        // Generate and link in the patch logic module
        //
        {
            std::unique_ptr<Module> part = cgi.cgRes.GenerateCodegenLogicLLVMModule(cgi.origin->GetModule());
            if (cgiOrd == 0)
            {
                ReleaseAssert(module.get() == nullptr);
                module = std::move(part);
            }
            else
            {
                ReleaseAssert(module.get() != nullptr);
                Linker linker(*module.get());
                ReleaseAssert(linker.linkInModule(std::move(part)) == false);
            }
        }

        // Locate the patch functions, rename it to avoid conflict and return it
        //
        auto findAndRenameFunction = [&](const std::string& fnName) WARN_UNUSED -> Function*
        {
            Function* fn = module->getFunction(fnName);
            ReleaseAssert(fn != nullptr);
            ReleaseAssert(fn->hasExternalLinkage());
            ReleaseAssert(!fn->empty());

            std::string newName = fnName + "_" + std::to_string(cgiOrd);
            ReleaseAssert(module->getNamedValue(newName) == nullptr);
            fn->setName(newName);
            ReleaseAssert(fn->getName() == newName);
            return fn;
        };

        cgi.fastPathPatchFn = findAndRenameFunction("deegen_do_codegen_fastpath");
        cgi.slowPathPatchFn = findAndRenameFunction("deegen_do_codegen_slowpath");
        cgi.dataSecPatchFn = findAndRenameFunction("deegen_do_codegen_datasec");
    }

    ValidateLLVMModule(module.get());

    std::string opcodeName = bytecodeDef->GetBytecodeIdName();
    ReleaseAssert(mainComponentJic->GetResultFunctionName() == "__deegen_bytecode_" + opcodeName);
    std::string mainFnName;
    if (IsDfgVariant())
    {
        // Create the codegen function implementation
        //
        mainFnName = std::string("__deegen_dfg_jit_codegen_") + opcodeName + dfgFnNameExtraSuffix;
    }
    else
    {
        // Create the main continuation-passing style codegen function
        //
        mainFnName = std::string("__deegen_baseline_jit_codegen_") + opcodeName;
    }
    m_resultFuncName = mainFnName;

    ReleaseAssert(!BytecodeOpcodeRawValueMap::IsFusedIcVariant(opcodeName));

    std::unique_ptr<JitCodegenFnProtoBase> cg;
    if (IsDfgVariant())
    {
        cg = DfgJitCodegenFnProto::Create(module.get(), mainFnName);
    }
    else
    {
        cg = BaselineJitCodegenFnProto::Create(module.get(), mainFnName);
    }
    Function* mainFn = cg->GetFunction();

    CopyFunctionAttributes(mainFn /*dst*/, stencilCgInfos[0].fastPathPatchFn /*src*/);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", mainFn);

    BasicBlock* dfgBB2 = nullptr;
    if (IsDfgVariant())
    {
        // DFG codegen logic may need to call some external functions
        // It's better to do these external calls at the end (fortunately this is correct due to our design),
        // to minimize the state we need to save across the call
        //
        // So the DFG codegen logic CFG is entryBB -> dfgBB2 where we do the external calls in dfgBB2
        //
        dfgBB2 = BasicBlock::Create(ctx, "", mainFn);
    }

    // Align the data section pointer if necessary
    //
    Value* dataSecBaseAddr = cg->EmitGetAlignedDataSectionPtr(cg->GetJitUnalignedDataSecPtr(), dataSec.m_alignment, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(dataSecBaseAddr));

    Value* fastPathBaseAddr = cg->GetJitFastPathPtr();
    Value* slowPathBaseAddr = cg->GetJitSlowPathPtr();

    // The operands of this bytecode or DFG node
    //
    std::vector<Value*> opcodeRawValues;

    if (IsDfgVariant())
    {
        // Load operand info from NodeOperandConfigData and NodeSpecificData
        //
        BytecodeVariantDefinition::DfgNsdLayout nsdLayout = bytecodeDef->ComputeDfgNsdLayout();
        size_t numLocalOperands = 0;
        bool hasSeenRangeOperand = false;
        for (auto& operand : bytecodeDef->m_list)
        {
            if (!operand->SupportsGetOperandValueFromBytecodeStruct())
            {
                opcodeRawValues.push_back(nullptr);
            }
            else
            {
                switch (operand->GetKind())
                {
                case BcOperandKind::Slot:
                {
                    // This is a SSA value, load slot from NodeOperandConfigData
                    // Note that all the Slot operands always show up first in the SSA inputs,
                    // so the input ordinal is simply numLocalOperands we have seen
                    //
                    Value* val = CreateCallToDeegenCommonSnippet(
                        module.get(),
                        "GetDfgPhysicalSlotForSSAInput",
                        { cg->AsDfgJIT()->GetNodeOperandConfigDataPtr(), CreateLLVMConstantInt<uint64_t>(ctx, numLocalOperands) },
                        entryBB);

                    ReleaseAssert(llvm_value_has_type<size_t>(val));
                    ReleaseAssert(val->getType() == operand->GetSourceValueFullRepresentationType(ctx));
                    opcodeRawValues.push_back(val);

                    numLocalOperands++;
                    break;
                }
                case BcOperandKind::Literal:
                {
                    // This is a literal value, its value should be loaded from NodeSpecificData
                    //
                    BcOpLiteral* lit = assert_cast<BcOpLiteral*>(operand.get());
                    ReleaseAssert(nsdLayout.m_operandOffsets.count(lit->OperandOrdinal()));
                    size_t nsdOffset = nsdLayout.m_operandOffsets[lit->OperandOrdinal()];

                    Value* val = lit->GetOperandValueFromStorage(
                        cg->AsDfgJIT()->GetNodeSpecificDataPtr(),
                        nsdOffset /*offsetInStorage*/,
                        lit->m_numBytes /*sizeInStorage*/,
                        entryBB);

                    ReleaseAssert(val->getType() == lit->GetSourceValueFullRepresentationType(ctx));
                    opcodeRawValues.push_back(val);
                    break;
                }
                case BcOperandKind::BytecodeRangeBase:
                {
                    // This is a ranged operand
                    // Currently DFG only supports one ranged operand in the bytecode for simplicity.
                    // Its value is directly given in the NodeOperandConfigData
                    //
                    ReleaseAssert(!hasSeenRangeOperand);
                    hasSeenRangeOperand = true;

                    Value* val = CreateCallToDeegenCommonSnippet(
                        module.get(),
                        "GetDfgNodePhysicalSlotStartForRangeOperand",
                        { cg->AsDfgJIT()->GetNodeOperandConfigDataPtr() },
                        entryBB);

                    ReleaseAssert(llvm_value_has_type<size_t>(val));
                    ReleaseAssert(val->getType() == operand->GetSourceValueFullRepresentationType(ctx));
                    opcodeRawValues.push_back(val);
                    break;
                }
                default:
                {
                    ReleaseAssert(false);
                }
                }   /*switch*/
            }
        }
    }
    else
    {
        // Decode the bytecode struct
        //
        for (auto& operand : bytecodeDef->m_list)
        {
            if (!operand->SupportsGetOperandValueFromBytecodeStruct())
            {
                opcodeRawValues.push_back(nullptr);
            }
            else
            {
                opcodeRawValues.push_back(operand->GetOperandValueFromBytecodeStruct(cg->AsBaselineJIT()->GetBytecodePtr(), entryBB));
            }
        }
    }
    ReleaseAssert(opcodeRawValues.size() == bytecodeDef->m_list.size());

    Value* outputSlot = nullptr;
    if (bytecodeDef->m_hasOutputValue)
    {
        if (IsDfgVariant())
        {
            outputSlot = CreateCallToDeegenCommonSnippet(
                module.get(),
                "GetDfgNodePhysicalSlotForOutput",
                { cg->AsDfgJIT()->GetNodeOperandConfigDataPtr() },
                entryBB);
        }
        else
        {
            outputSlot = bytecodeDef->m_outputOperand->GetOperandValueFromBytecodeStruct(cg->AsBaselineJIT()->GetBytecodePtr(), entryBB);
        }
        ReleaseAssert(llvm_value_has_type<uint64_t>(outputSlot));
    }

    auto extendTo64 = [&](Value* val, bool shouldSExt) WARN_UNUSED -> Value*
    {
        ReleaseAssert(val != nullptr);
        ReleaseAssert(val->getType()->isIntegerTy());
        ReleaseAssert(val->getType()->getIntegerBitWidth() <= 64);
        if (val->getType()->getIntegerBitWidth() == 64)
        {
            return val;
        }

        if (shouldSExt)
        {
            return new SExtInst(val, llvm_type_of<int64_t>(ctx), "", entryBB);
        }
        else
        {
            return new ZExtInst(val, llvm_type_of<int64_t>(ctx), "", entryBB);
        }
    };

    // The codegen impl function expects outputSlot (slot 100), fallthroughAddr (slot 101), slowPathDataOffset (slot 103),
    // BaselineCodeBlock32 (slot 104) and condBrDecision (slot 105) come first,
    // followed by the list of all the bytecode operand values, even if they cannot be decoded or does not exist (in which case
    // undefined shall be passed)
    //
    std::vector<Value*> bytecodeValList;

    // Slot 100 (outputSlot) comes as the first arg
    //
    if (outputSlot == nullptr)
    {
        bytecodeValList.push_back(nullptr);
    }
    else
    {
        bytecodeValList.push_back(extendTo64(outputSlot, bytecodeDef->m_outputOperand->IsSignedValue()));
    }

    // Slot 101 (fallthroughAddr) comes as the second arg
    //
    {
        GetElementPtrInst* fastPathEnd = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathBaseAddr,
                                                                           { CreateLLVMConstantInt<uint64_t>(ctx, m_fastPathCodeLen) }, "", entryBB);
        Value* fastPathEndI64 = new PtrToIntInst(fastPathEnd, llvm_type_of<uint64_t>(ctx), "", entryBB);
        bytecodeValList.push_back(fastPathEndI64);
    }

    // Then slot 103 (slowPathDataOffset)
    //
    bytecodeValList.push_back(cg->GetSlowPathDataOffset());
    // Slot 104 (BaselineCodeBlock32/DfgCodeBlock32)
    //
    bytecodeValList.push_back(cg->GetJitCodeBlock32());
    // Slot 105 is the conditionBrDecision, it is never used by baseline JIT,
    // and used by the DFG if the bytecode outputs a branch decision
    //
    Value* dfgCondBrDecisionSlot = nullptr;
    if (IsDfgVariant())
    {
        if (mainComponentJic->AsDfgJIT()->HasBranchDecisionOutput())
        {
            dfgCondBrDecisionSlot = CreateCallToDeegenCommonSnippet(
                module.get(),
                "GetDfgNodePhysicalSlotForBrDecision",
                { cg->AsDfgJIT()->GetNodeOperandConfigDataPtr() },
                entryBB);
            ReleaseAssert(llvm_value_has_type<uint64_t>(dfgCondBrDecisionSlot));
            bytecodeValList.push_back(dfgCondBrDecisionSlot);
        }
        else
        {
            bytecodeValList.push_back(nullptr);
        }
    }
    else
    {
        bytecodeValList.push_back(nullptr);
    }

    ReleaseAssert(opcodeRawValues.size() == bytecodeDef->m_list.size());
    for (size_t i = 0; i < bytecodeDef->m_list.size(); i++)
    {
        if (opcodeRawValues[i] == nullptr)
        {
            bytecodeValList.push_back(nullptr);
        }
        else
        {
            bytecodeValList.push_back(extendTo64(opcodeRawValues[i], bytecodeDef->m_list[i]->IsSignedValue()));
        }
    }

    // Assert that there is no invalid use: all the operands that does not exist should never be actually used by any of the patch functions
    // Note that the patch functions take 6 fixed arguments (dstAddr, fast/slow/ic/icData/data addr) before the bytecode operand value list
    //
    {
        auto validate = [&](size_t numGenericIcCaptures, Function* func)
        {
            constexpr size_t x_argPfxCnt = 6;
            ReleaseAssert(func->arg_size() == x_argPfxCnt + bytecodeValList.size() + numGenericIcCaptures);
            for (size_t i = 0; i < bytecodeValList.size() + numGenericIcCaptures; i++)
            {
                if (i < bytecodeValList.size() && bytecodeValList[i] != nullptr)
                {
                    continue;
                }
                Argument* arg = func->getArg(static_cast<uint32_t>(x_argPfxCnt + i));
                ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
                ReleaseAssert(arg->user_empty());
            }

            // For the main codegen function, the 'icCodeAddr' and 'icDataAddr' arg is undefined and should never be used
            //
            ReleaseAssert(func->getArg(3)->user_empty());
            ReleaseAssert(func->getArg(4)->user_empty());
        };

        for (StencilCgInfo& cgi : stencilCgInfos)
        {
            validate(cgi.origin->GetNumTotalGenericIcCaptures(), cgi.fastPathPatchFn);
            validate(cgi.origin->GetNumTotalGenericIcCaptures(), cgi.slowPathPatchFn);
            validate(cgi.origin->GetNumTotalGenericIcCaptures(), cgi.dataSecPatchFn);
        }
    }

    // After validation, change nullptr to Undef so we can pass them to the callees
    //
    for (size_t i = 0; i < bytecodeValList.size(); i++)
    {
        if (bytecodeValList[i] == nullptr)
        {
            bytecodeValList[i] = UndefValue::get(llvm_type_of<uint64_t>(ctx));
        }
    }

    for (Value* val : bytecodeValList)
    {
        ReleaseAssert(llvm_value_has_type<uint64_t>(val));
    }

    // Create logic that copies the pre-fixup bytes for each section
    //
    EmitCopyLogicForJitCodeGen(module.get(), fastPath.m_code, fastPathBaseAddr, "deegen_fastpath_prefixup_code", entryBB /*insertAtEnd*/, false /*mustBeExact*/);
    EmitCopyLogicForJitCodeGen(module.get(), slowPath.m_code, slowPathBaseAddr, "deegen_slowpath_prefixup_code", entryBB /*insertAtEnd*/, false /*mustBeExact*/);
    EmitCopyLogicForJitCodeGen(module.get(), dataSec.m_code, dataSecBaseAddr, "deegen_datasec_prefixup_code", entryBB /*insertAtEnd*/, false /*mustBeExact*/);

    // Emit calls to the patch logic
    //
    for (StencilCgInfo& cgi : stencilCgInfos)
    {
        // Compute the fast/slow/data address for this part of data
        //
        Value* fastPathAddr = GetElementPtrInst::CreateInBounds(
            llvm_type_of<uint8_t>(ctx), fastPathBaseAddr,
            { CreateLLVMConstantInt<uint64_t>(ctx, cgi.offsetInFastPath) },
            "", entryBB);

        Value* slowPathAddr = GetElementPtrInst::CreateInBounds(
            llvm_type_of<uint8_t>(ctx), slowPathBaseAddr,
            { CreateLLVMConstantInt<uint64_t>(ctx, cgi.offsetInSlowPath) },
            "", entryBB);

        Value* dataSecAddr = GetElementPtrInst::CreateInBounds(
            llvm_type_of<uint8_t>(ctx), dataSecBaseAddr,
            { CreateLLVMConstantInt<uint64_t>(ctx, cgi.offsetInDataSec) },
            "", entryBB);

        // Also cast to uint64_t because callee expects that
        //
        Value* fastPathAddrI64 = new PtrToIntInst(fastPathAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);
        Value* slowPathAddrI64 = new PtrToIntInst(slowPathAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);
        Value* dataSecAddrI64 = new PtrToIntInst(dataSecAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);

        auto emitPatchLogic = [&](Function* callee, Value* dstAddr)
        {
            ReleaseAssert(llvm_value_has_type<void*>(dstAddr));
            std::vector<Value*> args;
            args.push_back(dstAddr);
            args.push_back(fastPathAddrI64);
            args.push_back(slowPathAddrI64);
            args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));   // icCodeAddr
            args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));   // icDataSecAddr
            args.push_back(dataSecAddrI64);
            for (Value* val : bytecodeValList)
            {
                args.push_back(val);
            }
            for (size_t i = 0; i < cgi.origin->GetNumTotalGenericIcCaptures(); i++)
            {
                args.push_back(UndefValue::get(llvm_type_of<uint64_t>(ctx)));
            }

            ReleaseAssert(args.size() == callee->arg_size());
            for (size_t i = 0; i < args.size(); i++)
            {
                ReleaseAssert(args[i] != nullptr);
                ReleaseAssert(args[i]->getType() == callee->getArg(static_cast<uint32_t>(i))->getType());
            }

            ReleaseAssert(llvm_type_has_type<void>(callee->getReturnType()));
            CallInst::Create(callee, args, "", entryBB);

            ReleaseAssert(callee->hasExternalLinkage());
            ReleaseAssert(!callee->empty());
            ReleaseAssert(!callee->hasFnAttribute(Attribute::NoInline));
            callee->setLinkage(GlobalValue::InternalLinkage);
            callee->addFnAttr(Attribute::AlwaysInline);
        };

        emitPatchLogic(cgi.fastPathPatchFn, fastPathAddr);
        emitPatchLogic(cgi.slowPathPatchFn, slowPathAddr);
        emitPatchLogic(cgi.dataSecPatchFn, dataSecAddr);
    }

    // Emit register patch logic if this is for DFG and reg alloc is enabled
    //
    if (IsDfgVariant() && !mainComponentJic->AsDfgJIT()->IsRegAllocDisabled())
    {
        auto emitRegPatchLogic = [&](EncodedStencilRegPatchStream& data, Value* codePtr)
        {
            ReleaseAssert(llvm_value_has_type<void*>(codePtr));
            ReleaseAssert(data.IsValid());
            if (!data.IsEmpty())
            {
                Constant* patchData = data.EmitDataAsLLVMConstantGlobal(module.get());
                // This is an external function call, so put it at the end (dfgBB2, not entryBB)
                //
                CreateCallToDeegenCommonSnippet(
                    module.get(),
                    "ApplyDfgRuntimeRegPatchData",
                    {
                        codePtr,
                        cg->AsDfgJIT()->GetRegAllocStateForCodegenPtr(),
                        patchData
                    },
                    dfgBB2);
            }
        };

        ReleaseAssert(stencilCgInfos.size() == 1);
        StencilCgInfo& cgi = stencilCgInfos[0];
        ReleaseAssert(cgi.offsetInFastPath == 0 && cgi.offsetInSlowPath == 0);
        emitRegPatchLogic(cgi.cgRes.m_fastPathRegPatches, fastPathBaseAddr);
        emitRegPatchLogic(cgi.cgRes.m_slowPathRegPatches, slowPathBaseAddr);
    }
    else
    {
        for (StencilCgInfo& cgi : stencilCgInfos)
        {
            ReleaseAssert(!cgi.cgRes.NeedRegPatchPhase());
        }
    }

    // For baseline JIT, emit slowPathDataIndex (slowPathDataOffset and bytecodePtr32)
    //
    Value* advancedSlowPathDataIndex = nullptr;
    if (!IsDfgVariant())
    {
        Value* slowPathDataIndex = cg->AsBaselineJIT()->GetSlowPathDataIndexPtr();
        ReleaseAssert(llvm_value_has_type<void*>(slowPathDataIndex));

        Value* slowPathDataOffsetI32 = new TruncInst(cg->GetSlowPathDataOffset(), llvm_type_of<uint32_t>(ctx), "", entryBB);
        new StoreInst(slowPathDataOffsetI32, slowPathDataIndex, entryBB);

        Value* bytecodePtrI64 = new PtrToIntInst(cg->AsBaselineJIT()->GetBytecodePtr(), llvm_type_of<uint64_t>(ctx), "", entryBB);
        Value* bytecodePtrI32 = new TruncInst(bytecodePtrI64, llvm_type_of<uint32_t>(ctx), "", entryBB);

        Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint32_t>(ctx), slowPathDataIndex,
                                                        { CreateLLVMConstantInt<uint64_t>(ctx, 1) }, "", entryBB);
        new StoreInst(bytecodePtrI32, addr, entryBB);

        advancedSlowPathDataIndex = GetElementPtrInst::CreateInBounds(llvm_type_of<uint32_t>(ctx), slowPathDataIndex,
                                                                      { CreateLLVMConstantInt<uint64_t>(ctx, 2) }, "", entryBB);
    }

    // A list of locations that shall be patched based on the address of the cond br JIT code destination
    // Only useful for baseline JIT since DFG outputs condBrDecision instead
    //
    std::vector<std::pair<llvm::Value*, BaselineJitCondBrLatePatchKind>> condBrPatchList;

    // Emit slowPathData for baseline JIT or DFG JIT
    // Unfortunately the logic here is tightly coupled with how the SlowPathData is defined in BytecodeVariantDefinition,
    // because we do not want to rely on LLVM optimization to optimize out the redundant decoding of the bytecode
    // (which seems a bit fragile for me).
    //
    Value* advancedSlowPathData = nullptr;
    Instruction* advancedSlowPathDataOffset = nullptr;
    bool consumedCompactedRegConfEntry = false;
    constexpr size_t x_compactedRegConfEntrySize = dfg::DfgSlowPathRegConfigDataTraits::x_slowPathDataCompactRegConfigInfoSizeBytes;

    JitSlowPathDataLayoutBase* slowPathDataLayout = mainComponentJic->GetJitSlowPathDataLayoutBase();
    {
        // For assertion that we didn't forget to write a field
        //
        size_t totalFieldsWritten = 0;
        Value* slowPathData = cg->GetSlowPathDataPtr();

        // Write opcode (offset 0)
        //
        {
            Value* opcodeVal;
            if (IsDfgVariant())
            {
                // For DFG JIT, the opcode is the global ordinal assigned to this codegen function, which can be read from NodeOperandConfigData
                //
                opcodeVal = CreateCallToDeegenCommonSnippet(
                    module.get(),
                    "GetDfgNodeOperandConfigDataCodegenFuncOrd",
                    { cg->AsDfgJIT()->GetNodeOperandConfigDataPtr() },
                    entryBB);
            }
            else
            {
                size_t opcodeOrd = GetGBTA()->GetBytecodeOpcodeOrd(opcodeName);
                ReleaseAssert(opcodeOrd <= 65535);
                opcodeVal = CreateLLVMConstantInt<uint16_t>(ctx, SafeIntegerCast<uint16_t>(opcodeOrd));
            }

            ReleaseAssert(llvm_value_has_type<uint16_t>(opcodeVal));
            static_assert(BytecodeVariantDefinition::x_opcodeSizeBytes == 2);
            new StoreInst(opcodeVal, slowPathData, false /*isVolatile*/, Align(1), entryBB);
        }

        // Write jitAddr
        //
        slowPathDataLayout->m_jitAddr.EmitSetValueLogic(slowPathData, fastPathBaseAddr, entryBB);
        totalFieldsWritten++;

        // Emit a fixup record to fixup the SlowPathData if the bytecode can branch
        //
        if (!IsDfgVariant() && bytecodeDef->m_hasConditionalBranchTarget)
        {
            Value* addr = slowPathDataLayout->AsBaseline()->m_condBrJitAddr.EmitGetFieldAddressLogic(slowPathData, entryBB);
            condBrPatchList.push_back(std::make_pair(addr, BaselineJitCondBrLatePatchKind::SlowPathData));
            // The condBrTarget + condBrBcIndex (two fields) will be populated by late fixup logic
            //
            totalFieldsWritten += 2;
        }

        ReleaseAssert(bytecodeDef->m_list.size() == opcodeRawValues.size());
        for (size_t i = 0; i < bytecodeDef->m_list.size(); i++)
        {
            ReleaseAssertIff(bytecodeDef->m_list[i]->IsElidedFromBytecodeStruct(), opcodeRawValues[i] == nullptr);
            if (!bytecodeDef->m_list[i]->IsElidedFromBytecodeStruct())
            {
                Value* valueToWrite = opcodeRawValues[i];
                slowPathDataLayout->GetBytecodeOperand(i).EmitSetValueLogic(slowPathData, valueToWrite, entryBB);
                totalFieldsWritten++;
            }
        }

        // Write the output operand if it exists
        //
        if (bytecodeDef->m_hasOutputValue)
        {
            slowPathDataLayout->m_outputDest.EmitSetValueLogic(slowPathData, outputSlot, entryBB);
            totalFieldsWritten++;
        }

        // Initialize all the Call IC site data
        //
        if (!IsDfgVariant())
        {
            size_t numJitCallIcSites = bytecodeDef->GetNumCallICsInBaselineJitTier();
            if (numJitCallIcSites > 0)
            {
                for (size_t i = 0; i < numJitCallIcSites; i++)
                {
                    size_t offset = slowPathDataLayout->m_callICs.GetOffsetForSite(i);
                    Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                                    { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", entryBB);
                    CreateCallToDeegenCommonSnippet(module.get(), "InitializeJitCallIcSite", { addr }, entryBB);
                }
                totalFieldsWritten++;
            }
        }

        // Initialize all the Generic IC site data, and the slowpath/datasec address fields (which exist if Generic IC exists)
        //
        size_t numJitGenericIcSites = bytecodeDef->GetNumGenericICsInJitTier();
        if (numJitGenericIcSites > 0)
        {
            // Initialize the JIT slowpath address field
            //
            slowPathDataLayout->m_jitSlowPathAddr.EmitSetValueLogic(slowPathData, slowPathBaseAddr, entryBB);
            totalFieldsWritten++;

            // Initialize the JIT data section address field
            //
            slowPathDataLayout->m_jitDataSecAddr.EmitSetValueLogic(slowPathData, dataSecBaseAddr, entryBB);
            totalFieldsWritten++;

            // Initialize every IC site
            //
            for (size_t i = 0; i < numJitGenericIcSites; i++)
            {
                size_t offset = slowPathDataLayout->m_genericICs.GetOffsetForSite(i);
                Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                                { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", entryBB);
                CreateCallToDeegenCommonSnippet(module.get(), "InitializeJitGenericIcSite", { addr }, entryBB);
            }
            totalFieldsWritten++;
        }

        // Initialize condBrDecisionSlot (DFG only)
        //
        if (IsDfgVariant() && mainComponentJic->AsDfgJIT()->HasBranchDecisionOutput())
        {
            ReleaseAssert(dfgCondBrDecisionSlot != nullptr);
            JitSlowPathDataInt<uint16_t>& field = slowPathDataLayout->AsDfg()->m_condBrDecisionSlot;
            ReleaseAssert(llvm_value_has_type<uint64_t>(dfgCondBrDecisionSlot));
            Value* valI16 = new TruncInst(dfgCondBrDecisionSlot, llvm_type_of<uint16_t>(ctx), "", entryBB);
            field.EmitSetValueLogic(slowPathData, valI16, entryBB);
            totalFieldsWritten++;
        }
        else
        {
            ReleaseAssert(dfgCondBrDecisionSlot == nullptr);
        }

        // Initialize CompactRegConfig if needed (DFG only)
        //
        if (IsDfgVariant())
        {
            ReleaseAssertIff(slowPathDataLayout->AsDfg()->m_compactRegConfig.IsValid(),
                             !mainComponentJic->AsDfgJIT()->IsRegAllocDisabled() && numJitGenericIcSites > 0);
            if (slowPathDataLayout->AsDfg()->m_compactRegConfig.IsValid())
            {
                consumedCompactedRegConfEntry = true;
                Value* dstAddr = slowPathDataLayout->AsDfg()->m_compactRegConfig.EmitGetFieldAddressLogic(slowPathData, entryBB);
                Value* srcAddr = cg->AsDfgJIT()->GetCompactedRegConfPtr();

                EmitLLVMIntrinsicMemcpy<true /*forceInline*/>(
                    module.get(),
                    dstAddr,
                    srcAddr,
                    CreateLLVMConstantInt<uint64_t>(ctx, x_compactedRegConfEntrySize),
                    entryBB);
                totalFieldsWritten++;
            }
        }

        // Write the SlowPathDataOffset if needed
        //
        if (slowPathDataLayout->m_offsetFromCb.IsValid())
        {
            JitSlowPathDataInt<uint32_t>& field = slowPathDataLayout->m_offsetFromCb;
            Value* slowPathDataOffsetI32 = new TruncInst(cg->GetSlowPathDataOffset(), llvm_type_of<uint32_t>(ctx), "", entryBB);
            field.EmitSetValueLogic(slowPathData, slowPathDataOffsetI32, entryBB);
            totalFieldsWritten++;
        }

        m_slowPathDataLen = slowPathDataLayout->GetTotalLength();

        Constant* advanceOffset = CreateLLVMConstantInt<uint64_t>(ctx, m_slowPathDataLen);
        advancedSlowPathData = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                                 { advanceOffset }, "", entryBB);

        advancedSlowPathDataOffset = CreateUnsignedAddNoOverflow(cg->GetSlowPathDataOffset(), advanceOffset);
        advancedSlowPathDataOffset->insertBefore(entryBB->end());

        ReleaseAssert(totalFieldsWritten == slowPathDataLayout->GetNumValidFields());
    }

    // Emit all the condBr late fixup records (baseline JIT only)
    //
    Value* advancedCondBrPatchRecPtr = nullptr;
    if (!IsDfgVariant())
    {
        auto addRecords = [&](const std::vector<DeegenStencilCodegenResult::CondBrLatePatchRecord>& list, Value* base)
        {
            ReleaseAssert(llvm_value_has_type<void*>(base));
            for (auto& rec : list)
            {
                Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), base,
                                                                { CreateLLVMConstantInt<uint64_t>(ctx, rec.m_offset) }, "", entryBB);
                BaselineJitCondBrLatePatchKind kind = (rec.m_is64Bit ? BaselineJitCondBrLatePatchKind::Int64 : BaselineJitCondBrLatePatchKind::Int32);
                condBrPatchList.push_back(std::make_pair(addr, kind));
            }
        };

        addRecords(fastPath.m_condBrRecords, fastPathBaseAddr);
        addRecords(slowPath.m_condBrRecords, slowPathBaseAddr);
        addRecords(dataSec.m_condBrRecords, dataSecBaseAddr);

        Value* condBrPatchRecPtr = cg->AsBaselineJIT()->GetCondBrPatchRecPtr();

        if (bytecodeDef->m_hasConditionalBranchTarget)
        {
            // Compute the bytecode ptr of the cond branch
            //
            Value* condBrTargetOffset = bytecodeDef->m_condBrTarget->GetOperandValueFromBytecodeStruct(cg->AsBaselineJIT()->GetBytecodePtr(), entryBB);
            ReleaseAssert(llvm_value_has_type<int32_t>(condBrTargetOffset));
            Value* condBrTargetOffset64 = new SExtInst(condBrTargetOffset, llvm_type_of<int64_t>(ctx), "", entryBB);
            Value* bytecodePtr64 = new PtrToIntInst(cg->AsBaselineJIT()->GetBytecodePtr(), llvm_type_of<uint64_t>(ctx), "", entryBB);
            Instruction* condBrDstBytecodePtr64 = CreateAdd(bytecodePtr64, condBrTargetOffset64);
            condBrDstBytecodePtr64->insertBefore(entryBB->end());
            ReleaseAssert(llvm_value_has_type<uint64_t>(condBrDstBytecodePtr64));
            Value* condBrDstBytecodePtr32 = new TruncInst(condBrDstBytecodePtr64, llvm_type_of<uint32_t>(ctx), "", entryBB);

            for (size_t i = 0; i < condBrPatchList.size(); i++)
            {
                Value* addr = condBrPatchList[i].first;
                BaselineJitCondBrLatePatchKind kind = condBrPatchList[i].second;

                size_t offset_ptr = sizeof(BaselineJitCondBrLatePatchRecord) * i + offsetof_member_v<&BaselineJitCondBrLatePatchRecord::m_ptr>;
                size_t offset_byt = sizeof(BaselineJitCondBrLatePatchRecord) * i + offsetof_member_v<&BaselineJitCondBrLatePatchRecord::m_dstBytecodePtrLow32bits>;
                size_t offset_knd = sizeof(BaselineJitCondBrLatePatchRecord) * i + offsetof_member_v<&BaselineJitCondBrLatePatchRecord::m_patchKind>;

                Value* wptr_ptr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), condBrPatchRecPtr,
                                                                    { CreateLLVMConstantInt<uint64_t>(ctx, offset_ptr) }, "", entryBB);
                Value* wptr_byt = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), condBrPatchRecPtr,
                                                                    { CreateLLVMConstantInt<uint64_t>(ctx, offset_byt) }, "", entryBB);
                Value* wptr_knd = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), condBrPatchRecPtr,
                                                                    { CreateLLVMConstantInt<uint64_t>(ctx, offset_knd) }, "", entryBB);

                // Note that these writes are naturally-aligned, so no need to use Align(1)
                // We are breaking TBAA here, but should be fine since nothing else in the module is reading it
                //
                static_assert(sizeof(typeof_member_t<&BaselineJitCondBrLatePatchRecord::m_ptr>) == 8);
                ReleaseAssert(llvm_value_has_type<void*>(addr));
                new StoreInst(addr, wptr_ptr, entryBB);

                static_assert(sizeof(typeof_member_t<&BaselineJitCondBrLatePatchRecord::m_dstBytecodePtrLow32bits>) == 4);
                ReleaseAssert(llvm_value_has_type<uint32_t>(condBrDstBytecodePtr32));
                new StoreInst(condBrDstBytecodePtr32, wptr_byt, entryBB);

                static_assert(sizeof(typeof_member_t<&BaselineJitCondBrLatePatchRecord::m_patchKind>) == 4);
                new StoreInst(CreateLLVMConstantInt<uint32_t>(ctx, static_cast<uint32_t>(kind)), wptr_knd, entryBB);
            }

            advancedCondBrPatchRecPtr = GetElementPtrInst::CreateInBounds(
                llvm_type_of<uint8_t>(ctx), condBrPatchRecPtr,
                { CreateLLVMConstantInt<uint64_t>(ctx, sizeof(BaselineJitCondBrLatePatchRecord) * condBrPatchList.size()) }, "", entryBB);
        }
        else
        {
            ReleaseAssert(condBrPatchList.size() == 0);
            advancedCondBrPatchRecPtr = condBrPatchRecPtr;
        }

        m_numCondBrLatePatches = condBrPatchList.size();
    }

    Value* advancedJitFastPathAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathBaseAddr,
                                                                       { CreateLLVMConstantInt<uint64_t>(ctx, fastPath.m_code.size()) }, "", entryBB);
    Value* advancedJitSlowPathAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathBaseAddr,
                                                                       { CreateLLVMConstantInt<uint64_t>(ctx, slowPath.m_code.size()) }, "", entryBB);
    Value* advancedJitDataSecAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), dataSecBaseAddr,
                                                                      { CreateLLVMConstantInt<uint64_t>(ctx, dataSec.m_code.size()) }, "", entryBB);

    // For baseline JIT, create logic that dispatches to next bytecode
    // For DFG JIT, create logic that updates the main codegen state
    //
    if (!IsDfgVariant())
    {
        // For baseline JIT, create dispatch to the next codegen function
        //
        Value* advancedBytecodePtr = GetElementPtrInst::CreateInBounds(
            llvm_type_of<uint8_t>(ctx),
            cg->AsBaselineJIT()->GetBytecodePtr(),
            { CreateLLVMConstantInt<uint64_t>(ctx, bytecodeDef->GetBytecodeStructLength()) },
            "", entryBB);

        ReleaseAssert(!consumedCompactedRegConfEntry);

        Value* nextBytecodeOpcode = BytecodeVariantDefinition::DecodeBytecodeOpcode(advancedBytecodePtr, entryBB);
        ReleaseAssert(llvm_value_has_type<uint64_t>(nextBytecodeOpcode));

        Value* callee = GetBaselineJitCodegenFnDispatchTableEntry(module.get(), nextBytecodeOpcode, entryBB);
        CallInst* ci = BaselineJitCodegenFnProto::CreateDispatch(callee,
                                                                 advancedBytecodePtr,
                                                                 advancedJitFastPathAddr,
                                                                 advancedJitSlowPathAddr,
                                                                 advancedJitDataSecAddr,
                                                                 advancedSlowPathDataIndex,
                                                                 advancedSlowPathData,
                                                                 advancedCondBrPatchRecPtr,
                                                                 x_isDebugBuild ? cg->AsBaselineJIT()->GetControlStructPtr() : UndefValue::get(llvm_type_of<void*>(ctx)),
                                                                 advancedSlowPathDataOffset,
                                                                 cg->GetJitCodeBlock32());
        ci->insertBefore(entryBB->end());
        ReturnInst::Create(ctx, nullptr, entryBB);
    }
    else
    {
        // For DFG JIT, emit logic that updates the main codegen state
        //
        using PCS = dfg::PrimaryCodegenState;
        Value* pcs = cg->AsDfgJIT()->GetPrimaryCodegenStatePtr();
        WriteCppStructMember<&PCS::m_fastPathAddr>(pcs, advancedJitFastPathAddr, entryBB);
        WriteCppStructMember<&PCS::m_slowPathAddr>(pcs, advancedJitSlowPathAddr, entryBB);
        WriteCppStructMember<&PCS::m_dataSecAddr>(pcs, advancedJitDataSecAddr, entryBB);
        WriteCppStructMember<&PCS::m_slowPathDataAddr>(pcs, advancedSlowPathData, entryBB);
        WriteCppStructMember<&PCS::m_slowPathDataOffset>(pcs, advancedSlowPathDataOffset, entryBB);

        // If we have consumed a CompactRegConf entry, we need to advance the compactRegConfAddr value as well
        //
        if (consumedCompactedRegConfEntry)
        {
            Value* advancedAddr = GetElementPtrInst::CreateInBounds(
                llvm_type_of<uint8_t>(ctx),
                cg->AsDfgJIT()->GetCompactedRegConfPtr(),
                { CreateLLVMConstantInt<uint64_t>(ctx, x_compactedRegConfEntrySize) },
                "", entryBB);
            WriteCppStructMember<&PCS::m_compactedRegConfAddr>(pcs, advancedAddr, entryBB);
        }

        // Let 'entryBB' branch to 'dfgBB2', and 'dfgBB2' return
        //
        BranchInst::Create(dfgBB2, entryBB);
        ReturnInst::Create(ctx, nullptr, dfgBB2);
    }

    // At this point, the codegen logic is complete (though for the DFG JIT we need to generate a wrapper later)
    //
    ValidateLLVMModule(module.get());

    // Inline the patch logic
    //
    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);

    if (IsDfgVariant())
    {
        ReleaseAssert(module->getFunction(mainFnName + "_impl") == mainFn);
    }
    else
    {
        ReleaseAssert(module->getFunction(mainFnName) == mainFn);
    }

    // Fix up cross-reference. We need to replace reference to return continuation symbols by the corresponding JIT'ed address.
    //
    // Note that the reference must be return continuation. Reference to the main function is not possible because return
    // continuations cannot call main function, and currently we do not allow recursions in the main function.
    //
    // We have to fix up the cross-reference after inlining, because fastPathBaseAddr is only available in the main codegen logic.
    //
    {
        std::vector<std::pair<Function*, size_t /*offsetInFastPath*/>> crFixupList;
        for (StencilCgInfo& cgi : stencilCgInfos)
        {
            if (cgi.origin->IsReturnContinuation())
            {
                std::string fnName = cgi.origin->GetResultFunctionName();
                Function* func = module->getFunction(fnName);
                if (func != nullptr)
                {
                    crFixupList.push_back(std::make_pair(func, cgi.offsetInFastPath));
                }
                else
                {
                    ReleaseAssert(module->getNamedValue(fnName) == nullptr);
                }
            }
        }

        {
            std::unordered_set<Function*> chkUnique;
            for (auto& it : crFixupList)
            {
                ReleaseAssert(!chkUnique.count(it.first));
                chkUnique.insert(it.first);
            }
        }

        auto fixCrossReference = [&](Function* func, size_t offsetInFastPath)
        {
            Instruction* insPt = FindFirstNonAllocaInstInEntryBB(mainFn);
            Instruction* replacement = GetElementPtrInst::CreateInBounds(
                llvm_type_of<uint8_t>(ctx), cg->GetJitFastPathPtr(),
                { CreateLLVMConstantInt<uint64_t>(ctx, offsetInFastPath) }, "", insPt);

            DeegenStencilFixupCrossRefHelper::RunOnFunction(mainFn, func /*gvToReplace*/, replacement);

            if (!func->use_empty())
            {
                fprintf(stderr, "[ERROR] Unexpected use of '%s' that is not handled correctly, a bug?\n", func->getName().str().c_str());
                module->dump();
                abort();
            }
        };

        for (auto& it : crFixupList)
        {
            fixCrossReference(it.first, it.second);
        }

        ValidateLLVMModule(module.get());
    }

    RunLLVMDeadGlobalElimination(module.get());

    // Now, no fast path/return continuation function names should appear in the module. If not, something has gone wrong...
    //
    for (Function& func : *module.get())
    {
        std::string name = func.getName().str();
        if (name.starts_with("__deegen_bytecode_"))
        {
            ReleaseAssert(name.find("_slow_path_") != std::string::npos || name.find("_quickening_slowpath") != std::string::npos);
            ReleaseAssert(name.find("_retcont_") == std::string::npos);
        }
    }

    // For DFG, we need to create the wrapper that calls the impl function
    //
    if (IsDfgVariant())
    {
        cg->AsDfgJIT()->CreateWrapper(module.get(), m_resultFuncName);
        ValidateLLVMModule(module.get());
    }

    // Make all functions in our module dso_local, which is required to make sure we are using their PLT address
    // even if they come from a dynamic library.
    //
    // However, note that the work we did here is NOT sufficient (in fact, does not matter at all).
    // It seems like if two LLVM modules are linked together using LLVM linker, a declaration would become non-dso_local
    // if *either* module's declaration is not dso_local.
    //
    // So the final linkage phase is ultimately responsible for turning the symbols we use dso_local, and what
    // we do here have no effect. Nevertheless, we do this because our module will also be dumped as audit file,
    // and we want the audit file to accurately reflect what's actually going on.
    //
    for (Function& fn : *module.get())
    {
        fn.setDSOLocal(true);
    }

    RunLLVMOptimizePass(module.get());

    // Put the main codegen function and its accompanied data in dedicated sections
    //
    SetSectionsForCodegenModule(mainComponentJic->GetTier(), module.get(), m_resultFuncName);

    // The module should only have one function with non-empty body -- the codegen function we just generated
    //
    for (Function& func : module->functions())
    {
        if (!func.empty())
        {
            ReleaseAssert(func.getName() == m_resultFuncName);
        }
    }

    m_cgMod = std::move(module);

    // Link in the IC codegen slowpath logic if needed
    //
    if (icCodegenSlowpathModule.get() != nullptr)
    {
        Linker linker(*m_cgMod.get());
        ReleaseAssert(linker.linkInModule(std::move(icCodegenSlowpathModule)) == false);
    }

    std::vector<DfgCCallFuncInfo> dfgCCallFnNames;
    if (IsDfgVariant())
    {
        dfgCCallFnNames = mainComponentJic->AsDfgJIT()->GetFunctionNamesWrappedForRegAllocCCall();
        if (mainComponentJic->AsDfgJIT()->IsRegAllocDisabled())
        {
            ReleaseAssert(dfgCCallFnNames.size() == 0);
        }
        else
        {
            // Must have no return continuations, so the functions used by the main component is everything we need to wrap
            //
            ReleaseAssert(stencilGeneratorList.size() == 1);
        }
    }

    // For DFG, add suffix to all the IC body functions as well
    // Note that we also need to change the wrapped names correspondingly
    //
    if (IsDfgVariant())
    {
        for (std::string& icBodyFnName : genericIcInfo.m_icBodyFunctionNames)
        {
            Function* fn = m_cgMod->getFunction(icBodyFnName);
            ReleaseAssert(fn != nullptr);

            std::string newName;
            {
                size_t plen = icBodyFnName.rfind("_icbody_");
                ReleaseAssert(plen != std::string::npos);
                newName = icBodyFnName.substr(0, plen) + dfgFnNameExtraSuffix + icBodyFnName.substr(plen);
            }
            fn->setName(newName);
            ReleaseAssert(fn->getName() == newName);

            if (!mainComponentJic->AsDfgJIT()->IsRegAllocDisabled())
            {
                std::string wrapperPfx = mainComponentJic->AsDfgJIT()->GetCCallWrapperPrefix();
                std::string oldWrappedName = DfgRegAllocCCallAsmTransformPass::GetWrappedName(wrapperPfx, icBodyFnName);
                std::string newWrappedName = DfgRegAllocCCallAsmTransformPass::GetWrappedName(wrapperPfx, newName);

                fn = m_cgMod->getFunction(oldWrappedName);
                ReleaseAssert(fn != nullptr);
                fn->setName(newWrappedName);
                ReleaseAssert(fn->getName() == newWrappedName);

                bool found = false;
                for (DfgCCallFuncInfo& item : dfgCCallFnNames)
                {
                    if (item.m_fnName == icBodyFnName)
                    {
                        ReleaseAssert(!found);
                        found = true;
                        item.m_fnName = newName;
                    }
                    else if (item.m_fnName == newName)
                    {
                        ReleaseAssert(false);
                    }
                }
                ReleaseAssert(found);
            }
        }
    }

    // Rename AOT function names in the codegen module
    //
    {
        std::string prefixToFind = "__deegen_bytecode_";
        std::string prefixToReplace = IsDfgVariant() ? "__deegen_dfg_jit_op_" : "__deegen_baseline_jit_op_";
        RenameAllFunctionsStartingWithPrefix(m_cgMod.get(), prefixToFind, prefixToReplace);
    }

    // Generate the DFG C wrapper requests
    // Note that we do not immediately generate the wrappers, but only produce a list of requests,
    // since we can merge different wrappers on the same function globally into one later
    //
    if (IsDfgVariant() && !mainComponentJic->AsDfgJIT()->IsRegAllocDisabled())
    {
        std::string wrapperPrefix = mainComponentJic->AsDfgJIT()->GetCCallWrapperPrefix();

        ReleaseAssert(stencilList.size() == 1);
        // The FPU mask should consist of the FPU regs used by the JIT code as well as in any IC stub
        //
        uint64_t fpuRegMask = stencilList[0].m_usedFpuRegs;
        fpuRegMask |= genericIcInfo.m_fpuUsedMask;
        ReleaseAssert(mainComponentJic->GetAllCallIcInfo().size() == 0);

        m_dfgCWrapperRequests = GenerateCCallWrapperRequests(fpuRegMask, wrapperPrefix, dfgCCallFnNames);
    }

    m_rcJicList.clear();
}

// Generate the list of needed wrapper functions
//
std::vector<DfgRegAllocCCallWrapperRequest> WARN_UNUSED JitCodeGenLogicCreator::GenerateCCallWrapperRequests(
    uint64_t maskForAllUsedFprs,
    const std::string& wrapperPrefix,
    const std::vector<DfgCCallFuncInfo>& wrappers)
{
    std::vector<DfgRegAllocCCallWrapperRequest> res;
    for (const DfgCCallFuncInfo& item : wrappers)
    {
        res.push_back({
            .m_wrapperPrefix = wrapperPrefix,
            .m_info = item,
            .m_maskForAllUsedFprs = maskForAllUsedFprs
        });
    }
    return res;
}

// TODO: setting dedicated sections for the codegen data triggers a weird linker warning "section XXX contains incorrectly aligned strings".
//       I'm not sure what the warning is, what caused the warning, or if the warning is harmful (there's hardly any info on the Internet),
//       but for now let's avoid it.
//
void JitCodeGenLogicCreator::SetSectionForCodegenPrivateData(llvm::Module* /*cgMod*/, std::string /*secName*/)
{
#if 0
    using namespace llvm;
    for (GlobalObject& gobj : cgMod->global_objects())
    {
        GlobalVariable* gv = dyn_cast<GlobalVariable>(&gobj);
        if (gv != nullptr && gv->isConstant() && gv->hasInitializer())
        {
            if (gv->getLinkage() == GlobalValue::PrivateLinkage ||
                gv->getLinkage() == GlobalValue::InternalLinkage)
            {
                // StencilSharedConstantDataObjects are used at execution time, not codegen time.
                // Do not put them into the codegen-only data section.
                //
                if (gv->getName().str().find(StencilSharedConstantDataObject::x_varNamePrefix) == std::string::npos)
                {
                    // Do not mess up with C string constants
                    //
                    bool isCStrConstant = (gv->getLinkage() == GlobalValue::PrivateLinkage &&
                                           gv->getValueType()->isArrayTy() &&
                                           llvm_type_has_type<uint8_t>(dyn_cast<llvm::ArrayType>(gv->getValueType())->getElementType()) &&
                                           gv->getName().starts_with(".str"));
                    if (!isCStrConstant)
                    {
                        gv->setSection(secName);
                    }
                }
            }
        }
    }
#endif
}

void JitCodeGenLogicCreator::SetSectionsForCodegenModule(DeegenEngineTier tier, llvm::Module* cgMod, std::string cgFnName)
{
    using namespace llvm;
    Function* resultFunction = cgMod->getFunction(cgFnName);
    ReleaseAssert(resultFunction != nullptr);
    ReleaseAssert(!resultFunction->empty());
    std::string asmCodeSecName;
    if (tier == DeegenEngineTier::DfgJIT)
    {
        asmCodeSecName = DfgJitImplCreator::x_codegenFnSectionName;
    }
    else
    {
        ReleaseAssert(tier == DeegenEngineTier::BaselineJIT);
        asmCodeSecName = BaselineJitImplCreator::x_codegenFnSectionName;
    }
    resultFunction->setSection(asmCodeSecName);
    SetSectionForCodegenPrivateData(cgMod, asmCodeSecName + "_data_section");
}

void JitCodeGenLogicCreator::SetSectionsForIcCodegenModule(DeegenEngineTier tier, llvm::Module* cgMod)
{
    using namespace llvm;

    // Check that the codegen module should contain exactly one non-empty function
    //
    Function* cgFn = nullptr;
    for (Function& f : *cgMod)
    {
        if (!f.empty())
        {
            ReleaseAssert(cgFn == nullptr);
            cgFn = &f;
        }
    }
    ReleaseAssert(cgFn != nullptr);

    // Put the codegen function and its accompanied private data to dedicated sections
    //
    std::string asmCodeSecName;
    if (tier == DeegenEngineTier::DfgJIT)
    {
        asmCodeSecName = DfgJitImplCreator::x_icCodegenFnSectionName;
    }
    else
    {
        ReleaseAssert(tier == DeegenEngineTier::BaselineJIT);
        asmCodeSecName = BaselineJitImplCreator::x_icCodegenFnSectionName;
    }

    cgFn->setSection(asmCodeSecName);
    SetSectionForCodegenPrivateData(cgMod, asmCodeSecName + "_data_section");
}

void DeegenGenerateBaselineJitCompilerCppEntryFunction(llvm::Module* module)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    std::string fnName = "deegen_baseline_jit_do_codegen_impl";
    FunctionType* fty = FunctionType::get(llvm_type_of<void>(ctx), { llvm_type_of<void*>(ctx) }, false /*isVarArg*/);
    Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, fnName, module);
    ReleaseAssert(fn->getName() == fnName);
    fn->addFnAttr(Attribute::NoUnwind);
    fn->setDSOLocal(true);

    // Pull in some random C++ function so we can set up the correct function attributes..
    //
    {
        Function* tmp = LinkInDeegenCommonSnippet(module, "RoundPtrUpToMultipleOf");
        CopyFunctionAttributes(fn /*dst*/, tmp /*src*/);
    }

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", fn);
    Value* ctlStruct = fn->getArg(0);

    using CtlStruct = DeegenBaselineJitCodegenControlStruct;
    Value* fastPathAddr = GetMemberFromCppStruct<&CtlStruct::m_jitFastPathAddr>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(fastPathAddr));
    Value* slowPathAddr = GetMemberFromCppStruct<&CtlStruct::m_jitSlowPathAddr>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(slowPathAddr));
    Value* dataSecAddr = GetMemberFromCppStruct<&CtlStruct::m_jitDataSecAddr>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(dataSecAddr));
    Value* condBrPatchesArray = GetMemberFromCppStruct<&CtlStruct::m_condBrPatchesArray>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(condBrPatchesArray));
    Value* slowPathDataPtr = GetMemberFromCppStruct<&CtlStruct::m_slowPathDataPtr>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataPtr));
    Value* slowPathDataIndexArray = GetMemberFromCppStruct<&CtlStruct::m_slowPathDataIndexArray>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataIndexArray));
    Value* baselineCodeBlock32 = GetMemberFromCppStruct<&CtlStruct::m_baselineCodeBlock32>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<uint64_t>(baselineCodeBlock32));
    Value* initialSlowPathDataOffset = GetMemberFromCppStruct<&CtlStruct::m_initialSlowPathDataOffset>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<uint64_t>(initialSlowPathDataOffset));
    Value* bytecodeStream = GetMemberFromCppStruct<&CtlStruct::m_bytecodeStream>(ctlStruct, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(bytecodeStream));

    Value* bytecodeOpcode = BytecodeVariantDefinition::DecodeBytecodeOpcode(bytecodeStream, entryBB);
    ReleaseAssert(llvm_value_has_type<uint64_t>(bytecodeOpcode));

    Value* callee = GetBaselineJitCodegenFnDispatchTableEntry(module, bytecodeOpcode, entryBB);
    CallInst* ci = BaselineJitCodegenFnProto::CreateDispatch(callee,
                                                             bytecodeStream,
                                                             fastPathAddr,
                                                             slowPathAddr,
                                                             dataSecAddr,
                                                             slowPathDataIndexArray,
                                                             slowPathDataPtr,
                                                             condBrPatchesArray,
                                                             x_isDebugBuild ? ctlStruct : UndefValue::get(llvm_type_of<void*>(ctx)),
                                                             initialSlowPathDataOffset,
                                                             baselineCodeBlock32);
    // This call is not (and cannot be) musttail because this is a C -> GHC call, but tail is still OK
    //
    ci->setTailCallKind(CallInst::TailCallKind::TCK_Tail);
    ci->insertBefore(entryBB->end());
    ReturnInst::Create(ctx, nullptr, entryBB);

    ValidateLLVMModule(module);
}

void DeegenGenerateBaselineJitCodegenFinishFunction(llvm::Module* module)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    std::string fnName = "deegen_baseline_jit_codegen_finish";

    std::unique_ptr<BaselineJitCodegenFnProto> cg = BaselineJitCodegenFnProto::Create(module, fnName);
    Function* fn = cg->GetFunction();

    // Pull in some random C++ function so we can set up the correct function attributes..
    //
    {
        Function* tmp = LinkInDeegenCommonSnippet(module, "RoundPtrUpToMultipleOf");
        CopyFunctionAttributes(fn /*dst*/, tmp /*src*/);
    }

    BasicBlock* bb = BasicBlock::Create(ctx, "", fn);

#ifndef NDEBUG
    {
        // In debug build, populate the control struct fields for assertion
        //
        using CtlStruct = DeegenBaselineJitCodegenControlStruct;
        Value* ctlStruct = cg->GetControlStructPtr();

        WriteCppStructMember<&CtlStruct::m_actualJitFastPathEnd>(ctlStruct, cg->GetJitFastPathPtr(), bb);
        WriteCppStructMember<&CtlStruct::m_actualJitSlowPathEnd>(ctlStruct, cg->GetJitSlowPathPtr(), bb);
        WriteCppStructMember<&CtlStruct::m_actualJitDataSecEnd>(ctlStruct, cg->GetJitUnalignedDataSecPtr(), bb);
        WriteCppStructMember<&CtlStruct::m_actualCondBrPatchesArrayEnd>(ctlStruct, cg->GetCondBrPatchRecPtr(), bb);
        WriteCppStructMember<&CtlStruct::m_actualSlowPathDataEnd>(ctlStruct, cg->GetSlowPathDataPtr(), bb);
        WriteCppStructMember<&CtlStruct::m_actualSlowPathDataIndexArrayEnd>(ctlStruct, cg->GetSlowPathDataIndexPtr(), bb);
        WriteCppStructMember<&CtlStruct::m_actualBaselineCodeBlock32End>(ctlStruct, cg->GetJitCodeBlock32(), bb);
        WriteCppStructMember<&CtlStruct::m_actualSlowPathDataOffsetEnd>(ctlStruct, cg->GetSlowPathDataOffset(), bb);
        WriteCppStructMember<&CtlStruct::m_actualBytecodeStreamEnd>(ctlStruct, cg->GetBytecodePtr(), bb);
    }
#endif

    ReturnInst::Create(ctx, nullptr, bb);

    ValidateLLVMModule(module);
}

}   // namespace dast

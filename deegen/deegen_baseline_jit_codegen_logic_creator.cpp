#include "deegen_baseline_jit_codegen_logic_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "llvm/Linker/Linker.h"
#include "llvm/IR/ReplaceConstant.h"
#include "drt/baseline_jit_codegen_helper.h"
#include "deegen_ast_inline_cache.h"

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

struct BaselineJitCodegenFnProto
{
    static llvm::FunctionType* WARN_UNUSED GetFTy(llvm::LLVMContext& ctx)
    {
        using namespace llvm;
        return FunctionType::get(
            llvm_type_of<void>(ctx) /*result*/,
            {
                /*R13*/ llvm_type_of<void*>(ctx),       // Bytecode Ptr
                /*RBP*/ llvm_type_of<void*>(ctx),       // CondBrPatchRecord Ptr
                /*R12*/ llvm_type_of<void*>(ctx),       // JitDataSec Ptr
                /*RBX*/ llvm_type_of<uint64_t>(ctx),    // SlowPathDataOffset
                /*R14*/ llvm_type_of<void*>(ctx),       // SlowPathDataIndex Ptr
                /*RSI*/ llvm_type_of<uint64_t>(ctx),    // CodeBlock lower-32bits
                /*RDI*/ llvm_type_of<void*>(ctx),       // Control Struct Ptr (used in debug only, undefined in non-debug build to save a precious register)
                /*R8*/  llvm_type_of<void*>(ctx),       // JitFastPath Ptr
                /*R9*/  llvm_type_of<void*>(ctx),       // JitSlowPath Ptr
                /*R15*/ llvm_type_of<void*>(ctx),       // SlowPathData Ptr
                /*XMM1-6, unused*/
                llvm_type_of<double>(ctx),
                llvm_type_of<double>(ctx),
                llvm_type_of<double>(ctx),
                llvm_type_of<double>(ctx),
                llvm_type_of<double>(ctx),
                llvm_type_of<double>(ctx)
            } /*params*/,
            false /*isVarArg*/);
    }

    static BaselineJitCodegenFnProto WARN_UNUSED Create(llvm::Module* module, const std::string& name)
    {
        using namespace llvm;
        FunctionType* fty = GetFTy(module->getContext());
        ReleaseAssert(module->getNamedValue(name) == nullptr);
        Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, name, module);
        ReleaseAssert(fn->getName() == name);
        BaselineJitCodegenFnProto r = { .m_func = fn };
        r.GetBytecodePtr()->setName("bytecodePtr");
        r.GetCondBrPatchRecPtr()->setName("condBrPatchRecPtr");
        r.GetJitUnalignedDataSecPtr()->setName("jitPreAlignedDataSecPtr");
        r.GetControlStructPtr()->setName("ctlStructPtr");
        r.GetSlowPathDataIndexPtr()->setName("slowPathDataIndexPtr");
        r.GetJitFastPathPtr()->setName("jitFastPathPtr");
        r.GetJitSlowPathPtr()->setName("jitSlowPathPtr");
        r.GetSlowPathDataPtr()->setName("slowPathDataPtr");
        r.GetSlowPathDataOffset()->setName("slowPathDataOffset");
        r.GetCodeBlock32()->setName("codeBlock32");

        // By design, all the pointers are known to operate on non-overlapping ranges
        // and exclusively owning the respective ranges, so add noalias to help optimization
        //
        fn->addParamAttr(x_bytecodePtr, Attribute::NoAlias);
        fn->addParamAttr(x_condBrPatchRecordPtr, Attribute::NoAlias);
        fn->addParamAttr(x_jitUnalignedDataSecPtr, Attribute::NoAlias);
        fn->addParamAttr(x_controlStructPtr, Attribute::NoAlias);
        fn->addParamAttr(x_slowPathDataIndexPtr, Attribute::NoAlias);
        fn->addParamAttr(x_jitFastPathPtr, Attribute::NoAlias);
        fn->addParamAttr(x_jitSlowPathPtr, Attribute::NoAlias);
        fn->addParamAttr(x_slowPathDataPtr, Attribute::NoAlias);

        fn->setCallingConv(CallingConv::GHC);
        fn->setDSOLocal(true);

        return r;
    }

    static llvm::CallInst* WARN_UNUSED CreateDispatch(llvm::Value* callee,
                                                      llvm::Value* bytecodePtr,
                                                      llvm::Value* jitFastPathPtr,
                                                      llvm::Value* jitSlowPathPtr,
                                                      llvm::Value* jitDataSecPtr,
                                                      llvm::Value* slowPathDataIndexPtr,
                                                      llvm::Value* slowPathDataPtr,
                                                      llvm::Value* condBrPatchRecPtr,
                                                      llvm::Value* ctlStructPtr,
                                                      llvm::Value* slowPathDataOffset,
                                                      llvm::Value* codeBlock32)
    {
        using namespace llvm;
        ReleaseAssert(callee != nullptr);
        ReleaseAssert(bytecodePtr != nullptr);
        ReleaseAssert(jitFastPathPtr != nullptr);
        ReleaseAssert(jitSlowPathPtr != nullptr);
        ReleaseAssert(jitDataSecPtr != nullptr);
        ReleaseAssert(slowPathDataIndexPtr != nullptr);
        ReleaseAssert(slowPathDataPtr != nullptr);
        ReleaseAssert(condBrPatchRecPtr != nullptr);
        ReleaseAssert(ctlStructPtr != nullptr);
        ReleaseAssert(slowPathDataOffset != nullptr);
        ReleaseAssert(codeBlock32 != nullptr);

        LLVMContext& ctx = callee->getContext();
        // In non-debug build, ctlStructPtr should be undefined
        ReleaseAssertIff(!x_isDebugBuild, isa<UndefValue>(ctlStructPtr));
        CallInst* ci = CallInst::Create(
            GetFTy(ctx),
            callee,
            {
                bytecodePtr,
                condBrPatchRecPtr,
                jitDataSecPtr,
                slowPathDataOffset,
                slowPathDataIndexPtr,
                codeBlock32,
                ctlStructPtr,
                jitFastPathPtr,
                jitSlowPathPtr,
                slowPathDataPtr,
                llvm::UndefValue::get(llvm_type_of<double>(ctx)),
                llvm::UndefValue::get(llvm_type_of<double>(ctx)),
                llvm::UndefValue::get(llvm_type_of<double>(ctx)),
                llvm::UndefValue::get(llvm_type_of<double>(ctx)),
                llvm::UndefValue::get(llvm_type_of<double>(ctx)),
                llvm::UndefValue::get(llvm_type_of<double>(ctx))
            });
        ci->setCallingConv(CallingConv::GHC);
        ci->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
        return ci;
    }

    llvm::Function* WARN_UNUSED GetFunction() { return m_func; }
    llvm::Value* WARN_UNUSED GetBytecodePtr() { return m_func->getArg(x_bytecodePtr); }
    llvm::Value* WARN_UNUSED GetCondBrPatchRecPtr() { return m_func->getArg(x_condBrPatchRecordPtr); }
    llvm::Value* WARN_UNUSED GetJitUnalignedDataSecPtr() { return m_func->getArg(x_jitUnalignedDataSecPtr); }
    llvm::Value* WARN_UNUSED GetControlStructPtr() { return m_func->getArg(x_controlStructPtr); }
    llvm::Value* WARN_UNUSED GetSlowPathDataIndexPtr() { return m_func->getArg(x_slowPathDataIndexPtr); }
    llvm::Value* WARN_UNUSED GetJitFastPathPtr() { return m_func->getArg(x_jitFastPathPtr); }
    llvm::Value* WARN_UNUSED GetJitSlowPathPtr() { return m_func->getArg(x_jitSlowPathPtr); }
    llvm::Value* WARN_UNUSED GetSlowPathDataPtr() { return m_func->getArg(x_slowPathDataPtr); }
    llvm::Value* WARN_UNUSED GetSlowPathDataOffset() { return m_func->getArg(x_slowPathDataOffset); }
    llvm::Value* WARN_UNUSED GetCodeBlock32() { return m_func->getArg(x_codeBlock32); }

    llvm::Function* m_func;

    static constexpr uint32_t x_bytecodePtr = 0;
    static constexpr uint32_t x_condBrPatchRecordPtr = 1;
    static constexpr uint32_t x_jitUnalignedDataSecPtr = 2;
    static constexpr uint32_t x_slowPathDataOffset = 3;
    static constexpr uint32_t x_slowPathDataIndexPtr = 4;
    static constexpr uint32_t x_codeBlock32 = 5;
    static constexpr uint32_t x_controlStructPtr = 6;
    static constexpr uint32_t x_jitFastPathPtr = 7;
    static constexpr uint32_t x_jitSlowPathPtr = 8;
    static constexpr uint32_t x_slowPathDataPtr = 9;
};

DeegenBytecodeBaselineJitInfo WARN_UNUSED DeegenBytecodeBaselineJitInfo::Create(BytecodeIrInfo& bii, const BytecodeOpcodeRawValueMap& byToOpcodeMap)
{
    using namespace llvm;
    ReleaseAssert(bii.m_jitMainComponent.get() != nullptr);

    LLVMContext& ctx = bii.m_jitMainComponent->m_module->getContext();

    DeegenBytecodeBaselineJitInfo res;

    // TODO: implement
    //
    AstInlineCache::TriviallyLowerAllInlineCaches(bii.m_jitMainComponent->m_impl);

    BaselineJitImplCreator mainJic(*bii.m_jitMainComponent.get());
    mainJic.DoLowering();
    res.m_implModulesForAudit.push_back(std::make_pair(CloneModule(*mainJic.GetModule()), mainJic.GetResultFunctionName()));

    BytecodeVariantDefinition* bytecodeDef = mainJic.GetBytecodeDef();

    std::vector<std::unique_ptr<BaselineJitImplCreator>> rcJicList;
    for (auto& rc : bii.m_allRetConts)
    {
        if (!rc->IsReturnContinuationUsedBySlowPathOnly())
        {
            rcJicList.push_back(std::unique_ptr<BaselineJitImplCreator>(new BaselineJitImplCreator(*rc.get())));
        }
    }
    for (auto& rcJic : rcJicList)
    {
        rcJic->DoLowering();
        res.m_implModulesForAudit.push_back(std::make_pair(CloneModule(*rcJic->GetModule()), rcJic->GetResultFunctionName()));
    }

    // For now, stay simple and always layout all the return continuations in the order given, and let the last
    // return continuation fallthrough to the next bytecode.
    // This is good enough, because almost all bytecodes have only 1 possible (non-slowpath) return continuation
    //
    std::vector<BaselineJitImplCreator*> stencilList;
    stencilList.push_back(&mainJic);
    for (auto& rcJic : rcJicList)
    {
        stencilList.push_back(rcJic.get());
    }

    struct StencilCgInfo
    {
        BaselineJitImplCreator* origin;
        DeegenStencilCodegenResult cgRes;
        size_t offsetInFastPath;
        size_t offsetInSlowPath;
        size_t offsetInDataSec;
        Function* fastPathPatchFn;
        Function* slowPathPatchFn;
        Function* dataSecPatchFn;
    };

    std::vector<StencilCgInfo> stencilCgInfos;
    for (size_t i = 0; i < stencilList.size(); i++)
    {
        // Only the last stencil may eliminate the tail jmp to fallthrough
        //
        bool mayEliminateJmpToFallthrough = (i + 1 == stencilList.size());

        BaselineJitImplCreator* jic = stencilList[i];
        DeegenStencilCodegenResult cgResult = jic->GetStencil().PrintCodegenFunctions(
            mayEliminateJmpToFallthrough,
            bii.m_bytecodeDef->m_list.size() /*numBytecodeOperands*/,
            jic->GetStencilRcDefinitions() /*placeholderDefs*/);

        stencilCgInfos.push_back(StencilCgInfo { .origin = jic, .cgRes = cgResult });
    }

    MergedStencilSection fastPath, slowPath, dataSec;
    for (StencilCgInfo& cgi : stencilCgInfos)
    {
        cgi.offsetInFastPath = fastPath.Add(1 /*alignment*/, cgi.cgRes.m_fastPathPreFixupCode, cgi.cgRes.m_fastPathRelocMarker, cgi.cgRes.m_condBrFixupOffsetsInFastPath);
        cgi.offsetInSlowPath = slowPath.Add(1 /*alignment*/, cgi.cgRes.m_slowPathPreFixupCode, cgi.cgRes.m_slowPathRelocMarker, cgi.cgRes.m_condBrFixupOffsetsInSlowPath);
        cgi.offsetInDataSec = dataSec.Add(cgi.cgRes.m_dataSecAlignment, cgi.cgRes.m_dataSecPreFixupCode, cgi.cgRes.m_dataSecRelocMarker, cgi.cgRes.m_condBrFixupOffsetsInDataSec);
    }

    res.m_fastPathCodeLen = fastPath.m_code.size();
    res.m_slowPathCodeLen = slowPath.m_code.size();
    res.m_dataSectionCodeLen = dataSec.m_code.size();
    res.m_dataSectionAlignment = dataSec.m_alignment;

    {
        Triple targetTriple = mainJic.GetStencil().m_triple;

        std::string fastPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
            targetTriple, false /*isDataSection*/, fastPath.m_code, fastPath.m_relocMarker, "# " /*linePrefix*/);

        std::string finalAuditLog = std::string("# Fast Path:\n") + fastPathAuditLog;

        if (slowPath.m_code.size() > 0)
        {
            std::string slowPathAuditLog = DumpStencilDisassemblyForAuditPurpose(
                targetTriple, false /*isDataSection*/, slowPath.m_code, slowPath.m_relocMarker, "# " /*linePrefix*/);

            finalAuditLog += std::string("#\n# Slow Path:\n") + slowPathAuditLog;
        }

        if (dataSec.m_code.size() > 0)
        {
            std::string dataSecAuditLog = DumpStencilDisassemblyForAuditPurpose(
                targetTriple, true /*isDataSection*/, dataSec.m_code, dataSec.m_relocMarker, "# " /*linePrefix*/);

            finalAuditLog += std::string("#\n# Data Section:\n") + dataSecAuditLog;
        }

        finalAuditLog += std::string("#\n\n");
        res.m_disasmForAudit = finalAuditLog;
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

    // Create the main continuation-passing style codegen function
    //
    std::string opcodeName = bytecodeDef->GetBytecodeIdName();
    ReleaseAssert(mainJic.GetResultFunctionName() == "__deegen_bytecode_" + opcodeName);
    std::string mainFnName = std::string("__deegen_baseline_jit_codegen_") + opcodeName;
    res.m_resultFuncName = mainFnName;

    ReleaseAssert(!byToOpcodeMap.IsFusedIcVariant(opcodeName));

    BaselineJitCodegenFnProto cg = BaselineJitCodegenFnProto::Create(module.get(), mainFnName);
    Function* mainFn = cg.GetFunction();

    CopyFunctionAttributes(mainFn /*dst*/, stencilCgInfos[0].fastPathPatchFn /*src*/);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", mainFn);

    // Decode the bytecode struct
    //
    std::vector<Value*> opcodeRawValues;

    for (auto& operand : bytecodeDef->m_list)
    {
        if (!operand->SupportsGetOperandValueFromBytecodeStruct())
        {
            opcodeRawValues.push_back(nullptr);
        }
        else
        {
            opcodeRawValues.push_back(operand->GetOperandValueFromBytecodeStruct(cg.GetBytecodePtr(), entryBB));
        }
    }

    Value* outputSlot = nullptr;
    if (bytecodeDef->m_hasOutputValue)
    {
        outputSlot = bytecodeDef->m_outputOperand->GetOperandValueFromBytecodeStruct(cg.GetBytecodePtr(), entryBB);
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

    // The codegen impl function expects outputSlot (slot 100), slowPathDataOffset (slot 103) and CodeBlock32 (slot 104) come first,
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

    // Then slot 103 (slowPathDataOffset) and slot 104 (CodeBlock32)
    //
    bytecodeValList.push_back(cg.GetSlowPathDataOffset());
    bytecodeValList.push_back(cg.GetCodeBlock32());

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
    // Note that the patch functions take 4 fixed arguments (dstAddr, fast/slow/data addr) before the bytecode operand value list
    //
    {
        auto validate = [&](Function* func)
        {
            constexpr size_t x_argPfxCnt = 4;
            ReleaseAssert(func->arg_size() == x_argPfxCnt + bytecodeValList.size());
            for (size_t i = 0; i < bytecodeValList.size(); i++)
            {
                if (bytecodeValList[i] != nullptr)
                {
                    continue;
                }
                Argument* arg = func->getArg(static_cast<uint32_t>(x_argPfxCnt + i));
                ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
                ReleaseAssert(arg->user_empty());
            }
        };

        for (StencilCgInfo& cgi : stencilCgInfos)
        {
            validate(cgi.fastPathPatchFn);
            validate(cgi.slowPathPatchFn);
            validate(cgi.dataSecPatchFn);
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

    // Align the data section pointer if necessary
    //
    Value* dataSecBaseAddr = nullptr;
    {
        Value* preAlignedDataSecPtr = cg.GetJitUnalignedDataSecPtr();
        size_t alignment = dataSec.m_alignment;
        ReleaseAssert(alignment > 0 && is_power_of_2(alignment));
        if (alignment == 1)
        {
            dataSecBaseAddr = preAlignedDataSecPtr;
        }
        else
        {
            dataSecBaseAddr = CreateCallToDeegenCommonSnippet(
                module.get(),
                "RoundPtrUpToMultipleOf",
                { preAlignedDataSecPtr, CreateLLVMConstantInt<uint64_t>(ctx, alignment) },
                entryBB);
        }
    }
    ReleaseAssert(llvm_value_has_type<void*>(dataSecBaseAddr));

    Value* fastPathBaseAddr = cg.GetJitFastPathPtr();
    Value* slowPathBaseAddr = cg.GetJitSlowPathPtr();

    // Create logic that copies the pre-fixup bytes for each section
    // Note that we may over-copy at most 7 bytes without worrying about clobbering
    // anything: the allocation logic has already accounted for that.
    //
    auto emitCopyLogic = [&](const std::vector<uint8_t>& bytes, llvm::Value* dst, const std::string& dbgName)
    {
        ReleaseAssert(llvm_value_has_type<void*>(dst));
        size_t roundedLen = RoundUpToMultipleOf<8>(bytes.size());
        std::vector<Constant*> arr;
        for (size_t i = 0; i < roundedLen; i++)
        {
            uint8_t value;
            if (i < bytes.size())
            {
                value = bytes[i];
            }
            else
            {
                value = 0;
            }

            arr.push_back(CreateLLVMConstantInt<uint8_t>(ctx, value));
        }

        ArrayType* aty = ArrayType::get(llvm_type_of<uint8_t>(ctx), roundedLen);
        Constant* ca = ConstantArray::get(aty, arr);

        ReleaseAssert(module->getNamedValue(dbgName) == nullptr);
        GlobalVariable* gv = new GlobalVariable(*module.get(), aty, true /*isConstant*/, GlobalValue::PrivateLinkage, ca /*initializer*/, dbgName);
        ReleaseAssert(gv->getName() == dbgName);
        gv->setAlignment(Align(16));

        // TODO: LLVM lowers memcpy.inline to 'rep movsb' if the buffer is too long (>128 bytes?),
        // which has disastrous performance on older CPUs without FSRM, but (should?) work well on Golden Cove
        // or later (but really? our output buffer is not aligned..)
        // Think about if we need to do anything to this later..
        //
        EmitLLVMIntrinsicMemcpy<true /*forceInline*/>(module.get(), dst, gv, CreateLLVMConstantInt<uint64_t>(ctx, roundedLen), entryBB);
    };

    emitCopyLogic(fastPath.m_code, fastPathBaseAddr, "deegen_fastpath_prefixup_code");
    emitCopyLogic(slowPath.m_code, slowPathBaseAddr, "deegen_slowpath_prefixup_code");
    emitCopyLogic(dataSec.m_code, dataSecBaseAddr, "deegen_datasec_prefixup_code");

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
            args.push_back(dataSecAddrI64);
            for (Value* val : bytecodeValList)
            {
                args.push_back(val);
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

    // Emit slowPathDataIndex
    // Note that we simply truncate bytecodePtr to i32 and store, and fixup the undone work of subtracting
    // the base address in the end.
    // This is to avoid spending one precious register passing the base address of bytecodePtr around all the time.
    //
    // However for SlowPathData we already maintain SlowPathDataOffset, so we can store directly
    //
    Value* advancedSlowPathDataIndex = nullptr;
    {
        Value* slowPathDataIndex = cg.GetSlowPathDataIndexPtr();
        ReleaseAssert(llvm_value_has_type<void*>(slowPathDataIndex));

        Value* slowPathDataOffsetI32 = new TruncInst(cg.GetSlowPathDataOffset(), llvm_type_of<uint32_t>(ctx), "", entryBB);
        new StoreInst(slowPathDataOffsetI32, slowPathDataIndex, entryBB);

        Value* bytecodePtrI64 = new PtrToIntInst(cg.GetBytecodePtr(), llvm_type_of<uint64_t>(ctx), "", entryBB);
        Value* bytecodePtrI32 = new TruncInst(bytecodePtrI64, llvm_type_of<uint32_t>(ctx), "", entryBB);

        Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint32_t>(ctx), slowPathDataIndex,
                                                        { CreateLLVMConstantInt<uint64_t>(ctx, 1) }, "", entryBB);
        new StoreInst(bytecodePtrI32, addr, entryBB);

        advancedSlowPathDataIndex = GetElementPtrInst::CreateInBounds(llvm_type_of<uint32_t>(ctx), slowPathDataIndex,
                                                                      { CreateLLVMConstantInt<uint64_t>(ctx, 2) }, "", entryBB);
    }

    // A list of locations that shall be patched based on the address of the cond br JIT code destination
    //
    std::vector<std::pair<llvm::Value*, BaselineJitCondBrLatePatchKind>> condBrPatchList;

    // Emit slowPathData
    // Unfortunately the logic here is tightly coupled with how the SlowPathData is defined in BytecodeVariantDefinition,
    // because we do not want to rely on LLVM optimization to optimize out the redundant decoding of the bytecode
    // (which seems a bit fragile for me).
    //
    Value* advancedSlowPathData = nullptr;
    Instruction* advancedSlowPathDataOffset = nullptr;
    {
        Value* slowPathData = cg.GetSlowPathDataPtr();

        // Write opcode (offset 0)
        //
        size_t opcodeOrd = byToOpcodeMap.GetOpcode(opcodeName);
        ReleaseAssert(opcodeOrd <= 65535);
        static_assert(BytecodeVariantDefinition::x_opcodeSizeBytes == 2);
        new StoreInst(CreateLLVMConstantInt<uint16_t>(ctx, SafeIntegerCast<uint16_t>(opcodeOrd)), slowPathData, false /*isVolatile*/, Align(1), entryBB);

        // Write jitAddr (offset x_opcodeSizeBytes)
        //
        {
            Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                            { CreateLLVMConstantInt<uint64_t>(ctx, BytecodeVariantDefinition::x_opcodeSizeBytes) }, "", entryBB);

            Value* val64 = new PtrToIntInst(fastPathBaseAddr, llvm_type_of<uint64_t>(ctx), "", entryBB);
            Value* val32 = new TruncInst(val64, llvm_type_of<uint32_t>(ctx), "", entryBB);
            new StoreInst(val32, addr, false /*isVolatile*/, Align(1), entryBB);
        }

        // Emit a fixup record to fixup the SlowPathData if the bytecode can branch
        //
        if (bytecodeDef->m_hasConditionalBranchTarget)
        {
            Value* addr = bytecodeDef->GetAddressOfCondBrOperandForBaselineJit(slowPathData, entryBB);
            condBrPatchList.push_back(std::make_pair(addr, BaselineJitCondBrLatePatchKind::SlowPathData));
        }

        // Write the bytecode operands
        //
        auto writeOperand = [&](BcOperand* operand, Value* decodedValue)
        {
            ReleaseAssert(decodedValue != nullptr);

            size_t offset = operand->GetOffsetInBaselineJitSlowPathDataStruct();
            size_t size = operand->GetSizeInBaselineJitSlowPathDataStruct();
            ReleaseAssert(offset != static_cast<size_t>(-1) && size > 0);

            Value* src = decodedValue;
            ReleaseAssert(src->getType()->isIntegerTy());
            size_t srcBitWidth = src->getType()->getIntegerBitWidth();
            ReleaseAssert(srcBitWidth >= size * 8);

            Type* dstTy = Type::getIntNTy(ctx, static_cast<unsigned int>(size * 8));
            Value* dst;
            if (srcBitWidth == size * 8)
            {
                dst = src;
            }
            else
            {
                dst = new TruncInst(src, dstTy, "", entryBB);
            }
            ReleaseAssert(dst->getType() == dstTy);

            Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                            { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", entryBB);
            new StoreInst(dst, addr, false /*isVolatile*/, Align(1), entryBB);
        };

        ReleaseAssert(bytecodeDef->m_list.size() == opcodeRawValues.size());
        for (size_t i = 0; i < bytecodeDef->m_list.size(); i++)
        {
            ReleaseAssertIff(bytecodeDef->m_list[i]->IsElidedFromBytecodeStruct(), opcodeRawValues[i] == nullptr);
            if (!bytecodeDef->m_list[i]->IsElidedFromBytecodeStruct())
            {
                writeOperand(bytecodeDef->m_list[i].get(), opcodeRawValues[i]);
            }
        }

        // Write the output operand if it exists
        //
        if (bytecodeDef->m_hasOutputValue)
        {
            writeOperand(bytecodeDef->m_outputOperand.get(), outputSlot);
        }

        Constant* advanceOffset = CreateLLVMConstantInt<uint64_t>(ctx, bytecodeDef->GetBaselineJitSlowPathDataLength());
        advancedSlowPathData = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathData,
                                                                 { advanceOffset }, "", entryBB);

        advancedSlowPathDataOffset = CreateUnsignedAddNoOverflow(cg.GetSlowPathDataOffset(), advanceOffset);
        entryBB->getInstList().push_back(advancedSlowPathDataOffset);

        res.m_slowPathDataLen = bytecodeDef->GetBaselineJitSlowPathDataLength();
    }

    // Emit all the condBr late fixup records
    //
    Value* advancedCondBrPatchRecPtr = nullptr;
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

        Value* condBrPatchRecPtr = cg.GetCondBrPatchRecPtr();

        if (bytecodeDef->m_hasConditionalBranchTarget)
        {
            // Compute the bytecode ptr of the cond branch
            //
            Value* condBrTargetOffset = bytecodeDef->m_condBrTarget->GetOperandValueFromBytecodeStruct(cg.GetBytecodePtr(), entryBB);
            ReleaseAssert(llvm_value_has_type<int32_t>(condBrTargetOffset));
            Value* condBrTargetOffset64 = new SExtInst(condBrTargetOffset, llvm_type_of<int64_t>(ctx), "", entryBB);
            Value* bytecodePtr64 = new PtrToIntInst(cg.GetBytecodePtr(), llvm_type_of<uint64_t>(ctx), "", entryBB);
            Instruction* condBrDstBytecodePtr64 = CreateAdd(bytecodePtr64, condBrTargetOffset64);
            entryBB->getInstList().push_back(condBrDstBytecodePtr64);
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

        res.m_numCondBrLatePatches = condBrPatchList.size();
    }

    // Create logic that dispatches to next bytecode
    //
    Value* advancedJitFastPathAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), fastPathBaseAddr,
                                                                       { CreateLLVMConstantInt<uint64_t>(ctx, fastPath.m_code.size()) }, "", entryBB);
    Value* advancedJitSlowPathAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathBaseAddr,
                                                                       { CreateLLVMConstantInt<uint64_t>(ctx, slowPath.m_code.size()) }, "", entryBB);
    Value* advancedJitDataSecAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), dataSecBaseAddr,
                                                                      { CreateLLVMConstantInt<uint64_t>(ctx, dataSec.m_code.size()) }, "", entryBB);

    Value* advancedBytecodePtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), cg.GetBytecodePtr(),
                                                                   { CreateLLVMConstantInt<uint64_t>(ctx, bytecodeDef->GetBytecodeStructLength()) }, "", entryBB);

    // Create the dispatch table global declaration
    //
    constexpr const char* x_dispatchTableSymbolName = "__deegen_baseline_jit_codegen_dispatch_table";

    llvm::ArrayType* dispatchTableTy = llvm::ArrayType::get(llvm_type_of<void*>(ctx), 0 /*numElements*/);
    ReleaseAssert(module->getNamedValue(x_dispatchTableSymbolName) == nullptr);
    GlobalVariable* dispatchTable = new GlobalVariable(*module,
                                                       dispatchTableTy /*valueType*/,
                                                       true /*isConstant*/,
                                                       GlobalValue::ExternalLinkage,
                                                       nullptr /*initializer*/,
                                                       x_dispatchTableSymbolName /*name*/);
    ReleaseAssert(dispatchTable->getName().str() == x_dispatchTableSymbolName);
    dispatchTable->setAlignment(MaybeAlign(8));
    dispatchTable->setDSOLocal(true);

    Value* callee = nullptr;
    {
        Value* nextBytecodeOpcode = BytecodeVariantDefinition::DecodeBytecodeOpcode(advancedBytecodePtr, entryBB);
        ReleaseAssert(llvm_value_has_type<uint64_t>(nextBytecodeOpcode));

        Value* addr = GetElementPtrInst::CreateInBounds(
            dispatchTableTy /*pointeeType*/, dispatchTable,
            { CreateLLVMConstantInt<uint64_t>(ctx, 0), nextBytecodeOpcode }, "", entryBB);

        callee = new LoadInst(llvm_type_of<void*>(ctx), addr, "", false /*isVolatile*/, Align(8), entryBB);
    }

    CallInst* ci = BaselineJitCodegenFnProto::CreateDispatch(callee,
                                                             advancedBytecodePtr,
                                                             advancedJitFastPathAddr,
                                                             advancedJitSlowPathAddr,
                                                             advancedJitDataSecAddr,
                                                             advancedSlowPathDataIndex,
                                                             advancedSlowPathData,
                                                             advancedCondBrPatchRecPtr,
                                                             x_isDebugBuild ? cg.GetControlStructPtr() : UndefValue::get(llvm_type_of<void*>(ctx)),
                                                             advancedSlowPathDataOffset,
                                                             cg.GetCodeBlock32());
    entryBB->getInstList().push_back(ci);
    ReturnInst::Create(ctx, nullptr, entryBB);

    ValidateLLVMModule(module.get());

    // Inline the patch logic
    //
    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);

    ReleaseAssert(module->getFunction(mainFnName) == mainFn);

    // Fix up cross-reference. We need to replace reference to return continuation symbols by the corresponding JIT'ed address.
    //
    // Note that the reference must be return continuation. Reference to the main function is not possible because return
    // continuations cannot call main function, and currently we do not allow recursions in the main function.
    //
    // We have to fix up the cross-reference after inlining, because fastPathBaseAddr is only available in the main codegen logic.
    //
    // Conceptually what we are trying to do is pretty simple: replace all use of 'return_cont_fn_name' with 'fastPathBaseAddr + offsetInFastPath'.
    // However, due to the complexity and constraints of LLVM Constant/ConstantExpr, the logic is not as simple as it seems...
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

        // Find the first non-alloca instruction, which is the earliest safe place to insert new instructions
        //
        auto findFirstInsertionPt = [&](Function* func) WARN_UNUSED -> Instruction*
        {
            ReleaseAssert(!func->empty());
            auto it = func->getEntryBlock().getFirstInsertionPt();
            while (it != func->getEntryBlock().end())
            {
                Instruction& inst = *it;
                if (!isa<AllocaInst>(&inst))
                {
                    return &inst;
                }
                it++;
            }
            ReleaseAssert(false);
        };

        auto fixCrossReference = [&](Function* func, size_t offsetInFastPath)
        {
            // Figure out all ConstantExpr users that uses 'func'
            //
            std::unordered_set<Constant*> cstUsers;
            cstUsers.insert(func);

            {
                std::queue<Constant*> worklist;
                worklist.push(func);
                while (!worklist.empty())
                {
                    Constant* cst = worklist.front();
                    worklist.pop();
                    for (User* u : cst->users())
                    {
                        if (isa<Constant>(u))
                        {
                            if (isa<ConstantExpr>(u))
                            {
                                ConstantExpr* ce = cast<ConstantExpr>(u);
                                if (!cstUsers.count(ce))
                                {
                                    cstUsers.insert(ce);
                                    worklist.push(ce);
                                }
                            }
                            else
                            {
                                // This shouldn't happen for our module..
                                //
                                fprintf(stderr, "[ERROR] Unexpected non-ConstantExpr use of constant, a bug?\n");
                                cst->dump();
                                module->dump();
                                abort();
                            }
                        }
                    }
                }
            }

            // Replace every use of constant in 'cstUsers'
            //
            std::unordered_map<Constant*, Instruction*> replacementMap;
            Instruction* insPt = findFirstInsertionPt(mainFn);

            {
                Instruction* replacement = GetElementPtrInst::CreateInBounds(
                    llvm_type_of<uint8_t>(ctx), cg.GetJitFastPathPtr(),
                    { CreateLLVMConstantInt<uint64_t>(ctx, offsetInFastPath) }, "", insPt);

                replacementMap[func] = replacement;
            }

            std::function<Instruction*(Constant*, Instruction*)> handleConstant = [&](Constant* cst, Instruction* insertBefore) WARN_UNUSED -> Instruction*
            {
                if (replacementMap.count(cst))
                {
                    Instruction* inst = replacementMap[cst];
                    ReleaseAssert(inst != nullptr);
                    return inst;
                }

                ReleaseAssert(isa<ConstantExpr>(cst));
                ConstantExpr* ce = cast<ConstantExpr>(cst);
                Instruction* inst = ce->getAsInstruction(insertBefore);
                ReleaseAssert(inst != nullptr);

                // Expanding ConstantExpr should never result in cycle. Fire assert if a cycle is detected.
                //
                replacementMap[cst] = nullptr;
                for (Use& u : inst->operands())
                {
                    Value* val = u.get();
                    if (isa<Constant>(val))
                    {
                        Constant* c = cast<Constant>(val);
                        if (cstUsers.count(c))
                        {
                            Instruction* replacement = handleConstant(c, inst /*insertBefore*/);
                            u.set(replacement);
                        }
                    }
                }

                replacementMap[cst] = inst;
                return inst;
            };

            std::vector<Instruction*> instList;
            for (BasicBlock& bb : *mainFn)
            {
                for (Instruction& inst : bb)
                {
                    instList.push_back(&inst);
                }
            }

            for (Instruction* inst : instList)
            {
                for (Use& u : inst->operands())
                {
                    Value* val = u.get();
                    if (isa<Constant>(val))
                    {
                        Constant* c = cast<Constant>(val);
                        if (cstUsers.count(c))
                        {
                            Instruction* replacement = handleConstant(c, insPt /*insertBefore*/);
                            u.set(replacement);
                        }
                    }
                }
            }

            func->removeDeadConstantUsers();

            if (!func->use_empty())
            {
                fprintf(stderr, "[ERROR] Unexpected use of '%s' that is not handled correctly, a bug?\n", func->getName().str().c_str());
                module->dump();
                abort();
            }

            // Catch bugs as early as possible if any of the crap above went wrong..
            //
            ValidateLLVMModule(module.get());
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

    RunLLVMOptimizePass(module.get());
    ReleaseAssert(module->getFunction(res.m_resultFuncName) != nullptr);

    res.m_cgMod = std::move(module);
    return res;
}

}   // namespace dast

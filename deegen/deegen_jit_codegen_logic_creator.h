#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_bytecode_operand.h"
#include "deegen_global_bytecode_trait_accessor.h"
#include "deegen_dfg_jit_regalloc_rt_call_wrapper.h"
#include "drt/dfg_codegen_protocol.h"

namespace dast {

struct BaselineJitCodegenFnProto;
struct DfgJitCodegenFnProto;

struct JitCodegenFnProtoBase
{
    virtual ~JitCodegenFnProtoBase() = default;
    JitCodegenFnProtoBase() = default;
    MAKE_DEFAULT_COPYABLE(JitCodegenFnProtoBase);
    MAKE_DEFAULT_MOVABLE(JitCodegenFnProtoBase);

    virtual llvm::Function* GetFunction() = 0;
    virtual llvm::Value* GetJitUnalignedDataSecPtr() = 0;
    virtual llvm::Value* GetJitFastPathPtr() = 0;
    virtual llvm::Value* GetJitSlowPathPtr() = 0;
    virtual llvm::Value* GetSlowPathDataPtr() = 0;
    virtual llvm::Value* GetSlowPathDataOffset()  = 0;
    virtual llvm::Value* GetJitCodeBlock32() = 0;

    virtual DeegenEngineTier GetEngineTier() = 0;

    bool IsBaselineJIT() { return GetEngineTier() == DeegenEngineTier::BaselineJIT; }
    bool IsDfgJIT() { return GetEngineTier() == DeegenEngineTier::DfgJIT; }

    BaselineJitCodegenFnProto* AsBaselineJIT();
    DfgJitCodegenFnProto* AsDfgJIT();

    // Align the data section pointer if necessary, and return the aligned pointer
    //
    static llvm::Value* WARN_UNUSED EmitGetAlignedDataSectionPtr(llvm::Value* preAlignedDataSecPtr, size_t alignment, llvm::BasicBlock* insertAtEnd)
    {
        using namespace llvm;
        ReleaseAssert(insertAtEnd != nullptr);
        LLVMContext& ctx = insertAtEnd->getContext();

        ReleaseAssert(alignment > 0 && is_power_of_2(alignment));
        if (alignment == 1)
        {
            return preAlignedDataSecPtr;
        }
        else
        {
            ReleaseAssert(insertAtEnd->getParent() != nullptr && insertAtEnd->getParent()->getParent() != nullptr);
            return CreateCallToDeegenCommonSnippet(
                insertAtEnd->getParent()->getParent() /*module*/,
                "RoundPtrUpToMultipleOf",
                { preAlignedDataSecPtr, CreateLLVMConstantInt<uint64_t>(ctx, alignment) },
                insertAtEnd);
        }
    }
};

struct BaselineJitCodegenFnProto final : public JitCodegenFnProtoBase
{
    BaselineJitCodegenFnProto(llvm::Function* func) : JitCodegenFnProtoBase(), m_func(func) { }

    virtual DeegenEngineTier GetEngineTier() override { return DeegenEngineTier::BaselineJIT; }

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
                /*RSI*/ llvm_type_of<uint64_t>(ctx),    // BaselineCodeBlock lower-32bits
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

    static std::unique_ptr<BaselineJitCodegenFnProto> WARN_UNUSED Create(llvm::Module* module, const std::string& name)
    {
        using namespace llvm;
        FunctionType* fty = GetFTy(module->getContext());
        ReleaseAssert(module->getNamedValue(name) == nullptr);
        Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, name, module);
        ReleaseAssert(fn->getName() == name);
        std::unique_ptr<BaselineJitCodegenFnProto> r(new BaselineJitCodegenFnProto(fn));
        r->GetBytecodePtr()->setName("bytecodePtr");
        r->GetCondBrPatchRecPtr()->setName("condBrPatchRecPtr");
        r->GetJitUnalignedDataSecPtr()->setName("jitPreAlignedDataSecPtr");
        r->GetControlStructPtr()->setName("ctlStructPtr");
        r->GetSlowPathDataIndexPtr()->setName("slowPathDataIndexPtr");
        r->GetJitFastPathPtr()->setName("jitFastPathPtr");
        r->GetJitSlowPathPtr()->setName("jitSlowPathPtr");
        r->GetSlowPathDataPtr()->setName("slowPathDataPtr");
        r->GetSlowPathDataOffset()->setName("slowPathDataOffset");
        r->GetJitCodeBlock32()->setName("baselineCodeBlock32");

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
                                                      llvm::Value* baselineCodeBlock32)
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
        ReleaseAssert(baselineCodeBlock32 != nullptr);

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
                baselineCodeBlock32,
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

    virtual llvm::Function* GetFunction() override { return m_func; }
    virtual llvm::Value* GetJitUnalignedDataSecPtr() override { return m_func->getArg(x_jitUnalignedDataSecPtr); }
    virtual llvm::Value* GetJitFastPathPtr() override { return m_func->getArg(x_jitFastPathPtr); }
    virtual llvm::Value* GetJitSlowPathPtr() override { return m_func->getArg(x_jitSlowPathPtr); }
    virtual llvm::Value* GetSlowPathDataPtr() override { return m_func->getArg(x_slowPathDataPtr); }
    virtual llvm::Value* GetSlowPathDataOffset() override { return m_func->getArg(x_slowPathDataOffset); }
    virtual llvm::Value* GetJitCodeBlock32() override { return m_func->getArg(x_baselineCodeBlock32); }

    llvm::Value* GetBytecodePtr() { return m_func->getArg(x_bytecodePtr); }
    llvm::Value* GetCondBrPatchRecPtr() { return m_func->getArg(x_condBrPatchRecordPtr); }
    llvm::Value* GetControlStructPtr() { return m_func->getArg(x_controlStructPtr); }
    llvm::Value* GetSlowPathDataIndexPtr() { return m_func->getArg(x_slowPathDataIndexPtr); }

    llvm::Function* m_func;

    static constexpr uint32_t x_bytecodePtr = 0;
    static constexpr uint32_t x_condBrPatchRecordPtr = 1;
    static constexpr uint32_t x_jitUnalignedDataSecPtr = 2;
    static constexpr uint32_t x_slowPathDataOffset = 3;
    static constexpr uint32_t x_slowPathDataIndexPtr = 4;
    static constexpr uint32_t x_baselineCodeBlock32 = 5;
    static constexpr uint32_t x_controlStructPtr = 6;
    static constexpr uint32_t x_jitFastPathPtr = 7;
    static constexpr uint32_t x_jitSlowPathPtr = 8;
    static constexpr uint32_t x_slowPathDataPtr = 9;
};

struct DfgJitCodegenFnProto final : public JitCodegenFnProtoBase
{
    DfgJitCodegenFnProto(llvm::Function* func) : JitCodegenFnProtoBase(), m_func(func) { }

    virtual DeegenEngineTier GetEngineTier() override { return DeegenEngineTier::DfgJIT; }

    // This is not the final function prototype. We use this indirection to add NoAlias to all pointers
    //
    static llvm::FunctionType* WARN_UNUSED GetImplFTy(llvm::LLVMContext& ctx)
    {
        using namespace llvm;
        return FunctionType::get(
            llvm_type_of<void>(ctx) /*result*/,
            {
                /* 0 */ llvm_type_of<void*>(ctx),       // PrimaryCodegenState Ptr
                /* 1 */ llvm_type_of<void*>(ctx),       // jitFastPathAddr
                /* 2 */ llvm_type_of<void*>(ctx),       // jitSlowPathAddr
                /* 3 */ llvm_type_of<void*>(ctx),       // jitDataSecAddr
                /* 4 */ llvm_type_of<void*>(ctx),       // slowPathData Ptr
                /* 5 */ llvm_type_of<void*>(ctx),       // NodeOperandConfigData Ptr
                /* 6 */ llvm_type_of<void*>(ctx),       // NodeSpecificData (nsd) Ptr
                /* 7 */ llvm_type_of<void*>(ctx),       // RegAllocStateForCodegen Ptr
                /* 8 */ llvm_type_of<uint64_t>(ctx),    // SlowPathDataOffset
                /* 9 */ llvm_type_of<uint64_t>(ctx),    // DfgCodeBlock lower-32bits
                /* 10 */ llvm_type_of<void*>(ctx)       // compactedRegConfAddr
            } /*params*/,
            false /*isVarArg*/);
    }

    static std::unique_ptr<DfgJitCodegenFnProto> WARN_UNUSED Create(llvm::Module* module, const std::string& name)
    {
        using namespace llvm;
        FunctionType* fty = GetImplFTy(module->getContext());
        std::string implName = name + "_impl";
        ReleaseAssert(module->getNamedValue(implName) == nullptr);
        // We will change to InternalLinkage later, must be External now so it can survive LLVM passes
        //
        Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, implName, module);
        ReleaseAssert(fn->getName() == implName);

        std::unique_ptr<DfgJitCodegenFnProto> r(new DfgJitCodegenFnProto(fn));
        r->GetJitUnalignedDataSecPtr()->setName("jitPreAlignedDataSecPtr");
        r->GetJitFastPathPtr()->setName("jitFastPathPtr");
        r->GetJitSlowPathPtr()->setName("jitSlowPathPtr");
        r->GetSlowPathDataPtr()->setName("slowPathDataPtr");
        r->GetSlowPathDataOffset()->setName("slowPathDataOffset");
        r->GetJitCodeBlock32()->setName("baselineCodeBlock32");

        r->GetPrimaryCodegenStatePtr()->setName("primaryCodegenState");
        r->GetNodeOperandConfigDataPtr()->setName("nodeOperandConfigData");
        r->GetNodeSpecificDataPtr()->setName("nsd");
        r->GetRegAllocStateForCodegenPtr()->setName("regAllocStateForCodegen");
        r->GetCompactedRegConfPtr()->setName("compactedRegConfPtr");

        // By design, all the pointers are known to operate on non-overlapping ranges
        // and exclusively owning the respective ranges, so add noalias to help optimization
        //
        fn->addParamAttr(x_primaryCodegenStatePtr, Attribute::NoAlias);
        fn->addParamAttr(x_jitFastPathPtr, Attribute::NoAlias);
        fn->addParamAttr(x_jitSlowPathPtr, Attribute::NoAlias);
        fn->addParamAttr(x_jitUnalignedDataSecPtr, Attribute::NoAlias);
        fn->addParamAttr(x_slowPathDataPtr, Attribute::NoAlias);
        fn->addParamAttr(x_nodeOperandConfigDataPtr, Attribute::NoAlias);
        fn->addParamAttr(x_nodeSpecificDataPtr, Attribute::NoAlias);
        fn->addParamAttr(x_regAllocStateForCodegenPtr, Attribute::NoAlias);
        fn->addParamAttr(x_compactedRegConfAddr, Attribute::NoAlias);

        fn->setDSOLocal(true);

        return r;
    }

    void CreateWrapper(llvm::Module* module, const std::string& name)
    {
        using namespace llvm;
        LLVMContext& ctx = module->getContext();

        // Must agree with CodegenImplFn in dfg_codegen_protocol.h
        //
        FunctionType* fty = FunctionType::get(
            llvm_type_of<void>(ctx) /*result*/,
            {
                llvm_type_of<void*>(ctx),       // PrimaryCodegenState Ptr
                llvm_type_of<void*>(ctx),       // NodeOperandConfigData ptr
                llvm_type_of<void*>(ctx),       // NodeSpecificData ptr
                llvm_type_of<void*>(ctx)        // RegAllocStateForCodegen ptr
            } /*params*/,
            false /*isVarArg*/);

        ReleaseAssert(module->getNamedValue(name) == nullptr);
        Function* fn = Function::Create(fty, GlobalValue::ExternalLinkage, name, module);
        ReleaseAssert(fn->getName() == name);

        fn->addParamAttr(0, Attribute::NoAlias);
        fn->addParamAttr(1, Attribute::NoAlias);
        fn->addParamAttr(2, Attribute::NoAlias);
        fn->addParamAttr(3, Attribute::NoAlias);

        fn->setDSOLocal(true);

        Value* pcs = fn->getArg(0);
        Value* nodeOperandConfigData = fn->getArg(1);
        Value* nsd = fn->getArg(2);
        Value* regAllocStateForCodegen = fn->getArg(3);

        pcs->setName("primaryCodegenState");
        nodeOperandConfigData->setName("nodeOperandConfigData");
        nsd->setName("nsd");
        regAllocStateForCodegen->setName("regAllocStateForCodegen");

        BasicBlock* bb = BasicBlock::Create(ctx, "", fn);

        // Load all information from PrimaryCodegenState
        //
        using PCS = dfg::PrimaryCodegenState;
        Value* fastPathAddr = GetMemberFromCppStruct<&PCS::m_fastPathAddr>(pcs, bb);
        Value* slowPathAddr = GetMemberFromCppStruct<&PCS::m_slowPathAddr>(pcs, bb);
        Value* dataSecAddr = GetMemberFromCppStruct<&PCS::m_dataSecAddr>(pcs, bb);
        Value* slowPathDataAddr = GetMemberFromCppStruct<&PCS::m_slowPathDataAddr>(pcs, bb);
        Value* slowPathDataOffset = GetMemberFromCppStruct<&PCS::m_slowPathDataOffset>(pcs, bb);
        Value* compactRegConfAddr = GetMemberFromCppStruct<&PCS::m_compactedRegConfAddr>(pcs, bb);
        Value* dfgCodeBlock32 = GetMemberFromCppStruct<&PCS::m_dfgCodeBlockLower32Bits>(pcs, bb);

        fastPathAddr->setName("jitFastPathAddr");
        slowPathAddr->setName("jitSlowPathAddr");
        dataSecAddr->setName("jitDataSecAddr");
        slowPathDataAddr->setName("slowPathDataAddr");
        slowPathDataOffset->setName("slowPathDataOffset");
        compactRegConfAddr->setName("compactRegConfAddr");
        dfgCodeBlock32->setName("dfgCodeBlock32");

        // Set up the call to the impl function
        //
        Function* implFn = module->getFunction(name + "_impl");
        ReleaseAssert(implFn != nullptr && !implFn->empty());

        implFn->setLinkage(GlobalValue::InternalLinkage);
        implFn->addFnAttr(Attribute::AlwaysInline);

        CopyFunctionAttributes(fn /*dst*/, implFn /*src*/);

        size_t numArgs = implFn->arg_size();
        std::vector<Value*> args;
        args.resize(numArgs, nullptr /*value*/);

        auto insertArg = [&](size_t argOrd, Value* val)
        {
            ReleaseAssert(argOrd < args.size());
            ReleaseAssert(args[argOrd] == nullptr && val != nullptr);
            args[argOrd] = val;
        };

        insertArg(x_primaryCodegenStatePtr, pcs);
        insertArg(x_jitFastPathPtr, fastPathAddr);
        insertArg(x_jitSlowPathPtr, slowPathAddr);
        insertArg(x_jitUnalignedDataSecPtr, dataSecAddr);
        insertArg(x_slowPathDataPtr, slowPathDataAddr);
        insertArg(x_nodeOperandConfigDataPtr, nodeOperandConfigData);
        insertArg(x_nodeSpecificDataPtr, nsd);
        insertArg(x_regAllocStateForCodegenPtr, regAllocStateForCodegen);
        insertArg(x_slowPathDataOffset, slowPathDataOffset);
        insertArg(x_dfgCodeBlock32, dfgCodeBlock32);
        insertArg(x_compactedRegConfAddr, compactRegConfAddr);

        ReleaseAssert(args.size() == numArgs);
        for (Value* arg : args) { ReleaseAssert(arg != nullptr); }

        CallInst::Create(implFn, args, "", bb);
        ReturnInst::Create(ctx, nullptr, bb);
    }

    virtual llvm::Function* GetFunction() override { return m_func; }
    virtual llvm::Value* GetJitUnalignedDataSecPtr() override { return m_func->getArg(x_jitUnalignedDataSecPtr); }
    virtual llvm::Value* GetJitFastPathPtr() override { return m_func->getArg(x_jitFastPathPtr); }
    virtual llvm::Value* GetJitSlowPathPtr() override { return m_func->getArg(x_jitSlowPathPtr); }
    virtual llvm::Value* GetSlowPathDataPtr() override { return m_func->getArg(x_slowPathDataPtr); }
    virtual llvm::Value* GetSlowPathDataOffset() override { return m_func->getArg(x_slowPathDataOffset); }
    virtual llvm::Value* GetJitCodeBlock32() override { return m_func->getArg(x_dfgCodeBlock32); }

    llvm::Value* GetPrimaryCodegenStatePtr() { return m_func->getArg(x_primaryCodegenStatePtr); }

    // This is the NodeOperandConfigData* that holds the various physical slot info for the node.
    //
    llvm::Value* GetNodeOperandConfigDataPtr() { return m_func->getArg(x_nodeOperandConfigDataPtr); }

    // For DFG guest language node, this is the NodeSpecificData pointer for the node
    // For DFG built-in node, this is an uint64_t array that holds the value of each literal operand
    //
    llvm::Value* GetNodeSpecificDataPtr() { return m_func->getArg(x_nodeSpecificDataPtr); }

    llvm::Value* GetRegAllocStateForCodegenPtr() { return m_func->getArg(x_regAllocStateForCodegenPtr); }

    // Never used for DFG builtin nodes
    //
    llvm::Value* GetCompactedRegConfPtr() { return m_func->getArg(x_compactedRegConfAddr); }

    llvm::Function* m_func;

    static constexpr uint32_t x_primaryCodegenStatePtr = 0;
    static constexpr uint32_t x_jitFastPathPtr = 1;
    static constexpr uint32_t x_jitSlowPathPtr = 2;
    static constexpr uint32_t x_jitUnalignedDataSecPtr = 3;
    static constexpr uint32_t x_slowPathDataPtr = 4;
    static constexpr uint32_t x_nodeOperandConfigDataPtr = 5;
    static constexpr uint32_t x_nodeSpecificDataPtr = 6;
    static constexpr uint32_t x_regAllocStateForCodegenPtr = 7;
    static constexpr uint32_t x_slowPathDataOffset = 8;
    static constexpr uint32_t x_dfgCodeBlock32 = 9;
    static constexpr uint32_t x_compactedRegConfAddr = 10;
};

inline BaselineJitCodegenFnProto* JitCodegenFnProtoBase::AsBaselineJIT()
{
    ReleaseAssert(IsBaselineJIT());
    return assert_cast<BaselineJitCodegenFnProto*>(this);
}

inline DfgJitCodegenFnProto* JitCodegenFnProtoBase::AsDfgJIT()
{
    ReleaseAssert(IsDfgJIT());
    return assert_cast<DfgJitCodegenFnProto*>(this);
}

struct JitCodeGenLogicCreator
{
    JitCodeGenLogicCreator()
        : m_bii(nullptr)
        , m_gbta(nullptr)
        , m_fastPathCodeLen(static_cast<size_t>(-1))
        , m_slowPathCodeLen(static_cast<size_t>(-1))
        , m_dataSectionCodeLen(static_cast<size_t>(-1))
        , m_dataSectionAlignment(static_cast<size_t>(-1))
        , m_numCondBrLatePatches(static_cast<size_t>(-1))
        , m_slowPathDataLen(static_cast<size_t>(-1))
        , m_allCallIcTraitDescs()
        , m_allGenericIcTraitDescs()
        , m_cgMod(nullptr)
        , m_resultFuncName()
        , m_implModulesForAudit()
        , m_extraAuditFiles()
        , m_extraVerboseAuditFiles()
        , m_disasmForAudit()
        , m_didLowering(false)
        , m_generated(false)
    { }

    BytecodeIrInfo* m_bii;

    // Only used for baseline JIT
    //
    const DeegenGlobalBytecodeTraitAccessor* m_gbta;

    size_t m_fastPathCodeLen;
    size_t m_slowPathCodeLen;
    size_t m_dataSectionCodeLen;
    size_t m_dataSectionAlignment;
    size_t m_numCondBrLatePatches;      // populated for baseline JIT only
    size_t m_slowPathDataLen;

    struct CallIcTraitDesc
    {
        size_t m_ordInTraitTable;
        size_t m_allocationLength;
        bool m_isDirectCall;
        std::vector<std::pair<size_t /*offset*/, bool /*is64*/>> m_codePtrPatchRecords;
    };

    std::vector<CallIcTraitDesc> m_allCallIcTraitDescs;

    std::vector<AstInlineCache::JitFinalLoweringResult::TraitDesc> m_allGenericIcTraitDescs;

    // The module that contains the full logic of the baseline JIT codegen function for this bytecode, which
    // emits the JIT code, creates the slow path data and dispatches to the codegen function for the next bytecode
    //
    std::unique_ptr<llvm::Module> m_cgMod;

    // May only exist if for DFG and reg alloc is enabled
    //
    std::vector<DfgRegAllocCCallWrapperRequest> m_dfgCWrapperRequests;

    std::string m_resultFuncName;

    // The list of modules that were used to generate the stencils.
    // The first module is always the main JIT component, followed by the return continuations.
    // For audit purpose only.
    //
    std::vector<std::pair<std::unique_ptr<llvm::Module>, std::string /*fnName*/>> m_implModulesForAudit;

    // Extra list of audit files to be stored into the audit folder
    //
    std::vector<std::pair<std::string /*fileName*/, std::string /*fileContents*/>> m_extraAuditFiles;

    // Extra list of verbose audit files to be stored into the verbose audit folder
    //
    std::vector<std::pair<std::string /*fileName*/, std::string /*fileContents*/>> m_extraVerboseAuditFiles;

    // Human-readable disassembly of what code will be generated, for audit purpose only
    //
    std::string m_disasmForAudit;

private:
    // Used internally, the JitImplCreator for each return continuation
    //
    std::vector<std::unique_ptr<JitImplCreatorBase>> m_rcJicList;

    bool m_didLowering;
    bool m_generated;

public:
    void SetBII(BytecodeIrInfo* bii) { ReleaseAssert(m_bii == nullptr); m_bii = bii; }
    BytecodeIrInfo* GetBII() { ReleaseAssert(m_bii != nullptr); return m_bii; }

    bool IsDfgVariant()
    {
        return GetBII()->m_bytecodeDef->m_isDfgVariant;
    }

    const DeegenGlobalBytecodeTraitAccessor* GetGBTA() { ReleaseAssert(m_gbta != nullptr); return m_gbta; }

    void DoAllLowering(JitImplCreatorBase* mainComponentJic);

    void GenerateLogic(JitImplCreatorBase* mainComponentJic, std::string dfgFnNameExtraSuffix = "");

    static JitCodeGenLogicCreator WARN_UNUSED CreateForBaselineJIT(BytecodeIrInfo& bii, const DeegenGlobalBytecodeTraitAccessor& bcTraitAccessor);

    static void SetSectionForCodegenPrivateData(llvm::Module* cgMod, std::string secName);
    static void SetSectionsForCodegenModule(DeegenEngineTier tier, llvm::Module* cgMod, std::string cgFnName);
    static void SetSectionsForIcCodegenModule(DeegenEngineTier tier, llvm::Module* cgMod);

    static std::vector<DfgRegAllocCCallWrapperRequest> WARN_UNUSED GenerateCCallWrapperRequests(
        uint64_t maskForAllUsedFprs,
        const std::string& wrapperPrefix,
        const std::vector<DfgCCallFuncInfo>& wrappers);
};

// Emit the implementation of 'deegen_baseline_jit_do_codegen_impl'
//
void DeegenGenerateBaselineJitCompilerCppEntryFunction(llvm::Module* module);

// Emit the implementation of 'deegen_baseline_jit_codegen_finish'
//
void DeegenGenerateBaselineJitCodegenFinishFunction(llvm::Module* module);

}   // namespace dast

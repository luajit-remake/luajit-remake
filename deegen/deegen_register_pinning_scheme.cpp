#include "deegen_register_pinning_scheme.h"
#include "annotated/deegen_common_snippets/deegen_common_snippet_ir_accessor.h"
#include "deegen_options.h"
#include "x64_register_info.h"
#include "dfg_reg_alloc_register_info.h"
#include "reflective_stringify_helper.h"

#include "drt/dfg_reg_move_inst_generator.h"

namespace dast {

// Make sure the stack base defined in DFG agrees with us
//
static_assert(dfg::x_dfg_stack_base_register == RPV_StackBase::Reg());

// The arguments passed to the slow path will use the registers listed below, in the listed order.
//
// The order of the GPR list is chosen to stay away from the C calling conv registers,
// in the hope that it can reduce the likelihood of register shuffling when making C calls.
//
constexpr X64Reg x_deegen_slow_path_regs_for_gpr_args[] = {
    X64Reg::R10,
    X64Reg::R11,
    X64Reg::R9,
    X64Reg::R8,
    X64Reg::RSI,
    X64Reg::RDI
};

constexpr X64Reg x_deegen_slow_path_regs_for_fpr_args[] = {
    X64Reg::XMM1,
    X64Reg::XMM2,
    X64Reg::XMM3,
    X64Reg::XMM4,
    X64Reg::XMM5,
    X64Reg::XMM6
};

// Check that the SlowPath extra registers and DFG reg alloc registers are compatible with the interface
// Unfortunately due to the design the check cannot be static_asserted in constexpr,
// so we just run this check the first time GetFunctionType() is called
//
static bool g_regConfigCompatibilityChecked = false;

template<typename InterfaceTy>
static void CheckExtraRegistersCompatibleWithInterface(const X64Reg* regs, size_t numRegs)
{
    static_assert(std::is_base_of_v<ExecutorCtxInfoBase<InterfaceTy>, InterfaceTy>);
    InterfaceTy instance;
    ExecutorCtxInfoBase<InterfaceTy>* base = &instance;

    std::unordered_set<uint32_t> usedArgOrds;
    for (RegisterPinnedValueBase* e : base->GetValues())
    {
        uint32_t argOrd = e->GetFnCtxArgOrd();
        ReleaseAssert(!usedArgOrds.count(argOrd));
        usedArgOrds.insert(argOrd);
    }

    std::unordered_set<uint32_t> extraArgOrds;
    for (size_t idx = 0; idx < numRegs; idx++)
    {
        X64Reg reg = regs[idx];
        uint32_t argOrd = RegisterPinningScheme::GetArgumentOrdinalForRegister(reg);
        if (usedArgOrds.count(argOrd))
        {
            fprintf(stderr, "Extra register %s conflicts with an existing interface register in interface %s!\n",
                    reg.GetName(), std::string(__stringify_type__<InterfaceTy>()).c_str());
            ReleaseAssert(false);
        }
        if (extraArgOrds.count(argOrd))
        {
            fprintf(stderr, "Duplicate registers in extra register list for slowpath or DFG reg alloc!\n");
            ReleaseAssert(false);
        }
        extraArgOrds.insert(argOrd);
    }
}

static void AssertRegisterConfigCompatibilityIfNotCheckedYet()
{
    if (g_regConfigCompatibilityChecked) { return; }
    g_regConfigCompatibilityChecked = true;

    {
        // The list of extra registers used by the slow path
        //
        std::vector<X64Reg> allSlowPathRegs;
        allSlowPathRegs.insert(allSlowPathRegs.end(),
                               x_deegen_slow_path_regs_for_gpr_args,
                               x_deegen_slow_path_regs_for_gpr_args + std::extent_v<decltype(x_deegen_slow_path_regs_for_gpr_args)>);
        allSlowPathRegs.insert(allSlowPathRegs.end(),
                               x_deegen_slow_path_regs_for_fpr_args,
                               x_deegen_slow_path_regs_for_fpr_args + std::extent_v<decltype(x_deegen_slow_path_regs_for_fpr_args)>);

        // The slow path registers should be compatible with InterpreterInterface and JitAOTSlowPathInterface
        //
        CheckExtraRegistersCompatibleWithInterface<InterpreterInterface>(allSlowPathRegs.data(), allSlowPathRegs.size());
        CheckExtraRegistersCompatibleWithInterface<JitAOTSlowPathInterface>(allSlowPathRegs.data(), allSlowPathRegs.size());
    }
    {
        // The list of extra registers used by DFG reg alloc
        //
        std::vector<X64Reg> allDfgRegAllocRegs;
        allDfgRegAllocRegs.insert(allDfgRegAllocRegs.end(),
                                  x_dfg_reg_alloc_gprs,
                                  x_dfg_reg_alloc_gprs + std::extent_v<decltype(x_dfg_reg_alloc_gprs)>);
        allDfgRegAllocRegs.insert(allDfgRegAllocRegs.end(),
                                  x_dfg_reg_alloc_fprs,
                                  x_dfg_reg_alloc_fprs + std::extent_v<decltype(x_dfg_reg_alloc_fprs)>);

        // The DFG reg alloc registers should be compatible with JitGeneratedCodeInterface and JitAOTSlowPathSaveRegStubInterface
        //
        CheckExtraRegistersCompatibleWithInterface<JitGeneratedCodeInterface>(allDfgRegAllocRegs.data(), allDfgRegAllocRegs.size());
        CheckExtraRegistersCompatibleWithInterface<JitAOTSlowPathSaveRegStubInterface>(allDfgRegAllocRegs.data(), allDfgRegAllocRegs.size());
    }
}

constexpr X64Reg x_regNamesInGhcCC[] = {
    X64Reg::R13,
    X64Reg::RBP,
    X64Reg::R12,
    X64Reg::RBX,
    X64Reg::R14,
    X64Reg::RSI,
    X64Reg::RDI,
    X64Reg::R8,
    X64Reg::R9,
    X64Reg::R15,
    X64Reg::R10,
    X64Reg::R11,
    X64Reg::RDX,
    X64Reg::XMM1,
    X64Reg::XMM2,
    X64Reg::XMM3,
    X64Reg::XMM4,
    X64Reg::XMM5,
    X64Reg::XMM6
};

llvm::FunctionType* WARN_UNUSED RegisterPinningScheme::GetFunctionType(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    AssertRegisterConfigCompatibilityIfNotCheckedYet();

    // We use GHC calling convension so we can pass more info and allow less-overhead C runtime call.
    //
    // Speficially, all 6 callee-saved registers (under default cc calling convention)
    // are available as parameters in GHC. We use this as a register pinning mechanism to pin
    // the important states (coroutineCtx, stackBase, etc) into these registers
    // so that they are not clobbered by C calls.
    //
    // GHC passes integral parameters in the following order:
    //   R13 [CC/MSABI callee saved]
    //   RBP [CC/MSABI callee saved]
    //   R12 [CC/MSABI callee saved]
    //   RBX [CC/MSABI callee saved]
    //   R14 [CC/MSABI callee saved]
    //   RSI [MSABI callee saved]
    //   RDI [MSABI callee saved]
    //   R8
    //   R9
    //   R15 [CC/MSABI callee saved]
    //   R10 <--- starting here are the registers we added to LLVM's GHC convention
    //   R11
    //   RDX
    //
    FunctionType* fty = FunctionType::get(
        llvm_type_of<void>(ctx) /*result*/,
        {
            // R13 [CC/MSABI callee saved]
            // Tag register 2
            //
            llvm_type_of<uint64_t>(ctx),

            // RBP [CC/MSABI callee saved]
            // For bytecode function: the current codeBlock
            // For return continuation: unused
            // For function entry: codeblockHeapPtr
            //
            llvm_type_of<void*>(ctx),

            // R12 [CC/MSABI callee saved]
            // Tag register 1
            //
            llvm_type_of<uint64_t>(ctx),

            // RBX [CC/MSABI callee saved]
            // StackBase (for return continuation, this is the callee's stack base)
            //
            llvm_type_of<void*>(ctx),

            // R14 [CC/MSABI callee saved]
            // For bytecode function: the current bytecode
            // For return continuation: unused
            // For function entry: #args
            //
            llvm_type_of<void*>(ctx),

            // RSI [MSABI callee saved]
            // For return continuation: the start of the ret values
            // Otherwise unused
            //
            llvm_type_of<void*>(ctx),

            // RDI [MSABI callee saved]
            // For return continuation: the # of ret values
            // For function entry: isMustTail64
            // Otherwise unused
            //
            llvm_type_of<uint64_t>(ctx),

            // R8
            // unused
            //
            llvm_type_of<uint64_t>(ctx),

            // R9
            // unused
            //
            llvm_type_of<uint64_t>(ctx),

            // R15 [CC/MSABI callee saved]
            // CoroutineCtx
            //
            llvm_type_of<void*>(ctx),

            // R10
            // unused
            //
            llvm_type_of<uint64_t>(ctx),

            // R11
            // unused
            //
            llvm_type_of<uint64_t>(ctx),

            // RDX
            // JitSlowPathDataPtr for DfgSaveRegisterStub
            //
            llvm_type_of<void*>(ctx),

            // XMM1
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM2
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM3
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM4
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM5
            // unused
            //
            llvm_type_of<double>(ctx),

            // XMM6
            // unused
            //
            llvm_type_of<double>(ctx)
        } /*params*/,
        false /*isVarArg*/);

    ReleaseAssert(fty->getNumParams() == std::extent_v<decltype(x_regNamesInGhcCC)>);
    return fty;
}

size_t WARN_UNUSED RegisterPinningScheme::GetFunctionTypeNumArguments()
{
    return std::extent_v<decltype(x_regNamesInGhcCC)>;
}

llvm::Function* WARN_UNUSED RegisterPinningScheme::CreateFunction(llvm::Module* module, const std::string& name)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(module->getNamedValue(name) == nullptr);
    Function* func = Function::Create(GetFunctionType(module->getContext()), GlobalValue::ExternalLinkage, name, module);
    ReleaseAssert(func != nullptr && func->getName() == name);
    func->setCallingConv(CallingConv::GHC);
    func->setDSOLocal(true);

    func->addFnAttr(Attribute::NoUnwind);

    // Set up function attributes
    //
    {
        // We can use any function compiled from C++ to copy attributes: these attributes are set up by the C++ compiler and should be the same
        //
        std::pair<std::unique_ptr<Module>, Function*> modHolderAndFn = GetDeegenCommonSnippetModule(ctx, "RoundPtrUpToMultipleOf");
        Function* funcToCopyAttrFrom = modHolderAndFn.second;
        ReleaseAssert(funcToCopyAttrFrom != nullptr);
        ReleaseAssert(!funcToCopyAttrFrom->empty());

        CopyFunctionAttributes(func /*dst*/, funcToCopyAttrFrom /*src*/);
    }

    return func;
}

std::unique_ptr<llvm::Module> WARN_UNUSED RegisterPinningScheme::CreateModule(const std::string& name, llvm::LLVMContext& ctx)
{
    using namespace llvm;
    std::unique_ptr<llvm::Module> module = std::make_unique<Module>(name, ctx);
    // Link in some common snippet, which is needed to correctly set up some base stuffs like the target triple
    //
    Function* fakeFn = LinkInDeegenCommonSnippet(module.get(), "RoundPtrUpToMultipleOf");
    fakeFn->eraseFromParent();
    return module;
}

std::vector<uint64_t> WARN_UNUSED RegisterPinningScheme::GetAvaiableGPRListForBytecodeSlowPath()
{
    std::vector<uint64_t> res;
    size_t num = std::extent_v<decltype(x_deegen_slow_path_regs_for_gpr_args)>;
    for (size_t i = 0; i < num; i++)
    {
        res.push_back(GetArgumentOrdinalForRegister(x_deegen_slow_path_regs_for_gpr_args[i]));
    }
    return res;
}

std::vector<uint64_t> WARN_UNUSED RegisterPinningScheme::GetAvaiableFPRListForBytecodeSlowPath()
{
    std::vector<uint64_t> res;
    size_t num = std::extent_v<decltype(x_deegen_slow_path_regs_for_fpr_args)>;
    for (size_t i = 0; i < num; i++)
    {
        res.push_back(GetArgumentOrdinalForRegister(x_deegen_slow_path_regs_for_fpr_args[i]));
    }
    return res;
}

constexpr auto x_regToGhcArgOrdinalMapping = []() {
    std::array<size_t, X64Reg::x_totalNumGprs + X64Reg::x_totalNumFprs> result;
    for (size_t i = 0; i < X64Reg::x_totalNumGprs + X64Reg::x_totalNumFprs; i++)
    {
        result[i] = static_cast<size_t>(-1);
    }
    for (size_t i = 0; i < std::extent_v<decltype(x_regNamesInGhcCC)>; i++)
    {
        X64Reg reg = x_regNamesInGhcCC[i];
        size_t ord;
        if (reg.IsGPR())
        {
            ord = reg.MachineOrd();
            ReleaseAssert(ord < X64Reg::x_totalNumGprs);
        }
        else
        {
            ord = reg.MachineOrd() + X64Reg::x_totalNumGprs;
        }
        ReleaseAssert(ord < X64Reg::x_totalNumGprs + X64Reg::x_totalNumFprs);
        ReleaseAssert(result[ord] == static_cast<size_t>(-1));
        result[ord] = i;
    }
    return result;
}();

const char* WARN_UNUSED RegisterPinningScheme::GetRegisterName(size_t argOrd)
{
    ReleaseAssert(argOrd < std::extent_v<decltype(x_regNamesInGhcCC)>);
    return x_regNamesInGhcCC[argOrd].GetName();
}

X64Reg RegisterPinningScheme::GetRegisterForArgumentOrdinal(size_t ord)
{
    ReleaseAssert(ord < std::extent_v<decltype(x_regNamesInGhcCC)>);
    return x_regNamesInGhcCC[ord];
}

uint32_t RegisterPinningScheme::GetArgumentOrdinalForRegister(X64Reg reg)
{
    size_t ord;
    if (reg.IsGPR())
    {
        ord = reg.MachineOrd();
        ReleaseAssert(ord < X64Reg::x_totalNumGprs);
    }
    else
    {
        ord = reg.MachineOrd() + X64Reg::x_totalNumGprs;
    }
    ReleaseAssert(ord < x_regToGhcArgOrdinalMapping.size());
    size_t result = x_regToGhcArgOrdinalMapping[ord];
    ReleaseAssert(result != static_cast<size_t>(-1));
    ReleaseAssert(GetRegisterForArgumentOrdinal(result) == reg);
    return SafeIntegerCast<uint32_t>(result);
}

llvm::Value* WARN_UNUSED RegisterPinningScheme::GetArgumentAsInt64Value(llvm::Function* interfaceFn, uint64_t argOrd, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = interfaceFn->getContext();
    ReleaseAssert(interfaceFn->getFunctionType() == RegisterPinningScheme::GetFunctionType(ctx));
    ReleaseAssert(argOrd < interfaceFn->arg_size());
    Value* arg = interfaceFn->getArg(static_cast<uint32_t>(argOrd));
    if (llvm_value_has_type<double>(arg))
    {
        Instruction* dblToI64 = new BitCastInst(arg, llvm_type_of<uint64_t>(ctx), "", insertBefore);
        return dblToI64;
    }
    else if (llvm_value_has_type<void*>(arg))
    {
        Instruction* ptrToI64 = new PtrToIntInst(arg, llvm_type_of<uint64_t>(ctx), "", insertBefore);
        return ptrToI64;
    }
    else
    {
        ReleaseAssert(llvm_value_has_type<uint64_t>(arg));
        return arg;
    }
}

llvm::Value* WARN_UNUSED RegisterPinningScheme::GetArgumentAsInt64Value(llvm::Function* interfaceFn, uint64_t argOrd, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(insertAtEnd->getContext(), insertAtEnd);
    Value* res = GetArgumentAsInt64Value(interfaceFn, argOrd, dummy /*insertBefore*/);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED RegisterPinningScheme::EmitCastI64ToArgumentType(llvm::Value* value, uint64_t argOrd, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = value->getContext();
    FunctionType* fnTy = GetFunctionType(ctx);
    ReleaseAssert(argOrd < fnTy->getNumParams());
    Type* dstTy = fnTy->getParamType(static_cast<uint32_t>(argOrd));

    ReleaseAssert(llvm_value_has_type<uint64_t>(value));
    if (llvm_type_has_type<double>(dstTy))
    {
        return new BitCastInst(value, llvm_type_of<double>(ctx), "", insertBefore);
    }
    else if (llvm_type_has_type<void*>(dstTy))
    {
        return new IntToPtrInst(value, llvm_type_of<void*>(ctx), "", insertBefore);
    }
    else
    {
        ReleaseAssert(llvm_type_has_type<uint64_t>(dstTy));
        return value;
    }
}

llvm::Value* WARN_UNUSED RegisterPinningScheme::EmitCastI64ToArgumentType(llvm::Value* value, uint64_t argOrd, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(insertAtEnd->getContext(), insertAtEnd);
    Value* res = EmitCastI64ToArgumentType(value, argOrd, dummy /*insertBefore*/);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED RegisterPinningScheme::GetRegisterValueAtEntry(llvm::Function* interfaceFn, X64Reg reg)
{
    using namespace llvm;
    ReleaseAssert(interfaceFn != nullptr);
    LLVMContext& ctx = interfaceFn->getContext();
    FunctionType* fty = GetFunctionType(ctx);
    ReleaseAssert(interfaceFn->getFunctionType() == fty);

    uint32_t argOrd = GetArgumentOrdinalForRegister(reg);
    ReleaseAssert(argOrd < interfaceFn->arg_size());
    return interfaceFn->getArg(argOrd);
}

void RegisterPinningScheme::SetExtraDispatchArgument(llvm::CallInst* callInst, uint32_t argOrd, llvm::Value* newVal)
{
    using namespace llvm;
    ReleaseAssert(callInst != nullptr);
    LLVMContext& ctx = callInst->getContext();

    FunctionType* fty = GetFunctionType(ctx);
    ReleaseAssert(callInst->getFunctionType() == fty);
    ReleaseAssert(callInst->arg_size() == fty->getNumParams());
    ReleaseAssert(argOrd < callInst->arg_size());
    ReleaseAssert(isa<UndefValue>(callInst->getArgOperand(argOrd)));
    ReleaseAssert(newVal != nullptr && newVal->getType() == fty->getParamType(argOrd));
    callInst->setArgOperand(argOrd, newVal);
}

void RegisterPinningScheme::SetExtraDispatchArgument(llvm::CallInst* callInst, X64Reg reg, llvm::Value* newVal)
{
    uint32_t argOrd = GetArgumentOrdinalForRegister(reg);
    SetExtraDispatchArgument(callInst, argOrd, newVal);
}

llvm::CallInst* RegisterPinningScheme::CreateDispatchWithAllUndefArgs(llvm::Value* target, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = target->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(target));
    ReleaseAssert(insertBefore != nullptr);

    FunctionType* fty = RegisterPinningScheme::GetFunctionType(ctx);

    std::vector<Value*> args;
    for (uint32_t i = 0; i < fty->getNumParams(); i++)
    {
        args.push_back(UndefValue::get(fty->getParamType(i)));
    }

    CallInst* callInst = CallInst::Create(fty, target, args, "" /*name*/, insertBefore);
    callInst->setTailCallKind(CallInst::TailCallKind::TCK_MustTail);
    callInst->setCallingConv(CallingConv::GHC);
    ReleaseAssert(llvm_value_has_type<void>(callInst));

    ReturnInst::Create(ctx, nullptr /*retVal*/, insertBefore);

    return callInst;
}

}   // namespace dast

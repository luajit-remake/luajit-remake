#include "misc_llvm_helper.h"

#include "bytecode_definition_utils.h"

// TODO: fix this
#include "bytecode.h"

namespace dast
{

inline bool IsTValueTypeCheckAPIFunction(llvm::Function* func, TypeSpeculationMask* typeMask /*out*/ = nullptr)
{
    std::string fnName = func->getName().str();
    ReleaseAssert(fnName != "");
    if (!IsCXXSymbol(fnName))
    {
        return false;
    }
    std::string demangledName = GetDemangledName(func);
    constexpr const char* expected_prefix = "bool DeegenImpl_TValueIs<";
    constexpr const char* expected_suffix = ">(TValue)";
    if (demangledName.starts_with(expected_prefix) && demangledName.ends_with(expected_suffix))
    {
        if (typeMask != nullptr)
        {
            demangledName = demangledName.substr(strlen(expected_prefix));
            demangledName = demangledName.substr(0, demangledName.length() - strlen(expected_suffix));

            bool found = false;
            constexpr auto defs = detail::get_type_speculation_defs<TypeSpecializationList>::value;
            for (size_t i = 0; i < defs.size(); i++)
            {
                if (demangledName == defs[i].second)
                {
                    *typeMask = defs[i].first;
                    found = true;
                    break;
                }
            }
            ReleaseAssert(found);
        }
        return true;
    }
    else
    {
        return false;
    }
}

inline DesugarDecision ShouldDesugarTValueAPI(llvm::Function* func, DesugaringLevel level)
{
    if (!IsTValueTypeCheckAPIFunction(func))
    {
        return DesugarDecision::DontCare;
    }
    if (level >= DesugaringLevel::TypeSpecialization)
    {
        return DesugarDecision::MustInline;
    }
    else
    {
        return DesugarDecision::MustNotInline;
    }
}

class BcOperand;

// A LLVM function that implements a bytecode
//
class InterpreterFunction
{
public:
    InterpreterFunction(const std::string& implFunctionName)
        : m_implFunctionName(implFunctionName)
        , m_stubFunction(nullptr)
        , m_implFunction(nullptr)
        , m_allocaBBForStubFunction(nullptr)
        , m_initBBForStubFunction(nullptr)
        , m_codeBlockValue(nullptr)
        , m_operands()
        , m_bytecodeStructLength(static_cast<size_t>(-1))
    { }

    static constexpr uint32_t x_coroutineCtx = 0;
    static constexpr uint32_t x_stackBase = 1;
    // For a normal interpreter bytecode function, this is the 'curBytecode'
    // For the return continuation, this is the 'retStart'
    //
    static constexpr uint32_t x_curBytecodeOrRetStart = 2;
    // For a normal interpreter bytecode function, this is unused
    // For the return continuation, this is the # of return values
    //
    static constexpr uint32_t x_numRetValues = 3;

    void BindToModule(llvm::Module* module);

    void SetMaxOperandWidthBytes(size_t maxWidthBytes);

    llvm::Value* GetCoroutineCtx()
    {
        llvm::Value* r = GetStubFunction()->getArg(x_coroutineCtx);
        ReleaseAssert(llvm_value_has_type<uint64_t*>(r));
        return r;
    }

    llvm::Value* GetStackBase()
    {
        llvm::Value* r = GetStubFunction()->getArg(x_stackBase);
        ReleaseAssert(llvm_value_has_type<uint64_t*>(r));
        return r;
    }

    llvm::Value* GetCurBytecode()
    {
        llvm::Value* r = GetStubFunction()->getArg(x_curBytecodeOrRetStart);
        ReleaseAssert(llvm_value_has_type<uint8_t*>(r));
        return r;
    }

    // TODO: CodeBlock should be another parameter in the interpreter function prototype, so we don't need to dereference a pointer
    //
    llvm::Value* GetCodeBlock()
    {
        using namespace llvm;
        if (m_codeBlockValue != nullptr)
        {
            return m_codeBlockValue;
        }

        LLVMContext& ctx = GetLLVMContext();
        Value* coroutineCtx = GetCoroutineCtx();
        constexpr size_t offset = offsetof_member_v<&CoroutineRuntimeContext::m_codeBlock>;
        static_assert(offset % 8 == 0);
        Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), coroutineCtx, { CreateLLVMConstantInt<uint64_t>(ctx, offset / 8) }, "", m_initBBForStubFunction);
        LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", m_initBBForStubFunction);
        bv->setAlignment(Align(8));
        CastInst* cb = CastInst::Create(Instruction::IntToPtr, bv, llvm_type_of<uint64_t*>(ctx), "", m_initBBForStubFunction);
        ReleaseAssert(llvm_value_has_type<uint64_t*>(cb));
        m_codeBlockValue = cb;
        return cb;
    }

    llvm::LLVMContext& GetLLVMContext() { ReleaseAssert(m_implFunction != nullptr); return m_implFunction->getContext(); }
    void SetStubFunction(llvm::Function* func) { m_stubFunction = func; }
    void SetImplFunction(llvm::Function* func) { m_implFunction = func; }
    llvm::Function* GetStubFunction() { ReleaseAssert(m_stubFunction != nullptr); return m_stubFunction; }
    llvm::Function* GetImplFunction() { ReleaseAssert(m_implFunction != nullptr); return m_implFunction; }

    void AppendOperand(BcOperand* operand);

    std::vector<BcOperand*>& Operands() { return m_operands; }

    void dump(std::stringstream& ss);

    size_t GetBytecodeStructLength()
    {
        ReleaseAssert(m_bytecodeStructLength != static_cast<size_t>(-1));
        return m_bytecodeStructLength;
    }

    void EmitStubFunction();

    void RunPostLLVMSimplificationTransforms();

private:
    std::string m_implFunctionName;
    llvm::Function* m_stubFunction;
    llvm::Function* m_implFunction;

    llvm::BasicBlock* m_allocaBBForStubFunction;
    llvm::BasicBlock* m_initBBForStubFunction;
    llvm::Value* m_codeBlockValue;  // nullptr if hasn't emitted yet

    std::vector<BcOperand*> m_operands;

    size_t m_bytecodeStructLength;
};

enum class BcOperandKind
{
    Slot,
    Constant,
    Literal,
    SpecializedLiteral
};

// The base class for a bytecode operand
//
class BcOperand
{
public:
    BcOperand(const std::string& name)
        : m_name(name)
        , m_operandOrdinal(static_cast<size_t>(-1))
        , m_parent(nullptr)
        , m_offsetInBytecodeStruct(static_cast<size_t>(-1))
        , m_sizeInBytecodeStruct(0)
    { }

    virtual ~BcOperand() { }

    virtual void dump(std::stringstream& ss) = 0;

    virtual BcOperandKind GetKind() = 0;

    // Return 0 if this operand is specialized to have a constant value so it doesn't have to sit in the bytecode struct
    // Otherwise, return the max # of bytes needed to represent this operand
    //
    virtual size_t WARN_UNUSED ValueByteLength() = 0;

    // Whether this operand should be treated as a signed value or an unsigned value
    //
    virtual bool WARN_UNUSED IsSignedValue() = 0;

    // The LLVM type that the implementation function expects for this operand
    //
    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext* ctx) = 0;

    // Get the LLVM value to be passed to the implementation function.
    // 'targetBB' is the basic block where the logic should be appended to.
    // 'bytecodeValue' is an i64 denoting the value of the operand in the bytecode struct (or void if it is eliminated from bytecode struct).
    //
    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) = 0;

    // Do custom transformation to the implementation function
    //
    virtual void TransformImplementationBeforeSimplificationPhase() { }
    virtual void TransformImplementationAfterSimplificationPhase() { }

    size_t OperandOrdinal() const { ReleaseAssert(m_operandOrdinal != static_cast<size_t>(-1)); return m_operandOrdinal; }
    void SetOperandOrdinal(size_t ordinal) { m_operandOrdinal = ordinal; }
    void AssertOrSetOperandOrdinal(size_t ordinal) { ReleaseAssert(m_operandOrdinal == static_cast<size_t>(-1) || m_operandOrdinal == ordinal); m_operandOrdinal = ordinal; }
    InterpreterFunction* GetParent() { ReleaseAssert(m_parent != nullptr); return m_parent; }
    void SetParent(InterpreterFunction* parent) { m_parent = parent; }
    void AssertOrSetParent(InterpreterFunction* parent) { ReleaseAssert(m_parent == nullptr || m_parent == parent); m_parent = parent; }

    std::string OperandName() { return m_name; }

    void InvalidateBytecodeStructOffsetAndSize()
    {
        m_offsetInBytecodeStruct = static_cast<size_t>(-1);
        m_sizeInBytecodeStruct = 0;
    }

    void AssignOrChangeBytecodeStructOffsetAndSize(size_t offset, size_t size)
    {
        size_t maxSize = ValueByteLength();
        ReleaseAssertIff(maxSize == 0, offset == static_cast<size_t>(-1));
        ReleaseAssertIff(maxSize == 0, size == 0);
        ReleaseAssert(size <= maxSize);
        m_offsetInBytecodeStruct = offset;
        m_sizeInBytecodeStruct = size;
    }

    // If m_offsetInBytecodeStruct == -1, return nullptr
    // Otherwise emit decoding logic into 'targetBB', and return the decoded operand value in the bytecode struct
    // 'bytecodeStruct' must have type i8*
    //
    llvm::Value* WARN_UNUSED GetOperandValueFromBytecodeStruct(llvm::BasicBlock* targetBB, llvm::Value* bytecodeStruct)
    {
        using namespace llvm;
        LLVMContext& ctx = GetParent()->GetLLVMContext();
        ReleaseAssert(llvm_value_has_type<uint8_t*>(bytecodeStruct));
        ReleaseAssertIff(m_offsetInBytecodeStruct == static_cast<size_t>(-1), ValueByteLength() == 0);
        if (m_offsetInBytecodeStruct == static_cast<size_t>(-1))
        {
            return nullptr;
        }
        ReleaseAssert(m_sizeInBytecodeStruct > 0 && m_sizeInBytecodeStruct <= ValueByteLength());

        GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), bytecodeStruct, { CreateLLVMConstantInt<uint64_t>(ctx, m_offsetInBytecodeStruct) }, "", targetBB);
        ReleaseAssert(llvm_value_has_type<uint8_t*>(gep));
        Type* storageTypeInBytecodeStruct = Type::getIntNTy(ctx, static_cast<uint32_t>(m_sizeInBytecodeStruct * 8));
        CastInst* castToStorageType = BitCastInst::CreatePointerBitCastOrAddrSpaceCast(gep, storageTypeInBytecodeStruct->getPointerTo(), "", targetBB);
        LoadInst* storageValue = new LoadInst(storageTypeInBytecodeStruct, castToStorageType, "", targetBB);

        Type* dstType = Type::getIntNTy(ctx, static_cast<uint32_t>(ValueByteLength() * 8));
        Value* result;
        if (IsSignedValue())
        {
            result = CastInst::CreateSExtOrBitCast(storageValue, dstType, "", targetBB);
        }
        else
        {
            result = CastInst::CreateZExtOrBitCast(storageValue, dstType, "", targetBB);
        }
        ReleaseAssert(result != nullptr);
        return result;
    }

private:
    std::string m_name;
    // The ordinal of this operand in the bytecode definition
    //
    size_t m_operandOrdinal;
    InterpreterFunction* m_parent;

    // The offset of this bytecode in the bytecode struct
    //
    size_t m_offsetInBytecodeStruct;
    size_t m_sizeInBytecodeStruct;
};

inline void InterpreterFunction::BindToModule(llvm::Module* module)
{
    using namespace llvm;
    m_implFunction = module->getFunction(m_implFunctionName);
    ReleaseAssert(m_implFunction != nullptr);

    m_stubFunction = nullptr;

    for (BcOperand* operand : m_operands)
    {
        operand->InvalidateBytecodeStructOffsetAndSize();
    }
    m_bytecodeStructLength = static_cast<size_t>(-1);
}

inline void InterpreterFunction::SetMaxOperandWidthBytes(size_t maxWidthBytes)
{
    ReleaseAssert(m_bytecodeStructLength == static_cast<size_t>(-1));
    size_t currentOffset = 0;
    for (BcOperand* operand : m_operands)
    {
        size_t operandMaxWidth = operand->ValueByteLength();
        if (operandMaxWidth == 0)
        {
            continue;
        }
        size_t width = std::min(operandMaxWidth, maxWidthBytes);
        operand->AssignOrChangeBytecodeStructOffsetAndSize(currentOffset, width);
        currentOffset += width;
    }
    m_bytecodeStructLength = currentOffset;
}

inline void InterpreterFunction::AppendOperand(BcOperand* operand)
{
    operand->AssertOrSetOperandOrdinal(m_operands.size());
    operand->AssertOrSetParent(this);
    m_operands.push_back(operand);
}

inline void InterpreterFunction::dump(std::stringstream& ss)
{
    // ss << "Implemented by: " << DemangleCXXSymbol(m_implFunction->getName().str()) << std::endl;
    for (BcOperand* op : m_operands)
    {
        op->dump(ss);
    }
}

inline void InterpreterFunction::EmitStubFunction()
{
    using namespace llvm;
    LLVMContext& ctx = GetLLVMContext();
    ReleaseAssert(m_implFunction != nullptr);
    ReleaseAssert(m_stubFunction == nullptr);

    FunctionType* fty = FunctionType::get(
        llvm_type_of<void>(ctx) /*result*/,
        {
            llvm_type_of<uint64_t*>(ctx) /*coroutineCtx*/,
            llvm_type_of<uint64_t*>(ctx) /*stackBase*/,
            llvm_type_of<uint8_t*>(ctx) /*bytecode*/,
            llvm_type_of<uint64_t>(ctx) /*unused*/
        } /*params*/,
        false /*isVarArg*/);

    Function* func = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, "deegen_bytecode_impl", m_implFunction->getParent());
    m_stubFunction = func;
    m_stubFunction->addFnAttr(Attribute::AttrKind::NoReturn);
    CopyFunctionAttributes(m_stubFunction /*dst*/, m_implFunction /*src*/);

    m_allocaBBForStubFunction = nullptr;
    m_initBBForStubFunction = nullptr;

    ReleaseAssert(m_bytecodeStructLength != static_cast<size_t>(-1));

    ReleaseAssert(m_stubFunction->empty());
    m_allocaBBForStubFunction = BasicBlock::Create(ctx, "", m_stubFunction);
    m_initBBForStubFunction = BasicBlock::Create(ctx, "", m_stubFunction);

    BasicBlock* logicEntry = BasicBlock::Create(ctx, "", m_stubFunction);
    BasicBlock* currentBlock = logicEntry;

    Value* bytecodeStruct = m_stubFunction->getArg(x_curBytecodeOrRetStart);
    std::vector<Value*> opcodeValues;
    for (BcOperand* operand : m_operands)
    {
        opcodeValues.push_back(operand->GetOperandValueFromBytecodeStruct(currentBlock, bytecodeStruct));
    }

    std::vector<Value*> usageValues;
    {
        size_t ord = 0;
        for (BcOperand* operand : m_operands)
        {
            usageValues.push_back(operand->EmitUsageValueFromBytecodeValue(currentBlock, opcodeValues[ord]));
            ord++;
        }
        ReleaseAssert(ord == opcodeValues.size() && ord == usageValues.size());
    }

    CallInst::Create(m_implFunction, usageValues, "", currentBlock);
    new UnreachableInst(ctx, currentBlock);

    BranchInst::Create(m_initBBForStubFunction, m_allocaBBForStubFunction);
    BranchInst::Create(logicEntry, m_initBBForStubFunction);

    ValidateLLVMFunction(m_stubFunction);
}

inline void InterpreterFunction::RunPostLLVMSimplificationTransforms()
{
    for (BcOperand* operand : m_operands)
    {
        operand->TransformImplementationAfterSimplificationPhase();
    }
}

// A bytecode operand that refers to a bytecode slot
//
class BcOpSlot final : public BcOperand
{
public:
    BcOpSlot(const std::string& name)
        : BcOperand(name)
    { }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': Slot" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::Slot; }

    virtual size_t WARN_UNUSED ValueByteLength() override
    {
        return 8;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return false;
    }

    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext* ctx) override
    {
        // TValue, which is i64
        //
        return llvm_type_of<uint64_t>(*ctx);
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override
    {
        using namespace llvm;
        LLVMContext& ctx = GetParent()->GetLLVMContext();
        Value* stackBase = GetParent()->GetStackBase();
        ReleaseAssert(llvm_value_has_type<int64_t>(bytecodeValue));
        Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { bytecodeValue }, "", targetBB);
        LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", targetBB);
        bv->setAlignment(Align(8));
        return bv;
    }
};

// A bytecode operand that refers to a constant in the constant table
//
class BcOpConstant final : public BcOperand
{
public:
    BcOpConstant(const std::string& name, TypeSpeculationMask mask)
        : BcOperand(name), m_typeMask(mask)
    { }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': Constant [ " << DumpHumanReadableTypeSpeculation(m_typeMask) << " ]" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::Constant; }

    virtual size_t WARN_UNUSED ValueByteLength() override
    {
        // TODO: this can be improved: if the type is known to have only one value, the constant can be eliminated
        //
        return 8;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        // The constant ordinal is always a negative value due to how we designed the in-memory representation
        //
        return true;
    }

    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext* ctx) override
    {
        // TValue, which is i64
        //
        return llvm_type_of<uint64_t>(*ctx);
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override
    {
        using namespace llvm;
        LLVMContext& ctx = GetParent()->GetLLVMContext();
        Value* codeBlock = GetParent()->GetCodeBlock();
        ReleaseAssert(llvm_value_has_type<int64_t>(bytecodeValue));
        Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), codeBlock, { bytecodeValue }, "", targetBB);
        LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", targetBB);
        bv->setAlignment(Align(8));
        return bv;
    }

// This is the old implementation which has been replaced by the better implementation in tvalue_typecheck_analysis.cpp
// We keep this code for now just for archival purpose
#if 0
    struct RemoveTypeChecksState
    {
        // This map maps from the argument to be specialized in the orginal function to the specialized new function
        // When the mapped value exists but is nullptr, it means the specialization is being created but not yet complete
        //
        std::unordered_map<llvm::Argument* /*argInOriginalFunc*/, llvm::Function* /*newFunction*/> specializedCalleeMap;
    };

    void RemoveTypeChecks(RemoveTypeChecksState& state /*inout*/, llvm::Argument* value)
    {
        using namespace llvm;

        LLVMContext& ctx = GetParent()->GetLLVMContext();

        // Put all actions in a queue and run in the end, to prevent iterator invalidation bugs
        //
        std::vector<std::function<void()>> actions;
        for (Use& u : value->uses())
        {
            if (CallInst* callInst = dyn_cast<CallInst>(u.getUser()))
            {
                Function* callee = callInst->getCalledFunction();
                if (callee == nullptr)
                {
                    continue;
                }
                TypeSpeculationMask mask;
                if (IsTValueTypeCheckAPIFunction(callee, &mask /*out*/))
                {
                    ReleaseAssert(callee->arg_size() == 1);
                    ReleaseAssert(callInst->isArgOperand(&u));
                    ReleaseAssert(callInst->getArgOperandNo(&u) == 0);
                    ReleaseAssert(callInst->getType() == llvm_type_of<bool>(ctx));

                    // We currently only optimize two cases:
                    // 1. If the checked 'mask' is a superset of our known type mask, then the check must succeed
                    // 2. If the checked 'mask' does not overlap with our known type mask, then the check must fail
                    //
                    // TODO: We could have done more, including:
                    // 1. There could be opportunity for strength reduction if 'mask' partially overlaps with the known type mask,
                    //    and for eliminating "else" branches based on already-failed type checks. We probably should improve it through some
                    //    sort of abstract interpretation to analyze the possible types of TValue at each point.
                    // 2. We currently don't do anything for PHI-Node-use of 'value', and rely on user to prevent this case by e.g., manually
                    //    rotate the loops. In theory we probably could have done something automatically.
                    //
                    if ((mask & m_typeMask) == m_typeMask)
                    {
                        // The check must evaluate to true
                        //
                        actions.push_back([callInst, &ctx]() {
                            ReplaceInstructionWithValue(callInst, CreateLLVMConstantInt(ctx, true));
                        });
                    }
                    else if ((mask & m_typeMask) == 0)
                    {
                        // The check must evaluate to false
                        //
                        actions.push_back([callInst, &ctx]() {
                            ReplaceInstructionWithValue(callInst, CreateLLVMConstantInt(ctx, false));
                        });
                    }
                }
                else
                {
                    ReleaseAssert(callInst->isArgOperand(&u));
                    uint32_t argNo = callInst->getArgOperandNo(&u);
                    Argument* argInCallee = callee->getArg(argNo);
                    // We need to clone the function for each specialization
                    //
                    if (state.specializedCalleeMap.count(argInCallee))
                    {
                        // Currently we don't expect or support user code to be recursive
                        // Note that this lockdown is only best-effort, not complete: we cannot reliably report all recursive cases,
                        // and we could generate faulty code due to that, but it should be fine for now.
                        //
                        if (state.specializedCalleeMap[argInCallee] == nullptr)
                        {
                            fprintf(stderr, "Recursion is unsupported yet.\n");
                            abort();
                        }
                        // The specialized clone for the desired <Function, Arg> combination has been done.
                        // So all we need to do is to change the callee to the specialized implementation
                        //
                        callInst->setCalledFunction(state.specializedCalleeMap[argInCallee]);
                    }
                    else
                    {
                        // Assign a nullptr value to indiate that the specialization is being done but not complete.
                        // While the specialization is being done, if this argument is being searched upon again,
                        // it means there is a recursion case, so the lockdown in the above branch will be triggered
                        //
                        state.specializedCalleeMap[argInCallee] = nullptr;
                        ValueToValueMapTy vtvm;
                        Function* clonedFunction = CloneFunction(callee, vtvm /*inout*/);
                        ReleaseAssert(clonedFunction != nullptr);
                        ReleaseAssert(clonedFunction->arg_size() == callee->arg_size());

                        RemoveTypeChecks(state /*inout*/, clonedFunction->getArg(argNo));

                        // Now the specialized callee is created, store it in the map so future invocations to the same
                        // specialization can reuse this function
                        //
                        state.specializedCalleeMap[argInCallee] = clonedFunction;
                        callInst->setCalledFunction(clonedFunction);
                        clonedFunction->addFnAttr(Attribute::AttrKind::AlwaysInline);
                    }
                }
            }
        }

        for (auto& action : actions)
        {
            action();
        }
    }

    virtual void TransformImplementationAfterSimplificationPhase() override
    {
        using namespace llvm;
        Function* func = GetParent()->GetImplFunction();
        ReleaseAssert(func != nullptr);
        Argument* arg = func->getArg(static_cast<uint32_t>(OperandOrdinal()));
        RemoveTypeChecksState state;
        state.specializedCalleeMap[arg] = nullptr;
        RemoveTypeChecks(state /*inout*/, arg);
    }
#endif

    // The statically-known type mask of this constant
    //
    TypeSpeculationMask m_typeMask;
};

// A bytecode operand that is a literal integer
//
class BcOpLiteral : public BcOperand
{
public:
    BcOpLiteral(const std::string& name, bool isSigned, size_t numBytes)
        : BcOperand(name), m_isSigned(isSigned), m_numBytes(numBytes)
    { }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': Literal [ " << (m_isSigned ? "" : "u") << "int_" << m_numBytes * 8 << " ]" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::Literal; }

    virtual bool WARN_UNUSED IsSpecializedToConcreteValue()
    {
        return false;
    }

    virtual size_t WARN_UNUSED ValueByteLength() override
    {
        return m_numBytes;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return m_isSigned;
    }

    // The sign and width of this literal
    //
    bool m_isSigned;
    size_t m_numBytes;
};

class BcOpSpecializedLiteral : public BcOpLiteral
{
public:
    BcOpSpecializedLiteral(const std::string& name, bool isSigned, size_t numBytes, uint64_t concreteValue)
        : BcOpLiteral(name, isSigned, numBytes), m_concreteValue(concreteValue)
    {
        ReleaseAssert(is_power_of_2(numBytes));
        if (numBytes < 8)
        {
            if (isSigned)
            {
                ReleaseAssert((1LL << (numBytes * 8 - 1)) <= static_cast<int64_t>(concreteValue));
                ReleaseAssert(static_cast<int64_t>(concreteValue) < (1LL << (numBytes * 8 - 1)));
            }
            else
            {
                ReleaseAssert(concreteValue < (1LL << (numBytes * 8)));
            }
        }
    }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': Literal [ value = ";
        if (m_isSigned)
        {
            ss << static_cast<int64_t>(m_concreteValue);
        }
        else
        {
            ss << static_cast<uint64_t>(m_concreteValue);
        }
        ss << " ]" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::SpecializedLiteral; }

    virtual bool WARN_UNUSED IsSpecializedToConcreteValue() override
    {
        return true;
    }

    virtual size_t WARN_UNUSED ValueByteLength() override
    {
        // This is a specialized constant, so it doesn't have to live in the bytecode struct
        //
        return 0;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return m_isSigned;
    }

    uint64_t m_concreteValue;
};

struct BytecodeVariantDefinition
{
    BytecodeVariantDefinition(size_t bytecodeOrdInTU, size_t variantOrd, const std::string& bytecodeName, const std::vector<std::string>& opNames, const std::function<llvm::Module*()>& moduleCloner, const std::string& implFuncName)
        : m_bytecodeOrdInTU(bytecodeOrdInTU)
        , m_variantOrd(variantOrd)
        , m_bytecodeName(bytecodeName)
        , m_opNames(opNames)
        , m_moduleCloner(moduleCloner)
        , m_currentModule(nullptr)
        , m_ifunc(implFuncName)
    { }

    void dump(std::stringstream& ss)
    {
        ss << "Bytecode " << m_bytecodeName << " [ Variant " << m_variantOrd << " ]" << std::endl;
        m_ifunc.dump(ss);
    }

    void CreateInterpreterFunctionForMaxOperandWidthBytes(size_t maxOperandWidthBytes)
    {
        m_currentModule = m_moduleCloner();
        m_ifunc.BindToModule(m_currentModule);
        m_ifunc.SetMaxOperandWidthBytes(maxOperandWidthBytes);
        m_ifunc.EmitStubFunction();
    }

    size_t m_bytecodeOrdInTU;
    size_t m_variantOrd;
    std::string m_bytecodeName;
    std::vector<std::string> m_opNames;
    std::function<llvm::Module*()> m_moduleCloner;
    llvm::Module* m_currentModule;
    InterpreterFunction m_ifunc;
};

struct DeegenBytecodeDefinitionParser
{
    static std::vector<BytecodeVariantDefinition*> WARN_UNUSED ParseList(std::function<llvm::Module*()> loadModuleFn)
    {
        using namespace llvm;
        using Desc = DeegenFrontendBytecodeDefinitionDescriptor;
        using SpecializedOperand = DeegenFrontendBytecodeDefinitionDescriptor::SpecializedOperand;
        Module* module = loadModuleFn();
        constexpr const char* defListSymbolName = "x_deegen_impl_all_bytecode_defs_in_this_tu";
        constexpr const char* nameListSymbolName = "x_deegen_impl_all_bytecode_names_in_this_tu";
        if (module->getGlobalVariable(defListSymbolName) == nullptr)
        {
            ReleaseAssert(module->getGlobalVariable(nameListSymbolName) == nullptr);
            return {};
        }

        Constant* defList;
        {
            Constant* wrappedDefList = GetConstexprGlobalValue(module, defListSymbolName);
            LLVMConstantStructReader reader(module, wrappedDefList);
            defList = reader.Dewrap();
        }

        LLVMConstantArrayReader defListReader(module, defList);
        uint64_t numBytecodesInThisTU = defListReader.GetNumElements<Desc>();

        std::vector<std::string> bytecodeNamesInThisTU;
        {
            Constant* wrappedNameList = GetConstexprGlobalValue(module, nameListSymbolName);
            LLVMConstantStructReader readerTmp(module, wrappedNameList);
            Constant* nameList = readerTmp.Dewrap();
            LLVMConstantArrayReader reader(module, nameList);
            ReleaseAssert(reader.GetNumElements<uint8_t*>() == numBytecodesInThisTU);
            for (size_t i = 0; i < numBytecodesInThisTU; i++)
            {
                Constant* cst = reader.Get<uint8_t*>(i);
                bytecodeNamesInThisTU.push_back(GetValueFromLLVMConstantCString(cst));
            }
        }

        auto readSpecializedOperand = [&](Constant* cst) -> SpecializedOperand
        {
            LLVMConstantStructReader spOperandReader(module, cst);
            auto kind = spOperandReader.GetValue<&SpecializedOperand::m_kind>();
            auto value = spOperandReader.GetValue<&SpecializedOperand::m_value>();
            return SpecializedOperand { kind, value };
        };

        std::vector<BytecodeVariantDefinition*> result;
        for (size_t curBytecodeOrd = 0; curBytecodeOrd < numBytecodesInThisTU; curBytecodeOrd++)
        {
            LLVMConstantStructReader curDefReader(module, defListReader.Get<Desc>(curBytecodeOrd));
            ReleaseAssert(curDefReader.GetValue<&Desc::m_operandTypeListInitialized>() == true);
            ReleaseAssert(curDefReader.GetValue<&Desc::m_implementationInitialized>() == true);
            size_t numVariants = curDefReader.GetValue<&Desc::m_numVariants>();
            size_t numOperands = curDefReader.GetValue<&Desc::m_numOperands>();
            LLVMConstantArrayReader operandListReader(module, curDefReader.Get<&Desc::m_operandTypes>());
            ReleaseAssert(operandListReader.GetNumElements<Desc::Operand>() == Desc::x_maxOperands);

            std::vector<std::string> operandNames;
            std::vector<DeegenBytecodeOperandType> operandTypes;
            for (size_t i = 0; i < numOperands; i++)
            {
                LLVMConstantStructReader operandReader(module, operandListReader.Get<Desc::Operand>(i));
                std::string operandName = GetValueFromLLVMConstantCString(operandReader.Get<&Desc::Operand::m_name>());
                DeegenBytecodeOperandType opType = operandReader.GetValue<&Desc::Operand::m_type>();
                operandNames.push_back(operandName);
                operandTypes.push_back(opType);
            }

            std::string implFuncName;
            {
                Constant* implFunc = curDefReader.Get<&Desc::m_implementationFn>();
                ConstantExpr* bitCastExpr = dyn_cast<ConstantExpr>(implFunc);
                ReleaseAssert(bitCastExpr != nullptr);
                ReleaseAssert(bitCastExpr->getOpcode() == Instruction::BitCast);
                Constant* bitCastOperand = bitCastExpr->getOperand(0);
                Function* fnc = dyn_cast<Function>(bitCastOperand);
                ReleaseAssert(fnc != nullptr);
                implFuncName = fnc->getName().str();
            }

            LLVMConstantArrayReader variantListReader(module, curDefReader.Get<&Desc::m_variants>());
            for (size_t variantOrd = 0; variantOrd < numVariants; variantOrd++)
            {
                BytecodeVariantDefinition* def = new BytecodeVariantDefinition(
                    curBytecodeOrd /*bytecodeOrdInTU*/,
                    variantOrd /*variantOrd*/,
                    bytecodeNamesInThisTU[curBytecodeOrd] /*bytecodeName*/,
                    operandNames,
                    loadModuleFn,
                    implFuncName);

                result.push_back(def);
                InterpreterFunction* ifunc = &def->m_ifunc;

                LLVMConstantStructReader variantReader(module, variantListReader.Get<Desc::SpecializedVariant>(variantOrd));
                LLVMConstantArrayReader baseReader(module, variantReader.Get<&Desc::SpecializedVariant::m_base>());
                for (size_t opOrd = 0; opOrd < numOperands; opOrd++)
                {
                    SpecializedOperand spOp = readSpecializedOperand(baseReader.Get<SpecializedOperand>(opOrd));
                    DeegenBytecodeOperandType opType = operandTypes[opOrd];
                    std::string operandName = operandNames[opOrd];
                    if (opType == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                    {
                        if (spOp.m_kind == DeegenSpecializationKind::BytecodeSlot)
                        {
                            ifunc->AppendOperand(new BcOpSlot(operandName));
                        }
                        else
                        {
                            ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::BytecodeConstantWithType && "unexpected specialization");
                            ifunc->AppendOperand(new BcOpConstant(operandName, SafeIntegerCast<TypeSpeculationMask>(spOp.m_value)));
                        }
                    }
                    else if (opType == DeegenBytecodeOperandType::BytecodeSlot)
                    {
                        ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::NotSpecialized && "unexpected specialization");
                        ifunc->AppendOperand(new BcOpSlot(operandName));
                    }
                    else if (opType == DeegenBytecodeOperandType::Constant)
                    {
                        TypeSpeculationMask specMask;
                        if (spOp.m_kind == DeegenSpecializationKind::NotSpecialized)
                        {
                            specMask = x_typeSpeculationMaskFor<tTop>;
                        }
                        else if (spOp.m_kind == DeegenSpecializationKind::BytecodeConstantWithType)
                        {
                            specMask = SafeIntegerCast<TypeSpeculationMask>(spOp.m_value);
                        }
                        else
                        {
                            ReleaseAssert(false && "unexpected specialization");
                        }
                        ifunc->AppendOperand(new BcOpConstant(operandName, specMask));
                    }
                    else
                    {
                        ReleaseAssert(false && "unimplemented");
                    }
                }
            }
        }
        return result;
    }
};

}   // namespace dast

#include "misc_llvm_helper.h"

#include "bytecode_definition_utils.h"

#include "llvm/IR/GlobalValue.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Pass.h"

// TODO: fix this
#include "bytecode.h"

namespace dast
{

class BcOperand;

// A LLVM function that implements a bytecode
//
class InterpreterFunction
{
public:
    InterpreterFunction(llvm::Function* implFunction);

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
        Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t*>(ctx), coroutineCtx, { ConstantInt::get(llvm_type_of<int64_t>(ctx), offset / 8, false /*isSigned*/ ) }, "", m_initBBForStubFunction);
        LoadInst* bv = new LoadInst(llvm_type_of<uint64_t*>(ctx), bvPtr, "", m_initBBForStubFunction);
        bv->setAlignment(Align(8));
        ReleaseAssert(llvm_value_has_type<uint64_t*>(bv));
        m_codeBlockValue = bv;
        return bv;
    }

    llvm::LLVMContext& GetLLVMContext() { ReleaseAssert(m_implFunction != nullptr); return m_implFunction->getContext(); }
    void SetStubFunction(llvm::Function* func) { m_stubFunction = func; }
    void SetImplFunction(llvm::Function* func) { m_implFunction = func; }
    llvm::Function* GetStubFunction() { ReleaseAssert(m_stubFunction != nullptr); return m_stubFunction; }
    llvm::Function* GetImplFunction() { ReleaseAssert(m_implFunction != nullptr); return m_implFunction; }

    void AppendOperand(BcOperand* operand);

private:
    llvm::Function* m_stubFunction;
    llvm::Function* m_implFunction;

    llvm::BasicBlock* m_allocaBBForStubFunction;
    llvm::BasicBlock* m_initBBForStubFunction;
    llvm::Value* m_codeBlockValue;  // nullptr if hasn't emitted yet

    std::vector<BcOperand*> m_operands;
};

inline InterpreterFunction::InterpreterFunction(llvm::Function* implFunction)
    : m_stubFunction(nullptr)
    , m_implFunction(implFunction)
    , m_allocaBBForStubFunction(nullptr)
    , m_initBBForStubFunction(nullptr)
    , m_codeBlockValue(nullptr)
    , m_operands()
{
    using namespace llvm;
    LLVMContext& ctx = GetLLVMContext();
    FunctionType* fty = FunctionType::get(
        llvm_type_of<void>(ctx) /*result*/,
        {
            llvm_type_of<uint64_t*>(ctx) /*coroutineCtx*/,
            llvm_type_of<uint64_t*>(ctx) /*stackBase*/,
            llvm_type_of<uint8_t*>(ctx) /*bytecode*/,
            llvm_type_of<uint64_t>(ctx) /*unused*/
        } /*params*/,
        false /*isVarArg*/);

    Function* func = Function::Create(fty, GlobalValue::LinkageTypes::ExternalLinkage, "deegen_bytecode_impl", implFunction->getParent());
    m_stubFunction = func;

    m_allocaBBForStubFunction = BasicBlock::Create(ctx, "", func);
    m_initBBForStubFunction = BasicBlock::Create(ctx, "", func);
}

// The base class for a bytecode operand
//
class BcOperand
{
public:
    BcOperand() : m_operandOrdinal(static_cast<size_t>(-1)), m_parent(nullptr) { }

    virtual ~BcOperand() { }

    // Return true if this operand is specialized to have a constant value so it doesn't have to sit in the bytecode struct
    //
    virtual bool WARN_UNUSED MayBeEliminatedFromBytecodeStruct() = 0;

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
    virtual void TransformImplementationBeforeMem2Reg(llvm::Function* /*func*/) { }
    virtual void TransformImplementationAfterMem2Reg(llvm::Function* /*func*/) { }

    size_t OperandOrdinal() const { ReleaseAssert(m_operandOrdinal != static_cast<size_t>(-1)); return m_operandOrdinal; }
    void SetOperandOrdinal(size_t ordinal) { m_operandOrdinal = ordinal; }
    void AssertOrSetOperandOrdinal(size_t ordinal) { ReleaseAssert(m_operandOrdinal == static_cast<size_t>(-1) || m_operandOrdinal == ordinal); m_operandOrdinal = ordinal; }
    InterpreterFunction* GetParent() { ReleaseAssert(m_parent != nullptr); return m_parent; }
    void SetParent(InterpreterFunction* parent) { m_parent = parent; }
    void AssertOrSetParent(InterpreterFunction* parent) { ReleaseAssert(m_parent == nullptr || m_parent == parent); m_parent = parent; }

private:
    // The ordinal of this operand in the bytecode definition
    //
    size_t m_operandOrdinal;
    InterpreterFunction* m_parent;
};

inline void InterpreterFunction::AppendOperand(BcOperand* operand)
{
    operand->AssertOrSetOperandOrdinal(m_operands.size());
    operand->AssertOrSetParent(this);
    m_operands.push_back(operand);
}

// A bytecode operand that refers to a bytecode slot
//
class BcOpSlot final : public BcOperand
{
public:
    virtual bool WARN_UNUSED MayBeEliminatedFromBytecodeStruct() override
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
    BcOpConstant(TypeSpeculationMask mask)
        : BcOperand(), m_typeMask(mask)
    { }

    virtual bool WARN_UNUSED MayBeEliminatedFromBytecodeStruct() override
    {
        // TODO: this can be improved: if the type is known to have only one value, the constant can be eliminated
        //
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
        Value* codeBlock = GetParent()->GetCodeBlock();
        ReleaseAssert(llvm_value_has_type<int64_t>(bytecodeValue));
        Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), codeBlock, { bytecodeValue }, "", targetBB);
        LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", targetBB);
        bv->setAlignment(Align(8));
        return bv;
    }

    // The statically-known type mask of this constant
    //
    TypeSpeculationMask m_typeMask;
};

// A bytecode operand that is a literal integer
//
class BcOpLiteral : public BcOperand
{
public:
    virtual bool WARN_UNUSED IsSpecializedToConcreteValue()
    {
        return false;
    }

    virtual bool WARN_UNUSED MayBeEliminatedFromBytecodeStruct() override
    {
        return false;
    }

    // The sign and width of this literal
    //
    bool m_isSigned;
    size_t m_bitWidth;
};

class BcOpSpecializedLiteral : public BcOpLiteral
{
public:
    virtual bool WARN_UNUSED IsSpecializedToConcreteValue() override
    {
        return true;
    }

    virtual bool WARN_UNUSED MayBeEliminatedFromBytecodeStruct() override
    {
        return true;
    }


    uint64_t m_concreteValue;
};

struct BytecodeVariantDefinition
{
    BytecodeVariantDefinition(size_t bytecodeOrdInTU, size_t variantOrd)
        : m_bytecodeOrdInTU(bytecodeOrdInTU)
        , m_variantOrd(variantOrd)
        , m_module(nullptr)
        , m_func(nullptr)
    { }

    // Each BytecodeVariantDefinition must come from a separate clone of the LLVM module
    // This is why we have this two-step construction process:
    //     The first step (the constructor) only fill in m_bytecodeOrdInTU and m_variantOrd,
    //     which is sufficient to identify the BytecodeVariant in the module,
    //     and in the second step (this function) we make a clone of the module and actually populate everything.
    //
    void ParseFromModule(std::function<llvm::Module*()> loadModuleFn);

    size_t m_bytecodeOrdInTU;
    size_t m_variantOrd;
    std::string m_bytecodeName;
    std::vector<std::string> m_opNames;
    llvm::Module* m_module;
    InterpreterFunction* m_func;
};

struct DeegenBytecodeDefinitionParser
{
    static std::vector<BytecodeVariantDefinition*> WARN_UNUSED ParseList(llvm::Module* module)
    {
        using namespace llvm;
        using Desc = DeegenFrontendBytecodeDefinitionDescriptor;
        using SpecializedOperand = DeegenFrontendBytecodeDefinitionDescriptor::SpecializedOperand;
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
        printf("%d\n", int(numBytecodesInThisTU));

        auto readSpecializedOperand = [&](Constant* cst) -> SpecializedOperand
        {
            LLVMConstantStructReader spOperandReader(module, cst);
            auto kind = spOperandReader.GetValue<&SpecializedOperand::m_kind>();
            auto value = spOperandReader.GetValue<&SpecializedOperand::m_value>();
            return SpecializedOperand { kind, value };
        };

        for (size_t curBytecodeOrd = 0; curBytecodeOrd < numBytecodesInThisTU; curBytecodeOrd++)
        {
            LLVMConstantStructReader curDefReader(module, defListReader.Get<Desc>(curBytecodeOrd));
            ReleaseAssert(curDefReader.GetValue<&Desc::m_operandTypeListInitialized>() == true);
            ReleaseAssert(curDefReader.GetValue<&Desc::m_implementationInitialized>() == true);
            size_t numVariants = curDefReader.GetValue<&Desc::m_numVariants>();
            size_t numOperands = curDefReader.GetValue<&Desc::m_numOperands>();
            LLVMConstantArrayReader operandListReader(module, curDefReader.Get<&Desc::m_operandTypes>());
            ReleaseAssert(operandListReader.GetNumElements<Desc::Operand>() == Desc::x_maxOperands);

            printf("%d %d\n", int(numVariants), int(numOperands));
            std::vector<std::pair<std::string, DeegenBytecodeOperandType>> operandList;
            for (size_t i = 0; i < numOperands; i++)
            {
                LLVMConstantStructReader operandReader(module, operandListReader.Get<Desc::Operand>(i));
                std::string operandName = GetLLVMConstantString(operandReader.Get<&Desc::Operand::m_name>());
                printf("%s\n", operandName.c_str());
                DeegenBytecodeOperandType opType = operandReader.GetValue<&Desc::Operand::m_type>();
                printf("%d\n", static_cast<int>(opType));
                operandList.push_back(std::make_pair(operandName, opType));
            }

            LLVMConstantArrayReader variantListReader(module, curDefReader.Get<&Desc::m_variants>());
            for (size_t i = 0; i < numVariants; i++)
            {
                LLVMConstantStructReader variantReader(module, variantListReader.Get<Desc::SpecializedVariant>(i));
                LLVMConstantArrayReader baseReader(module, variantReader.Get<&Desc::SpecializedVariant::m_base>());
                for (size_t opOrd = 0; opOrd < numOperands; opOrd++)
                {
                    SpecializedOperand spOp = readSpecializedOperand(baseReader.Get<SpecializedOperand>(opOrd));
                    printf("%d %llu\n", static_cast<int>(spOp.m_kind), static_cast<unsigned long long>(spOp.m_value));
                }
            }
        }
        return {};
    }
};

}   // namespace dast

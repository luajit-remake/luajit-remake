#pragma once

#include "common_utils.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/CFG.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "cxx_symbol_demangler.h"
#include "deegen_desugaring_level.h"

namespace dast
{

template<typename T>
inline llvm::Type* WARN_UNUSED llvm_type_of(llvm::LLVMContext& ctx)
{
    using namespace llvm;
    if constexpr(std::is_same_v<T, void>)
    {
        return Type::getVoidTy(ctx);
    }
    else if constexpr(std::is_same_v<T, bool>)
    {
        // Note that despite that bool has type 'i1' (which we return from this function), it is stored in 'i8'
        //
        return IntegerType::get(ctx, 1 /*numBits*/);
    }
    else if constexpr(std::is_integral_v<T>)
    {
        return IntegerType::get(ctx, static_cast<unsigned>(sizeof(T)) * 8 /*numBits*/);
    }
    else if constexpr(std::is_same_v<T, float>)
    {
        return Type::getFloatTy(ctx);
    }
    else if constexpr(std::is_same_v<T, double>)
    {
        return Type::getDoubleTy(ctx);
    }
    else if constexpr(std::is_enum_v<T>)
    {
        return llvm_type_of<std::underlying_type_t<T>>(ctx);
    }
    else
    {
        static_assert(std::is_pointer_v<T>, "unhandled type");
        using PointeeType = std::remove_pointer_t<T>;
        // Special case:
        // (1) We store a bool value in i8. This is required to maintain compatibility with C++ ABI.
        //     So, type for 'bool' will be i1. But type for 'bool*' will be 'i8*', 'bool**' be 'i8**', etc
        // (2) void* has type i8* instead of void* in LLVM. (void** is i8**, etc).
        //     This is also required to maintain compatibility with C++ ABI.
        //
        if constexpr(std::is_same_v<PointeeType, bool> || std::is_same_v<PointeeType, void>)
        {
            return Type::getInt8PtrTy(ctx);
        }
        else
        {
            return llvm_type_of<PointeeType>(ctx)->getPointerTo();
        }
    }
}

template<typename T>
inline bool WARN_UNUSED llvm_type_has_type(llvm::Type* type)
{
    return llvm_type_of<T>(type->getContext()) == type;
}

template<typename T>
inline bool WARN_UNUSED llvm_value_has_type(llvm::Value* value)
{
    return llvm_type_has_type<T>(value->getType());
}

// For value that is directly forwarded (e.g., passed from the caller to the callee), clang generates the following pattern:
//   %0 = alloca T
//   store val, %0
//   %1 = load %0
//
// This function deals with (and can only handle) this simple pattern. Given %1, it returns 'val'.
//
inline llvm::Value* WARN_UNUSED LLVMBacktrackForSourceOfForwardedValue(llvm::LoadInst* loadInst)
{
    using namespace llvm;
    ReleaseAssert(loadInst != nullptr);
    Value* loadFrom = loadInst->getPointerOperand();
    AllocaInst* local = dyn_cast<AllocaInst>(loadFrom);
    ReleaseAssert(local != nullptr);

    // This function only deals with the simple case where the alloca is only used by is one LOAD and one STORE and nothing else,
    // so it should have exactly two users
    //
    {
        auto uit = local->user_begin();
        ReleaseAssert(uit != local->user_end());
        uit++;
        ReleaseAssert(uit != local->user_end());
        uit++;
        ReleaseAssert(uit == local->user_end());
    }

    User* u1 = *(local->user_begin());
    User* u2 = *(++local->user_begin());
    StoreInst* storeInst = dyn_cast<StoreInst>(u1);
    if (storeInst == nullptr)
    {
        ReleaseAssert(dyn_cast<LoadInst>(u1) == loadInst);
        storeInst = dyn_cast<StoreInst>(u2);
        ReleaseAssert(storeInst != nullptr);
    }
    else
    {
        ReleaseAssert(dyn_cast<LoadInst>(u2) == loadInst);
    }

    ReleaseAssert(storeInst->getPointerOperand() == local);
    Value* valStored = storeInst->getValueOperand();
    ReleaseAssert(valStored != nullptr);
    return valStored;
}

template<typename T>
T WARN_UNUSED GetValueOfLLVMConstantInt(llvm::Constant* value)
{
    using namespace llvm;
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    static_assert(sizeof(T) <= 8);
    ReleaseAssert(value != nullptr);
    if constexpr(std::is_same_v<T, bool>)
    {
        ReleaseAssert(value->getType() == llvm_type_of<uint8_t>(value->getContext()));
    }
    else
    {
        ReleaseAssert(value->getType() == llvm_type_of<T>(value->getContext()));
    }
    ConstantInt* ci = dyn_cast<ConstantInt>(value);
    ReleaseAssert(ci != nullptr);
    ReleaseAssert(ci->getBitWidth() == sizeof(T) * 8);
    if constexpr(std::is_enum_v<T>)
    {
        using U = std::underlying_type_t<T>;
        if constexpr(std::is_signed_v<U>)
        {
            return static_cast<T>(static_cast<U>(ci->getSExtValue()));
        }
        else
        {
            return static_cast<T>(static_cast<U>(ci->getZExtValue()));
        }
    }
    else
    {
        if constexpr(std::is_signed_v<T>)
        {
            return static_cast<T>(ci->getSExtValue());
        }
        else
        {
            uint64_t v = ci->getZExtValue();
            if constexpr(std::is_same_v<T, bool>)
            {
                ReleaseAssert(v == 0 || v == 1);
            }
            return static_cast<T>(v);
        }
    }
}

template<typename T>
bool WARN_UNUSED llvm_constant_has_value(llvm::Constant* cst, T expectedValue)
{
    return GetValueOfLLVMConstantInt<T>(cst) == expectedValue;
}

template<typename T>
llvm::ConstantInt* CreateLLVMConstantInt(llvm::LLVMContext& ctx, T value)
{
    static_assert(std::is_integral_v<T>);
    using namespace llvm;
    if constexpr(std::is_same_v<T, bool>)
    {
        return ConstantInt::get(ctx, APInt(1 /*numBits*/, static_cast<uint64_t>(value), false /*isSigned*/));
    }
    else
    {
        return ConstantInt::get(ctx, APInt(sizeof(T) * 8 /*numBits*/, static_cast<uint64_t>(value), std::is_signed_v<T> /*isSigned*/));
    }
}

class LLVMConstantStructReader
{
public:
    LLVMConstantStructReader(llvm::Module* module, llvm::Constant* value)
        : m_dataLayout(module)
    {
        using namespace llvm;
        ConstantStruct* cs = dyn_cast<ConstantStruct>(value);
        if (cs == nullptr)
        {
            ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(value);
            ReleaseAssert(caz != nullptr);
            m_isCaz = true;
        }
        else
        {
            m_isCaz = false;
        }
        StructType* ty = dyn_cast<StructType>(value->getType());
        ReleaseAssert(ty != nullptr);
        m_structLayout = m_dataLayout.getStructLayout(ty);
        m_value = value;
    }

    template<auto v>
    llvm::Constant* WARN_UNUSED Get()
    {
        using namespace llvm;
        static_assert(std::is_member_object_pointer_v<decltype(v)>);
        using C = class_type_of_member_object_pointer_t<decltype(v)>;
        using V = value_type_of_member_object_pointer_t<decltype(v)>;
        ReleaseAssert(sizeof(C) == m_structLayout->getSizeInBytes());
        constexpr size_t offset = offsetof_member_v<v>;
        static_assert(offset < sizeof(C));
        uint32_t element = m_structLayout->getElementContainingOffset(offset);
        ReleaseAssert(m_structLayout->getElementOffset(element) == offset);
        Constant* result;
        if (!m_isCaz)
        {
            ConstantStruct* cs = dyn_cast<ConstantStruct>(m_value);
            ReleaseAssert(cs != nullptr);
            result = cs->getAggregateElement(element);
        }
        else
        {
            ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(m_value);
            ReleaseAssert(caz != nullptr);
            result = caz->getStructElement(element);
        }
        ReleaseAssert(result != nullptr);
        ReleaseAssert(m_dataLayout.getTypeAllocSize(result->getType()) == sizeof(V));
        return result;
    }

    template<auto v>
    value_type_of_member_object_pointer_t<decltype(v)> WARN_UNUSED GetValue()
    {
        static_assert(std::is_member_object_pointer_v<decltype(v)>);
        using V = value_type_of_member_object_pointer_t<decltype(v)>;
        static_assert(std::is_integral_v<V> || std::is_enum_v<V>);
        return GetValueOfLLVMConstantInt<V>(Get<v>());
    }

    // This handles the case where the Struct only contains one element, and returns that element
    //
    llvm::Constant* WARN_UNUSED Dewrap()
    {
        using namespace llvm;
        Constant* result;
        if (!m_isCaz)
        {
            ConstantStruct* cs = dyn_cast<ConstantStruct>(m_value);
            ReleaseAssert(cs != nullptr);
            ReleaseAssert(cs->getNumOperands() == 1);
            result = cs->getAggregateElement(0U /*element*/);
        }
        else
        {
            ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(m_value);
            ReleaseAssert(caz != nullptr);
            ReleaseAssert(caz->getElementCount().getKnownMinValue() == 1);
            result = caz->getStructElement(0);
        }
        ReleaseAssert(result != nullptr);
        return result;
    }

private:
    llvm::DataLayout m_dataLayout;
    const llvm::StructLayout* m_structLayout;
    llvm::Constant* m_value;
    bool m_isCaz;
};

// In LLVM a constant array is either a ConstantArray, a ConstantDataArray, a ConstantAggregateZero, or a ConstantAggregate
// holding a list of elements (due to Clang frontend quirks that we cannot stop), so we need to handle all cases
//
class LLVMConstantArrayReader
{
public:
    LLVMConstantArrayReader(llvm::Module* module, llvm::Constant* value)
        : m_dataLayout(module)
    {
        using namespace llvm;
        Type* ty = value->getType();
        StructType* st = dyn_cast<StructType>(ty);
        if (st != nullptr)
        {
            m_typeRepKind = TypeRepKind::Struct;
            m_structLayout = m_dataLayout.getStructLayout(st);
            ConstantStruct* cs = dyn_cast<ConstantStruct>(value);
            if (cs != nullptr)
            {
                m_valueRepKind = ValueRepKind::Normal;
            }
            else
            {
                ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(value);
                ReleaseAssert(caz != nullptr);
                m_valueRepKind = ValueRepKind::AggregateZero;
            }
        }
        else
        {
            ReleaseAssert(dyn_cast<ArrayType>(ty) != nullptr);
            m_typeRepKind = TypeRepKind::Array;
            ConstantArray* ca = dyn_cast<ConstantArray>(value);
            if (ca != nullptr)
            {
                m_valueRepKind = ValueRepKind::Normal;
            }
            else
            {
                ConstantDataArray* cda = dyn_cast<ConstantDataArray>(value);
                if (cda != nullptr)
                {
                    m_valueRepKind = ValueRepKind::DataArray;
                }
                else
                {
                    ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(value);
                    ReleaseAssert(caz != nullptr);
                    m_valueRepKind = ValueRepKind::AggregateZero;
                }
            }
        }
        m_value = value;
    }

    template<typename T>
    llvm::Constant* WARN_UNUSED Get(size_t index)
    {
        using namespace llvm;
        llvm::Constant* result = nullptr;
        if (m_typeRepKind == TypeRepKind::Struct)
        {
            StructType* st = dyn_cast<StructType>(m_value->getType());
            ReleaseAssert(st != nullptr);
            constexpr size_t elementSize = sizeof(T);
            size_t offset = elementSize * index;
            ReleaseAssert(m_dataLayout.getTypeAllocSize(st) > offset);
            uint32_t element = m_structLayout->getElementContainingOffset(offset);
            Type* ty = st->getElementType(element);
            size_t tySize = m_dataLayout.getTypeAllocSize(ty);
            if (tySize == elementSize)
            {
                ReleaseAssert(m_structLayout->getElementOffset(element) == offset);
                if (m_valueRepKind == ValueRepKind::Normal)
                {
                    ConstantStruct* cs = dyn_cast<ConstantStruct>(m_value);
                    ReleaseAssert(cs != nullptr);
                    result = cs->getAggregateElement(element);
                }
                else
                {
                    ReleaseAssert(m_valueRepKind == ValueRepKind::AggregateZero);
                    ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(m_value);
                    result = caz->getStructElement(element);
                }
            }
            else
            {
                ReleaseAssert(tySize % elementSize == 0);

                size_t offsetBaseOfSubArray = m_structLayout->getElementOffset(element);
                ReleaseAssert(offsetBaseOfSubArray <= offset && offset < offsetBaseOfSubArray + tySize);
                ReleaseAssert(offsetBaseOfSubArray % elementSize == 0);
                size_t offsetInSubArray = offset - offsetBaseOfSubArray;
                ReleaseAssert(offsetInSubArray % elementSize == 0);
                size_t ordInSubArray = offsetInSubArray / elementSize;

                if (m_valueRepKind == ValueRepKind::Normal)
                {
                    ConstantStruct* cs = dyn_cast<ConstantStruct>(m_value);
                    ReleaseAssert(cs != nullptr);

                    Constant* subarrayElement = cs->getAggregateElement(element);
                    ConstantArray* ca = dyn_cast<ConstantArray>(subarrayElement);
                    if (ca != nullptr)
                    {
                        ReleaseAssert(m_dataLayout.getTypeAllocSize(ca->getType()->getElementType()) == elementSize);
                        result = ca->getAggregateElement(SafeIntegerCast<uint32_t>(ordInSubArray));
                    }
                    else
                    {
                        ConstantDataArray* cda = dyn_cast<ConstantDataArray>(subarrayElement);
                        if (cda != nullptr)
                        {
                            result = cda->getElementAsConstant(SafeIntegerCast<uint32_t>(ordInSubArray));
                        }
                        else
                        {
                            ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(subarrayElement);
                            ReleaseAssert(caz != nullptr);
                            result = caz->getElementValue(SafeIntegerCast<uint32_t>(ordInSubArray));
                        }
                    }
                }
                else
                {
                    ReleaseAssert(m_valueRepKind == ValueRepKind::AggregateZero);
                    ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(m_value);
                    ReleaseAssert(caz != nullptr);

                    Constant* subarrayElement = caz->getAggregateElement(element);
                    ConstantAggregateZero* subarrayCaz = dyn_cast<ConstantAggregateZero>(subarrayElement);
                    ReleaseAssert(subarrayCaz != nullptr);
                    result = subarrayCaz->getElementValue(SafeIntegerCast<uint32_t>(ordInSubArray));
                }
            }
        }
        else
        {
            ReleaseAssert(m_typeRepKind == TypeRepKind::Array);
            ArrayType* art = dyn_cast<ArrayType>(m_value->getType());
            ReleaseAssert(art != nullptr);

            uint64_t numElements = art->getNumElements();
            ReleaseAssert(index < numElements);
            ReleaseAssert(m_dataLayout.getTypeAllocSize(art->getElementType()) == sizeof(T));

            if (m_valueRepKind == ValueRepKind::Normal)
            {
                ConstantArray* ca = dyn_cast<ConstantArray>(m_value);
                ReleaseAssert(ca != nullptr);
                result = ca->getAggregateElement(SafeIntegerCast<uint32_t>(index));
            }
            else if (m_valueRepKind == ValueRepKind::DataArray)
            {
                ConstantDataArray* cda = dyn_cast<ConstantDataArray>(m_value);
                ReleaseAssert(cda != nullptr);
                result = cda->getElementAsConstant(SafeIntegerCast<uint32_t>(index));
            }
            else
            {
                ReleaseAssert(m_valueRepKind == ValueRepKind::AggregateZero);
                ConstantAggregateZero* caz = dyn_cast<ConstantAggregateZero>(m_value);
                ReleaseAssert(caz != nullptr);
                result = caz->getElementValue(SafeIntegerCast<uint32_t>(index));
            }
        }
        ReleaseAssert(result != nullptr);
        ReleaseAssert(m_dataLayout.getTypeAllocSize(result->getType()) == sizeof(T));
        return result;
    }

    template<typename T>
    T WARN_UNUSED GetValue(size_t index)
    {
        static_assert(std::is_integral_v<T>);
        return GetValueOfLLVMConstantInt<T>(Get<T>(index));
    }

    template<typename T>
    size_t GetNumElements()
    {
        size_t totalSize = m_dataLayout.getTypeAllocSize(m_value->getType());
        ReleaseAssert(totalSize % sizeof(T) == 0);
        return totalSize / sizeof(T);
    }

private:
    enum class TypeRepKind
    {
        Struct,
        Array
    };
    enum class ValueRepKind
    {
        Normal,
        AggregateZero,
        DataArray /* only possible for Array */
    };
    TypeRepKind m_typeRepKind;
    ValueRepKind m_valueRepKind;
    llvm::DataLayout m_dataLayout;
    llvm::Constant* m_value;
    // Only filled if TypeRepKind == Struct
    //
    const llvm::StructLayout* m_structLayout;
};

// We currently only support two representations: strings directly represented as an array of i8,
// and strings represented as i8* GetElementPtr (some_global, 0, 0)
//
inline std::string GetValueFromLLVMConstantCString(llvm::Constant* value)
{
    using namespace llvm;
    ConstantDataArray* cda = dyn_cast<ConstantDataArray>(value);
    std::string r;
    if (cda != nullptr)
    {
        ReleaseAssert(llvm_type_has_type<uint8_t>(cda->getType()->getElementType()));
        r = cda->getAsString().str();
    }
    else
    {
        ConstantExpr* expr = dyn_cast<ConstantExpr>(value);
        ReleaseAssert(expr != nullptr);
        ReleaseAssert(expr->getOpcode() == Instruction::GetElementPtr);
        ReleaseAssert(expr->getNumOperands() == 3);
        ReleaseAssert(llvm_constant_has_value<int>(expr->getOperand(1), 0));
        ReleaseAssert(llvm_constant_has_value<int>(expr->getOperand(2), 0));
        Constant* cst = expr->getOperand(0);
        GlobalVariable* gv = dyn_cast<GlobalVariable>(cst);
        ReleaseAssert(gv != nullptr);
        ReleaseAssert(gv->isConstant());
        Constant* gvi = gv->getInitializer();
        cda = dyn_cast<ConstantDataArray>(gvi);
        ReleaseAssert(cda != nullptr);
        ReleaseAssert(llvm_type_has_type<uint8_t>(cda->getType()->getElementType()));
        r = cda->getAsString().str();
    }
    ReleaseAssert(r.length() > 0 && r[r.length() - 1] == '\0');
    for (size_t i = 0; i < r.length() - 1; i++)
    {
        ReleaseAssert(r[i] != '\0');
    }
    r = r.substr(0, r.length() - 1);
    return r;
}

inline llvm::Constant* GetConstexprGlobalValue(llvm::Module* module, std::string name)
{
    using namespace llvm;
    GlobalVariable* gv = module->getGlobalVariable(name);
    ReleaseAssert(gv != nullptr);
    ReleaseAssert(gv->isConstant());
    return gv->getInitializer();
}

inline void RunLLVMOptimizePass(llvm::Module* module)
{
    using namespace llvm;

    PassBuilder passBuilder;
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    ModulePassManager MPM;

    passBuilder.registerModuleAnalyses(MAM);
    passBuilder.registerCGSCCAnalyses(CGAM);
    passBuilder.registerFunctionAnalyses(FAM);
    passBuilder.registerLoopAnalyses(LAM);
    passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    MPM = passBuilder.buildPerModuleDefaultPipeline(OptimizationLevel::O3);

    ReleaseAssert(module != nullptr);
    MPM.run(*module, MAM);
}

// In LLVM, a function A may be inlined into a function B only if B's target-features
// is a superset of that of A. (This is because B may be calling into A only after checking
// the CPU features needed by A exists. If A were inlined into B, the logic of A may be moved
// around by optimizer to outside of the CPU feature check, resulting in a bug).
//
// Fortunately, since our IR will be run on the same CPU as the cpp source files, we can simply
// copy the target features from a Clang-generated function definition.
//
// We also additionally copy a bunch of other attributes just to avoid other potential surprises
// like the above one that prevents inlining.
//
inline void CopyFunctionAttributes(llvm::Function* dstFunc, llvm::Function* srcFunc)
{
    using namespace llvm;
    constexpr const char* featuresToCopy[] = {
        "target-cpu",
        "target-features",
        "tune-cpu",
        "frame-pointer",
        "min-legal-vector-width",
        "no-trapping-math",
        "stack-protector-buffer-size"
    };
    constexpr size_t numFeatures = std::extent_v<decltype(featuresToCopy), 0 /*dimension*/>;
    for (size_t i = 0; i < numFeatures; i++)
    {
        const char* feature = featuresToCopy[i];
        ReleaseAssert(!dstFunc->hasFnAttribute(feature));
        ReleaseAssert(srcFunc->hasFnAttribute(feature));
        Attribute attr = srcFunc->getFnAttribute(feature);
        dstFunc->addFnAttr(attr);
        ReleaseAssert(dstFunc->hasFnAttribute(feature));
    }
}

inline void ValidateLLVMFunction(llvm::Function* func)
{
    using namespace llvm;
    raw_fd_ostream tmp(STDERR_FILENO, false /*shouldClose*/, true /*unbuffered*/);
    ReleaseAssert(verifyFunction(*func, &tmp) == false);
}

inline void ValidateLLVMModule(llvm::Module* module)
{
    using namespace llvm;
    raw_fd_ostream tmp(STDERR_FILENO, false /*shouldClose*/, true /*unbuffered*/);
    ReleaseAssert(verifyModule(*module, &tmp) == false);
}

// Each call on the same module may only increase DesugaringLevel
//
void DesugarAndSimplifyLLVMModule(llvm::Module* module, DesugaringLevel level);

// Remove anything unrelated to the specified function:
// function bodies of all other functions are dropped, then all unreferenced symbols are removed.
//
// Note that this creates a new module, and invalidates any reference to the old module.
//
void ExtractFunction(llvm::Module*& module /*inout*/, std::string functionName);

inline void ReplaceInstructionWithValue(llvm::Instruction* inst, llvm::Value* value)
{
    ReleaseAssert(inst->getType() == value->getType());
    using namespace llvm;
    BasicBlock::iterator BI(inst);
    ReplaceInstWithValue(inst->getParent()->getInstList(), BI, value);
}

}   // namespace dast

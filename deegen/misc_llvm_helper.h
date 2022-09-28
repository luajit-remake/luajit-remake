#pragma once

#include "common_utils.h"
#include "heap_ptr_utils.h"

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
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

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
        if constexpr(IsHeapPtrType<T>::value)
        {
            return PointerType::get(ctx, CLANG_ADDRESS_SPACE_IDENTIFIER_FOR_HEAP_PTR);
        }
        else
        {
            return PointerType::getUnqual(ctx);
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
        ReleaseAssert(value->getType() == llvm_type_of<uint8_t>(value->getContext()) || value->getType() == llvm_type_of<bool>(value->getContext()));
    }
    else
    {
        ReleaseAssert(value->getType() == llvm_type_of<T>(value->getContext()));
    }
    ConstantInt* ci = dyn_cast<ConstantInt>(value);
    ReleaseAssert(ci != nullptr);
    if constexpr(std::is_same_v<T, bool>)
    {
        ReleaseAssert(ci->getBitWidth() == 1 || ci->getBitWidth() == 8);
    }
    else
    {
        ReleaseAssert(ci->getBitWidth() == sizeof(T) * 8);
    }
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
T WARN_UNUSED GetValueOfLLVMConstantInt(llvm::Value* value)
{
    using namespace llvm;
    Constant* cst = dyn_cast<Constant>(value);
    ReleaseAssert(cst != nullptr);
    return GetValueOfLLVMConstantInt<T>(cst);
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

    {
        GlobalVariable* gv = dyn_cast<GlobalVariable>(value);
        if (gv != nullptr)
        {
            ReleaseAssert(gv->isConstant());
            return GetValueFromLLVMConstantCString(gv->getInitializer());
        }
    }

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
    ReleaseAssert(gv->getInitializer() != nullptr);
    return gv->getInitializer();
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

inline void RunLLVMOptimizePass(llvm::Module* module)
{
    using namespace llvm;

    ValidateLLVMModule(module);

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

    ValidateLLVMModule(module);
}

inline void RunLLVMModuleSimplificationAndInliningPipeline(llvm::Module* module)
{
    using namespace llvm;

    ValidateLLVMModule(module);

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

    MPM = passBuilder.buildModuleSimplificationPipeline(OptimizationLevel::O3, ThinOrFullLTOPhase::None);

    ReleaseAssert(module != nullptr);
    MPM.run(*module, MAM);

    ValidateLLVMModule(module);
}

// Compile an LLVM module to assembly (.S) file
//
std::string WARN_UNUSED CompileLLVMModuleToAssemblyFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel);

// Compile an LLVM module to ELF object (.o) file
//
std::string WARN_UNUSED CompileLLVMModuleToElfObjectFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel);

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

// Each call on the same module may only increase DesugaringLevel
//
void DesugarAndSimplifyLLVMModule(llvm::Module* module, DesugaringLevel level);

// Remove anything unrelated to the specified function:
// function bodies of all other functions are dropped, then all unreferenced symbols are removed.
//
// Note that this creates a new module (which is returned). The old module is untouched.
//
// 'ignoreLinkageIssues = true' should only be used in unit tests.
//
std::unique_ptr<llvm::Module> WARN_UNUSED ExtractFunction(llvm::Module* module, std::string functionName, bool ignoreLinkageIssues = false);
std::unique_ptr<llvm::Module> WARN_UNUSED ExtractFunctions(llvm::Module* module, const std::vector<std::string>& functionNameList, bool ignoreLinkageIssues = false);
std::unique_ptr<llvm::Module> WARN_UNUSED ExtractFunctionDeclaration(llvm::Module* module, const std::string& functionName);

inline void ReplaceInstructionWithValue(llvm::Instruction* inst, llvm::Value* value)
{
    ReleaseAssert(inst->getType() == value->getType());
    using namespace llvm;
    BasicBlock::iterator BI(inst);
    ReplaceInstWithValue(inst->getParent()->getInstList(), BI, value);
}

inline void DumpLLVMModuleForDebug(llvm::Module* module)
{
    std::string _dst;
    llvm::raw_string_ostream rso(_dst /*target*/);
    module->print(rso, nullptr);
    std::string& dump = rso.str();
    printf("%s\n", dump.c_str());
}

inline std::string DumpLLVMModuleAsString(llvm::Module* module)
{
    std::string _dst;
    llvm::raw_string_ostream rso(_dst /*target*/);
    module->print(rso, nullptr);
    std::string dump = rso.str();
    return dump;
}

// Link the requested snippet into the current module if it hasn't been linked in yet.
// Return the LLVM Function for the requested snippet.
//
llvm::Function* WARN_UNUSED LinkInDeegenCommonSnippet(llvm::Module* module /*inout*/, const std::string& snippetName);

// Import a runtime function declaration snippet if the declaration hasn't been imported yet.
//
llvm::Function* WARN_UNUSED DeegenImportRuntimeFunctionDeclaration(llvm::Module* module /*inout*/, const std::string& snippetName);

// Create a function declaration with the specified name, which prototype is defined by declaration snippet.
//
llvm::Function* WARN_UNUSED DeegenCreateFunctionDeclarationBasedOnSnippet(llvm::Module* module /*inout*/, const std::string& snippetName, const std::string& desiredFunctionName);

inline llvm::CallInst* CreateCallToDeegenCommonSnippet(llvm::Module* module, const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    Function* callee = LinkInDeegenCommonSnippet(module, dcsName);
    ReleaseAssert(callee != nullptr);
    return CallInst::Create(callee, args, "", insertBefore);
}

inline llvm::CallInst* CreateCallToDeegenCommonSnippet(llvm::Module* module, const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    Function* callee = LinkInDeegenCommonSnippet(module, dcsName);
    ReleaseAssert(callee != nullptr);
    return CallInst::Create(callee, args, "", insertAtEnd);
}

inline llvm::CallInst* CreateCallToDeegenRuntimeFunction(llvm::Module* module, const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    Function* callee = DeegenImportRuntimeFunctionDeclaration(module, dcsName);
    ReleaseAssert(callee != nullptr);
    return CallInst::Create(callee, args, "", insertBefore);
}

inline llvm::CallInst* CreateCallToDeegenRuntimeFunction(llvm::Module* module, const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    Function* callee = DeegenImportRuntimeFunctionDeclaration(module, dcsName);
    ReleaseAssert(callee != nullptr);
    return CallInst::Create(callee, args, "", insertAtEnd);
}

// This helper struct preserves certain values (must be LLVM instruction) from being optimized away even if they are not immediately used,
// and allows us to locate these values after optimization passes are run
//
class LLVMInstructionPreserver
{
    MAKE_NONCOPYABLE(LLVMInstructionPreserver);
    MAKE_NONMOVABLE(LLVMInstructionPreserver);

public:
    LLVMInstructionPreserver() : m_func(nullptr) { }
    ~LLVMInstructionPreserver()
    {
        // Assert that 'Cleanup' has been called
        //
        ReleaseAssert(m_func == nullptr);
        ReleaseAssert(m_nameToInstMap.size() == 0);
        ReleaseAssert(m_ordToNameMap.size() == 0);
    }

    void Preserve(const std::string& name, llvm::Instruction* inst)
    {
        using namespace llvm;
        ReleaseAssert(!llvm_type_has_type<void>(inst->getType()));
        ReleaseAssert(inst->getParent() != nullptr);
        ReleaseAssert(m_func == nullptr || inst->getParent()->getParent() == m_func);
        m_func = inst->getParent()->getParent();
        ReleaseAssert(m_func != nullptr);

        ReleaseAssert(!m_nameToInstMap.count(name));

        Module* module = inst->getParent()->getModule();
        LLVMContext& ctx = module->getContext();

        size_t dummyFnOrd = 0;
        std::string dummyFnName;
        while (true)
        {
            dummyFnName = std::string(x_createdFnPrefix) + std::to_string(dummyFnOrd);
            if (module->getNamedValue(dummyFnName) == nullptr)
            {
                break;
            }
            dummyFnOrd++;
        }
        ReleaseAssert(!m_ordToNameMap.count(dummyFnOrd));
        m_ordToNameMap[dummyFnOrd] = name;

        FunctionType* fty = FunctionType::get(Type::getVoidTy(ctx) /*ret*/, { inst->getType() } /*arg*/, false /*isVarArg*/);
        Function* dummyFunc = Function::Create(fty, GlobalValue::ExternalLinkage, dummyFnName, module);
        ReleaseAssert(dummyFunc->getName() == dummyFnName);
        dummyFunc->addFnAttr(Attribute::AttrKind::WillReturn);
        dummyFunc->addFnAttr(Attribute::AttrKind::NoUnwind);

        CallInst* callInst = CallInst::Create(dummyFunc, { inst });
        callInst->insertAfter(inst);
        m_nameToInstMap[name] = callInst;
    }

    // If the LLVM function had undergone some transform, this must be called before 'Get' can be used
    // I think this might be unnecessary since probably LLVM won't do strange things to replace the CallInst,
    // but it doesn't hurt to be paranoid.. and it also validates that LLVM didn't do anything unexpected by us
    //
    void RefreshAfterTransform()
    {
        using namespace llvm;
        ReleaseAssert(m_nameToInstMap.size() == m_ordToNameMap.size());
        if (m_func == nullptr)
        {
            ReleaseAssert(m_nameToInstMap.size() == 0);
            return;
        }

        std::unordered_map<std::string, CallInst*> newMap;
        for (BasicBlock& bb : *m_func)
        {
            for (Instruction& inst : bb)
            {
                CallInst* callInst = dyn_cast<CallInst>(&inst);
                if (callInst != nullptr)
                {
                    Function* callee = callInst->getCalledFunction();
                    if (callee != nullptr)
                    {
                        std::string calleeName = callee->getName().str();
                        if (calleeName.starts_with(x_createdFnPrefix))
                        {
                            calleeName = calleeName.substr(strlen(x_createdFnPrefix));
                            size_t ord = 0;
                            for (size_t k = 0; k < calleeName.length(); k++)
                            {
                                ReleaseAssert('0' <= calleeName[k] && calleeName[k] <= '9');
                                ReleaseAssert(ord < 100000000);
                                ord = ord * 10 + static_cast<size_t>(calleeName[k] - '0');
                            }
                            ReleaseAssert(m_ordToNameMap.count(ord));
                            std::string name = m_ordToNameMap[ord];
                            ReleaseAssert(m_nameToInstMap.count(name));
                            ReleaseAssert(!newMap.count(name));
                            newMap[name] = callInst;
                        }
                    }
                }
            }
        }
        ReleaseAssert(newMap.size() == m_nameToInstMap.size());
        m_nameToInstMap = newMap;

        // Just sanity check we didn't screw anything
        //
        {
            std::unordered_set<std::string> showedUp;
            for (auto& it : m_ordToNameMap)
            {
                ReleaseAssert(m_nameToInstMap.count(it.second));
                ReleaseAssert(!showedUp.count(it.second));
                showedUp.insert(it.second);
            }
            ReleaseAssert(showedUp.size() == m_ordToNameMap.size());
            ReleaseAssert(m_ordToNameMap.size() == m_nameToInstMap.size());
        }
    }

    llvm::Value* WARN_UNUSED Get(const std::string& name) const
    {
        using namespace llvm;
        ReleaseAssert(m_nameToInstMap.count(name));
        CallInst* inst = m_nameToInstMap.find(name)->second;
        ReleaseAssert(inst->arg_size() == 1);
        ReleaseAssert(inst->getCalledFunction() != nullptr);
        ReleaseAssert(inst->getCalledFunction()->getName().str().starts_with(x_createdFnPrefix));
        return inst->getArgOperand(0);
    }

    void Cleanup()
    {
        using namespace llvm;
        RefreshAfterTransform();
        for (auto& it : m_nameToInstMap)
        {
            CallInst* inst = it.second;
            Function* func = inst->getCalledFunction();
            ReleaseAssert(func != nullptr);
            ReleaseAssert(func->getName().str().starts_with(x_createdFnPrefix));
            inst->eraseFromParent();
            ReleaseAssert(func->use_begin() == func->use_end());
            func->eraseFromParent();
        }
        m_nameToInstMap.clear();
        m_ordToNameMap.clear();
        m_func = nullptr;
    }

    friend class LLVMValuePreserver;

private:
    static constexpr const char* x_createdFnPrefix = "__DeegenImpl_LLVMInstructionPreserver_DummyFunc_";

    llvm::Function* m_func;
    std::unordered_map<std::string, llvm::CallInst*> m_nameToInstMap;
    std::unordered_map<size_t, std::string> m_ordToNameMap;
};

// A wrapper around LLVMInstructionPreserver to additionally provide the facility of preserving Argument and Constant
//
class LLVMValuePreserver
{
public:
    LLVMValuePreserver() : m_func(nullptr) { }
    ~LLVMValuePreserver()
    {
        ReleaseAssert(m_func == nullptr);
        ReleaseAssert(m_nonInstMap.size() == 0);
    }

    void Preserve(const std::string& name, llvm::Value* val)
    {
        using namespace llvm;
        ReleaseAssert(!m_nonInstMap.count(name));
        ReleaseAssert(!m_instPreserver.m_nameToInstMap.count(name));
        if (isa<Instruction>(val))
        {
            if (m_func != nullptr)
            {
                ReleaseAssert(m_func == cast<Instruction>(val)->getFunction());
            }
            m_instPreserver.Preserve(name, cast<Instruction>(val));
            return;
        }
        if (isa<Argument>(val))
        {
            Function* func = cast<Argument>(val)->getParent();
            ReleaseAssert(func != nullptr);
            ReleaseAssert(m_func == nullptr || m_func == func);
            ReleaseAssert(m_instPreserver.m_func == nullptr || m_instPreserver.m_func == func);
            m_func = func;
            m_nonInstMap[name] = val;
            return;
        }
        if (isa<Constant>(val))
        {
            m_nonInstMap[name] = val;
            return;
        }
        ReleaseAssert(false && "unexpected type of value");
    }

    void RefreshAfterTransform()
    {
        m_instPreserver.RefreshAfterTransform();
    }

    llvm::Value* WARN_UNUSED Get(const std::string& name) const
    {
        using namespace llvm;
        if (m_nonInstMap.count(name))
        {
            ReleaseAssert(!m_instPreserver.m_nameToInstMap.count(name));
            return m_nonInstMap.find(name)->second;
        }
        else
        {
            return m_instPreserver.Get(name);
        }
    }

    void Cleanup()
    {
        m_instPreserver.Cleanup();
        m_nonInstMap.clear();
        m_func = nullptr;
    }

private:
    llvm::Function* m_func;
    LLVMInstructionPreserver m_instPreserver;
    std::unordered_map<std::string, llvm::Value*> m_nonInstMap;
};

inline llvm::BinaryOperator* WARN_UNUSED CreateArithmeticBinaryOp(llvm::BinaryOperator::BinaryOps opcode, llvm::Value* lhs, llvm::Value* rhs, bool mustHaveNoUnsignedWrap, bool mustHaveNoSignedWrap)
{
    using namespace llvm;
    ReleaseAssert(lhs->getType() == rhs->getType());
    BinaryOperator* bo = BinaryOperator::Create(opcode, lhs, rhs);
    if (mustHaveNoUnsignedWrap) bo->setHasNoUnsignedWrap();
    if (mustHaveNoSignedWrap) bo->setHasNoSignedWrap();
    return bo;
}

inline llvm::Instruction* WARN_UNUSED CreateAdd(llvm::Value* lhs, llvm::Value* rhs, bool mustHaveNoUnsignedWrap = false, bool mustHaveNoSignedWrap = false)
{
    return CreateArithmeticBinaryOp(llvm::BinaryOperator::BinaryOps::Add, lhs, rhs, mustHaveNoUnsignedWrap, mustHaveNoSignedWrap);
}

inline llvm::Instruction* WARN_UNUSED CreateSignedAddNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::Instruction* insertBefore = nullptr)
{
    llvm::Instruction* res = CreateAdd(lhs, rhs, false /*mustHaveNoUnsignedWrap*/, true /*mustHaveNoSignedWrap*/);
    if (insertBefore != nullptr) { res->insertBefore(insertBefore); }
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateUnsignedAddNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::Instruction* insertBefore = nullptr)
{
    llvm::Instruction* res = CreateAdd(lhs, rhs, true /*mustHaveNoUnsignedWrap*/, false /*mustHaveNoSignedWrap*/);
    if (insertBefore != nullptr) { res->insertBefore(insertBefore); }
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateSub(llvm::Value* lhs, llvm::Value* rhs, bool mustHaveNoUnsignedWrap = false, bool mustHaveNoSignedWrap = false)
{
    return CreateArithmeticBinaryOp(llvm::BinaryOperator::BinaryOps::Sub, lhs, rhs, mustHaveNoUnsignedWrap, mustHaveNoSignedWrap);
}

inline llvm::Instruction* WARN_UNUSED CreateSignedSubNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::Instruction* insertBefore = nullptr)
{
    llvm::Instruction* res = CreateSub(lhs, rhs, false /*mustHaveNoUnsignedWrap*/, true /*mustHaveNoSignedWrap*/);
    if (insertBefore != nullptr) { res->insertBefore(insertBefore); }
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateUnsignedSubNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::Instruction* insertBefore = nullptr)
{
    llvm::Instruction* res = CreateSub(lhs, rhs, true /*mustHaveNoUnsignedWrap*/, false /*mustHaveNoSignedWrap*/);
    if (insertBefore != nullptr) { res->insertBefore(insertBefore); }
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateMul(llvm::Value* lhs, llvm::Value* rhs, bool mustHaveNoUnsignedWrap = false, bool mustHaveNoSignedWrap = false)
{
    return CreateArithmeticBinaryOp(llvm::BinaryOperator::BinaryOps::Mul, lhs, rhs, mustHaveNoUnsignedWrap, mustHaveNoSignedWrap);
}

inline llvm::Instruction* WARN_UNUSED CreateSignedMulNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::Instruction* insertBefore = nullptr)
{
    llvm::Instruction* res = CreateMul(lhs, rhs, false /*mustHaveNoUnsignedWrap*/, true /*mustHaveNoSignedWrap*/);
    if (insertBefore != nullptr) { res->insertBefore(insertBefore); }
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateUnsignedMulNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::Instruction* insertBefore = nullptr)
{
    llvm::Instruction* res = CreateMul(lhs, rhs, true /*mustHaveNoUnsignedWrap*/, false /*mustHaveNoSignedWrap*/);
    if (insertBefore != nullptr) { res->insertBefore(insertBefore); }
    return res;
}

inline void AssertInstructionIsFollowedByUnreachable(llvm::Instruction* inst)
{
    using namespace llvm;
    Instruction* nextInst = inst->getNextNode();
    ReleaseAssert(nextInst != nullptr);
    ReleaseAssert(dyn_cast<UnreachableInst>(nextInst) != nullptr);
    ReleaseAssert(nextInst->getNextNode() == nullptr);
}

inline llvm::CallInst* EmitLLVMIntrinsicMemcpy(llvm::Module* module, llvm::Value* dst, llvm::Value* src, llvm::Value* bytesToCopy, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(dst));
    ReleaseAssert(llvm_value_has_type<void*>(src));
    ReleaseAssert(llvm_value_has_type<uint64_t>(bytesToCopy) || llvm_value_has_type<uint32_t>(bytesToCopy));
    Function* memcpyFunc = Intrinsic::getDeclaration(module, Intrinsic::memcpy, { llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx), bytesToCopy->getType() /*sizeType*/ });
    CallInst* result = CallInst::Create(memcpyFunc, { dst, src, bytesToCopy, CreateLLVMConstantInt<bool>(ctx, false) /*isVolatile*/ }, "", insertBefore);
    return result;
}

inline llvm::CallInst* EmitLLVMIntrinsicMemmove(llvm::Module* module, llvm::Value* dst, llvm::Value* src, llvm::Value* bytesToCopy, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(dst));
    ReleaseAssert(llvm_value_has_type<void*>(src));
    ReleaseAssert(llvm_value_has_type<uint64_t>(bytesToCopy) || llvm_value_has_type<uint32_t>(bytesToCopy));
    Function* memmoveFunc = Intrinsic::getDeclaration(module, Intrinsic::memmove, { llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx), bytesToCopy->getType() /*sizeType*/ });
    CallInst* result = CallInst::Create(memmoveFunc, { dst, src, bytesToCopy, CreateLLVMConstantInt<bool>(ctx, false) /*isVolatile*/ }, "", insertBefore);
    return result;
}

// Undo the effect of '__attribute__((__used__))' for a global variable
// That is, after this function call, 'gv' is no longer protected from being removed even if unused
//
inline void RemoveGlobalValueUsedAttributeAnnotation(llvm::GlobalValue* gv)
{
    using namespace llvm;
    // Return true if 'gvToRemove' is found and removed from the specified 'used' intrinsic name
    //
    auto removeFromUseIntrinsicName = [](GlobalValue* gvToRemove, const std::string& intrinName) -> bool
    {
        Module* module = gvToRemove->getParent();
        GlobalVariable* intrinVar = module->getGlobalVariable(intrinName);
        if (intrinVar == nullptr)
        {
            ReleaseAssert(module->getNamedValue(intrinName) == nullptr);
            return false;
        }

        ReleaseAssert(intrinVar->hasInitializer());
        Constant* initv = intrinVar->getInitializer();
        ReleaseAssert(isa<ConstantArray>(initv));
        ConstantArray* initializer = cast<ConstantArray>(initv);
        uint32_t numElements = initializer->getNumOperands();
        bool found = false;
        std::vector<Constant*> newValueList;
        for (uint32_t i = 0; i < numElements; i++)
        {
            Constant* value = initializer->getOperand(i);
            if (value == gvToRemove)
            {
                found = true;
            }
            else
            {
                newValueList.push_back(value);
            }
        }
        if (!found)
        {
            return false;
        }

        ReleaseAssert(intrinVar->use_empty());

        if (newValueList.size() == 0)
        {
            intrinVar->eraseFromParent();
        }
        else
        {
            Constant* newInitializer = ConstantArray::get(llvm::ArrayType::get(initializer->getType()->getElementType(), newValueList.size()), newValueList);
            intrinVar->setName(std::string("tmp_") + intrinName);

            GlobalVariable* replacedVar = new GlobalVariable(
                *module,
                newInitializer->getType(),
                intrinVar->isConstant(),
                intrinVar->getLinkage(),
                newInitializer,
                intrinName,
                intrinVar /*insertBefore*/,
                intrinVar->getThreadLocalMode(),
                intrinVar->getType()->getAddressSpace(),
                intrinVar->isExternallyInitialized());
            replacedVar->copyAttributesFrom(intrinVar);

            intrinVar->eraseFromParent();
            ReleaseAssert(replacedVar->getName() == intrinName);
        }
        return true;
    };

    bool success = false;
    success |= removeFromUseIntrinsicName(gv, "llvm.used");
    success |= removeFromUseIntrinsicName(gv, "llvm.compiler.used");

    // If success == false here, it means 'gvToRemove' is not inside the globals marked '__used__' at all.
    // This should be treated as a bug.
    //
    ReleaseAssert(success);
}

// Return 'true' if the global was not already in 'llvm.used' or 'llvm.compiler.used', in which case the global is added to 'llvm.used'
// Return 'false' otherwise, in which case no changes are made.
//
inline bool AddUsedAttributeToGlobalValue(llvm::GlobalValue* globalVal)
{
    using namespace llvm;
    // If 'compilerUsed' is true, it checks 'llvm.compiler.used'. Otherwise it checks 'llvm.used'
    //
    auto isGlobalInUsedIntrinsic = [](GlobalValue* gv, bool compilerUsed) WARN_UNUSED -> bool
    {
        Module* module = gv->getParent();
        SmallVector<GlobalValue*, 8> res;
        collectUsedGlobalVariables(*module, res, compilerUsed);
        for (GlobalValue* value : res)
        {
            if (value == gv)
            {
                return true;
            }
        }
        return false;
    };

    if (isGlobalInUsedIntrinsic(globalVal, true) || isGlobalInUsedIntrinsic(globalVal, false))
    {
        // The global is already marked 'used', don't do anything
        //
        return false;
    }

    appendToUsed(*globalVal->getParent(), { globalVal });
    return true;
}

}   // namespace dast

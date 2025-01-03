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
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/CFG.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/StripSymbols.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Object/ELFObjectFile.h"

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

// Compile an LLVM module to assembly (.S) file
//
std::string WARN_UNUSED CompileLLVMModuleToAssemblyFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel);
std::string WARN_UNUSED CompileLLVMModuleToAssemblyFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel, const std::function<void(llvm::TargetOptions&)>& targetOptionsTweaker);

std::string WARN_UNUSED CompileLLVMModuleToAssemblyFileForStencilGeneration(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel);
std::string WARN_UNUSED CompileLLVMModuleToAssemblyFileForStencilGeneration(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel, const std::function<void(llvm::TargetOptions&)>& targetOptionsTweaker);

// Compile an LLVM module to ELF object (.o) file
//
std::string WARN_UNUSED CompileLLVMModuleToElfObjectFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel);
std::string WARN_UNUSED CompileLLVMModuleToElfObjectFile(llvm::Module* module, llvm::Reloc::Model relocationModel, llvm::CodeModel::Model codeModel, const std::function<void(llvm::TargetOptions&)>& targetOptionsTweaker);

// Load a ELF object file
//
llvm::object::ELFObjectFileBase* WARN_UNUSED LoadElfObjectFile(llvm::LLVMContext& ctx, const std::string& fileContent);

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
    ReleaseAssert(inst->getParent() != nullptr);
    BasicBlock::iterator BI(inst);
    ReplaceInstWithValue(BI, value);
}

inline void DumpLLVMModuleForDebug(llvm::Module* module)
{
    std::string _dst;
    llvm::raw_string_ostream rso(_dst /*target*/);
    module->print(rso, nullptr);
    std::string& dump = rso.str();
    printf("%s\n", dump.c_str());
}

inline std::string DumpLLVMTypeAsString(llvm::Type* ty)
{
    std::string _dst;
    llvm::raw_string_ostream rso(_dst /*target*/);
    ty->print(rso);
    std::string dump = rso.str();
    return dump;
}

inline std::string DumpLLVMModuleAsString(llvm::Module* module)
{
    std::string _dst;
    llvm::raw_string_ostream rso(_dst /*target*/);
    module->print(rso, nullptr);
    std::string dump = rso.str();
    return dump;
}

inline std::unique_ptr<llvm::Module> WARN_UNUSED ParseLLVMModuleFromString(llvm::LLVMContext& ctx, const std::string& moduleName, const std::string& moduleString)
{
    using namespace llvm;

    SMDiagnostic llvmErr;
    MemoryBufferRef mb(StringRef(moduleString.data(), moduleString.length()), StringRef(moduleName.data(), moduleName.length()));

    std::unique_ptr<Module> module = parseIR(mb, llvmErr, ctx);
    ReleaseAssert(module.get() != nullptr);
    return module;
}

std::pair<std::unique_ptr<llvm::Module>, llvm::Function*> WARN_UNUSED GetDeegenCommonSnippetModule(llvm::LLVMContext& ctx, const std::string& snippetName);

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
        dummyFunc->addFnAttr(Attribute::WillReturn);
        dummyFunc->addFnAttr(Attribute::NoUnwind);

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

    // Clone this struct to 'dest' after the function has been cloned into a new module
    //
    void Clone(LLVMInstructionPreserver& dest /*out*/, llvm::Module* newModule)
    {
        if (m_func == nullptr)
        {
            ReleaseAssert(m_nameToInstMap.empty() && m_ordToNameMap.empty());
            dest.m_func = nullptr;
            dest.m_nameToInstMap.clear();
            dest.m_ordToNameMap.clear();
        }
        else
        {
            ReleaseAssert(m_func != nullptr && dest.m_func == nullptr);
            dest.m_func = newModule->getFunction(m_func->getName());
            ReleaseAssert(dest.m_func != nullptr);
            dest.m_nameToInstMap = m_nameToInstMap;
            dest.m_ordToNameMap = m_ordToNameMap;
            dest.RefreshAfterTransform();
        }
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

    // Clone this struct to 'dest' after the function has been cloned into a new module
    //
    void Clone(LLVMValuePreserver& dest /*out*/, llvm::Module* newModule)
    {
        using namespace llvm;
        m_instPreserver.Clone(dest.m_instPreserver /*out*/, newModule);
        if (m_func == nullptr)
        {
            ReleaseAssert(m_nonInstMap.empty());
            dest.m_func = nullptr;
            dest.m_nonInstMap.clear();
        }
        else
        {
            ReleaseAssert(m_func != nullptr && dest.m_func == nullptr);
            dest.m_func = newModule->getFunction(m_func->getName());
            ReleaseAssert(dest.m_func != nullptr);

            dest.m_nonInstMap.clear();
            for (auto& it : m_nonInstMap)
            {
                Value* val = it.second;
                Value* mappedVal;
                if (isa<Constant>(val))
                {
                    mappedVal = val;
                }
                else
                {
                    ReleaseAssert(isa<Argument>(val) && cast<Argument>(val)->getParent() == m_func);
                    uint32_t ord = cast<Argument>(val)->getArgNo();
                    ReleaseAssert(ord < dest.m_func->arg_size());
                    mappedVal = dest.m_func->getArg(ord);
                }
                ReleaseAssert(!dest.m_nonInstMap.count(it.first));
                dest.m_nonInstMap[it.first] = mappedVal;
            }
            ReleaseAssert(m_nonInstMap.size() == dest.m_nonInstMap.size());
        }
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

inline llvm::Instruction* WARN_UNUSED CreateSignedAddNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    ReleaseAssert(insertAtEnd != nullptr);
    LLVMContext& ctx = insertAtEnd->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    Instruction* res = CreateSignedAddNoOverflow(lhs, rhs, dummy);
    dummy->eraseFromParent();
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateUnsignedAddNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::Instruction* insertBefore = nullptr)
{
    llvm::Instruction* res = CreateAdd(lhs, rhs, true /*mustHaveNoUnsignedWrap*/, false /*mustHaveNoSignedWrap*/);
    if (insertBefore != nullptr) { res->insertBefore(insertBefore); }
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateUnsignedAddNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    ReleaseAssert(insertAtEnd != nullptr);
    LLVMContext& ctx = insertAtEnd->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    Instruction* res = CreateUnsignedAddNoOverflow(lhs, rhs, dummy);
    dummy->eraseFromParent();
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

inline llvm::Instruction* WARN_UNUSED CreateSignedSubNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    ReleaseAssert(insertAtEnd != nullptr);
    LLVMContext& ctx = insertAtEnd->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    Instruction* res = CreateSignedSubNoOverflow(lhs, rhs, dummy);
    dummy->eraseFromParent();
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateUnsignedSubNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::Instruction* insertBefore = nullptr)
{
    llvm::Instruction* res = CreateSub(lhs, rhs, true /*mustHaveNoUnsignedWrap*/, false /*mustHaveNoSignedWrap*/);
    if (insertBefore != nullptr) { res->insertBefore(insertBefore); }
    return res;
}

inline llvm::Instruction* WARN_UNUSED CreateUnsignedSubNoOverflow(llvm::Value* lhs, llvm::Value* rhs, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    ReleaseAssert(insertAtEnd != nullptr);
    LLVMContext& ctx = insertAtEnd->getContext();
    UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
    Instruction* res = CreateUnsignedSubNoOverflow(lhs, rhs, dummy);
    dummy->eraseFromParent();
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

template<bool forceInline>
llvm::Function* GetLLVMIntrinsicMemcpyFn(llvm::Module* module, llvm::Type* sizeType)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_type_has_type<uint64_t>(sizeType) || llvm_type_has_type<uint32_t>(sizeType));
    return Intrinsic::getDeclaration(module, forceInline ? Intrinsic::memcpy_inline : Intrinsic::memcpy, { llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx), sizeType });
}

template<bool forceInline = false>
llvm::CallInst* EmitLLVMIntrinsicMemcpy(llvm::Module* module, llvm::Value* dst, llvm::Value* src, llvm::Value* bytesToCopy, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(dst));
    ReleaseAssert(llvm_value_has_type<void*>(src));
    ReleaseAssert(llvm_value_has_type<uint64_t>(bytesToCopy) || llvm_value_has_type<uint32_t>(bytesToCopy));
    Function* fn = GetLLVMIntrinsicMemcpyFn<forceInline>(module, bytesToCopy->getType());
    CallInst* result = CallInst::Create(fn, { dst, src, bytesToCopy, CreateLLVMConstantInt<bool>(ctx, false) /*isVolatile*/ }, "", insertBefore);
    return result;
}

template<bool forceInline = false>
llvm::CallInst* EmitLLVMIntrinsicMemcpy(llvm::Module* module, llvm::Value* dst, llvm::Value* src, llvm::Value* bytesToCopy, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(dst));
    ReleaseAssert(llvm_value_has_type<void*>(src));
    ReleaseAssert(llvm_value_has_type<uint64_t>(bytesToCopy) || llvm_value_has_type<uint32_t>(bytesToCopy));
    Function* fn = GetLLVMIntrinsicMemcpyFn<forceInline>(module, bytesToCopy->getType());
    CallInst* result = CallInst::Create(fn, { dst, src, bytesToCopy, CreateLLVMConstantInt<bool>(ctx, false) /*isVolatile*/ }, "", insertAtEnd);
    return result;
}

inline llvm::Function* GetLLVMIntrinsicMemmoveFn(llvm::Module* module, llvm::Type* sizeType)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_type_has_type<uint64_t>(sizeType) || llvm_type_has_type<uint32_t>(sizeType));
    return Intrinsic::getDeclaration(module, Intrinsic::memmove, { llvm_type_of<void*>(ctx), llvm_type_of<void*>(ctx), sizeType /*sizeType*/ });
}

inline llvm::CallInst* EmitLLVMIntrinsicMemmove(llvm::Module* module, llvm::Value* dst, llvm::Value* src, llvm::Value* bytesToCopy, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(dst));
    ReleaseAssert(llvm_value_has_type<void*>(src));
    ReleaseAssert(llvm_value_has_type<uint64_t>(bytesToCopy) || llvm_value_has_type<uint32_t>(bytesToCopy));
    Function* memmoveFunc = GetLLVMIntrinsicMemmoveFn(module, bytesToCopy->getType());
    CallInst* result = CallInst::Create(memmoveFunc, { dst, src, bytesToCopy, CreateLLVMConstantInt<bool>(ctx, false) /*isVolatile*/ }, "", insertBefore);
    return result;
}

inline llvm::CallInst* EmitLLVMIntrinsicMemmove(llvm::Module* module, llvm::Value* dst, llvm::Value* src, llvm::Value* bytesToCopy, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(dst));
    ReleaseAssert(llvm_value_has_type<void*>(src));
    ReleaseAssert(llvm_value_has_type<uint64_t>(bytesToCopy) || llvm_value_has_type<uint32_t>(bytesToCopy));
    Function* memmoveFunc = GetLLVMIntrinsicMemmoveFn(module, bytesToCopy->getType());
    CallInst* result = CallInst::Create(memmoveFunc, { dst, src, bytesToCopy, CreateLLVMConstantInt<bool>(ctx, false) /*isVolatile*/ }, "", insertAtEnd);
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

inline std::string WARN_UNUSED GetFirstAvailableFunctionNameWithPrefix(llvm::Module* module, const std::string& prefix)
{
    std::string decidedName;
    size_t suffixOrd = 0;
    while (true)
    {
        decidedName = prefix + std::to_string(suffixOrd);
        if (module->getNamedValue(decidedName) == nullptr)
        {
            break;
        }
        suffixOrd++;
    }
    return decidedName;
}

// It seems like the LLVM inlining pass does not run to fixpoint.
// That is, if the LLVM inlining pass is executed repeatedly,
// more and more callees can get inlined, causing undesirable code bloat.
//
// This utility is used to prevent this from happening.
// Basically the rule here is, each function only gets one chance to be inlined.
// Once an inlining pass gets run on the module, all the functions in the module
// will be recorded in a magic global variable in the module.
// Before the next time we run the inlining pass, the global variable will be read,
// and all those functions will be marked no_inline before the LLVM pass.
//
// Note that this means if we run the inlining pass, and then create a new function
// that calls some existing function in the module, the callee won't get inlined
// since it's marked no_inline. But this is not a problem for our use case because
// we will not create functions that call random user logic: we always manually mark
// the callees of our created function as always_inline.
//
struct LLVMRepeatedInliningInhibitor
{
    LLVMRepeatedInliningInhibitor(llvm::Module* module)
        : m_module(module)
        , m_attrAnnotated(false)
        , m_attrRestored(false)
        , m_globalFound(false)
    { }

    ~LLVMRepeatedInliningInhibitor()
    {
        ReleaseAssertImp(m_attrAnnotated, m_attrRestored);
    }

    // Called before running an inlining pass
    //
    void PrepareForInliningPass()
    {
        using namespace llvm;

        ReleaseAssert(!m_attrAnnotated);
        m_attrAnnotated = true;

        GlobalVariable* gv = m_module->getGlobalVariable(x_magic_global_name);
        if (gv == nullptr)
        {
            ReleaseAssert(m_module->getNamedValue(x_magic_global_name) == nullptr);
        }
        else
        {
            RemoveGlobalValueUsedAttributeAnnotation(gv);
            ReleaseAssert(gv->hasInitializer());
            Constant* gvi = gv->getInitializer();
            if (!isa<ConstantAggregateZero>(gvi))
            {
                ConstantArray* list = dyn_cast<ConstantArray>(gvi);
                ReleaseAssert(list != nullptr);
                size_t numElements = list->getType()->getNumElements();
                for (size_t i = 0; i < numElements; i++)
                {
                    Constant* c = list->getOperand(static_cast<uint32_t>(i));
                    ReleaseAssert(llvm_value_has_type<void*>(c));
                    Function* func = dyn_cast<Function>(c);
                    ReleaseAssert(func != nullptr);

                    if (func->hasFnAttribute(Attribute::AlwaysInline))
                    {
                        // This means our caller has changed the function to always_inline.
                        // We should respect it. Just skip.
                        //
                        // TODO: there are still some problems with this strategy. Specifically, many Deegen APIs
                        // are marked no_inline initially and gradually desugared by the desugaring pass. This
                        // causes a problem: a Deegen API 'f' may call some function 'g', which is small but not
                        // inlined into 'f' because 'f' itself is also too small. However, when 'f' is marked
                        // always_inline and inlined into its caller, now it makes sense to inline 'g' as well
                        // (since the size of 'g' is going to be small compared with its caller). But our current
                        // implementation will prevent 'g' from being inlined because 'g' already got a chance to
                        // inline (while 'f' is still marked no_inline). For now, we are working around this issue
                        // by adding __flatten__ attribute to such functions to make sure their callees are inlined.
                        //
                        continue;
                    }

                    std::string fnName = func->getName().str();

                    if (func->hasFnAttribute(Attribute::NoInline))
                    {
                        // This means our caller has added no_inline attribute to this function.
                        // We should respect it by making sure it is still no_inline after the pass.
                        // Nevertheless, this function has already got its chance to inline
                        // while its no_inline attribute has not been added (which is why it is in this list),
                        // so it should still be in the list after the pass
                        //
                        ReleaseAssert(!m_list.count(fnName));
                        m_list.insert(fnName);
                    }
                    else
                    {
                        // In addition to putting this function into 'm_list',
                        // we should also add no_inline attribute, which shall be removed after the optimization passes
                        //
                        ReleaseAssert(!m_list.count(fnName));
                        m_list.insert(fnName);

                        func->addFnAttr(Attribute::NoInline);
                        ReleaseAssert(!m_functionToRemoveNoInlineAttr.count(fnName));
                        m_functionToRemoveNoInlineAttr.insert(fnName);
                    }
                }
            }
            gv->eraseFromParent();
            gv = nullptr;
        }

        // Iterate through all functions, record all the functions that does not have no_inline attribute:
        // those functions will get their chance to inline in the incoming inlining pass, so they should get into the new list
        //
        for (Function& func : *m_module)
        {
            if (func.isIntrinsic())
            {
                continue;
            }
            if (func.empty())
            {
                continue;
            }
            if (func.hasFnAttribute(Attribute::AlwaysInline))
            {
                continue;
            }
            if (!func.hasFnAttribute(Attribute::NoInline))
            {
                ReleaseAssert(!m_list.count(func.getName().str()));
                m_list.insert(func.getName().str());
            }
        }
    }

    void RestoreAfterInliningPass()
    {
        using namespace llvm;

        ReleaseAssert(m_attrAnnotated && !m_attrRestored);
        m_attrRestored = true;

        for (auto& fnName : m_functionToRemoveNoInlineAttr)
        {
            Function* func = m_module->getFunction(fnName);
            if (func == nullptr)
            {
                ReleaseAssert(m_module->getNamedValue(fnName) == nullptr);
            }
            else
            {
                ReleaseAssert(func->hasFnAttribute(Attribute::NoInline));
                func->removeFnAttr(Attribute::NoInline);
            }
        }

        CreateNewList();
    }

    // Called before running DCE pass
    // We need this because our magic global is holding a reference to a bunch of functions
    // which prevents them from being dropped by DCE
    //
    void PrepareForDCE()
    {
        using namespace llvm;

        ReleaseAssert(!m_attrAnnotated);
        m_attrAnnotated = true;

        GlobalVariable* gv = m_module->getGlobalVariable(x_magic_global_name);
        if (gv == nullptr)
        {
            m_globalFound = false;
            ReleaseAssert(m_module->getNamedValue(x_magic_global_name) == nullptr);
        }
        else
        {
            m_globalFound = true;
            RemoveGlobalValueUsedAttributeAnnotation(gv);
            ReleaseAssert(gv->hasInitializer());
            ConstantArray* list = dyn_cast<ConstantArray>(gv->getInitializer());
            size_t numElements = list->getType()->getNumElements();
            for (size_t i = 0; i < numElements; i++)
            {
                Constant* c = list->getOperand(static_cast<uint32_t>(i));
                ReleaseAssert(llvm_value_has_type<void*>(c));
                Function* func = dyn_cast<Function>(c);
                ReleaseAssert(func != nullptr);
                ReleaseAssert(!m_list.count(func->getName().str()));
                m_list.insert(func->getName().str());
            }
            gv->eraseFromParent();
            gv = nullptr;
        }
    }

    void RestoreAfterDCE()
    {
        using namespace llvm;

        ReleaseAssert(m_attrAnnotated && !m_attrRestored);
        m_attrRestored = true;

        if (m_globalFound)
        {
            CreateNewList();
        }
    }

    static void DropAllRecord(llvm::Module* module)
    {
        using namespace llvm;
        GlobalVariable* gv = module->getGlobalVariable(x_magic_global_name);
        if (gv != nullptr)
        {
            RemoveGlobalValueUsedAttributeAnnotation(gv);
            gv->eraseFromParent();
        }
        ReleaseAssert(module->getNamedValue(x_magic_global_name) == nullptr);
    }

    // Remove the specified function from the list, so it gets one more chance to be inlined
    //
    static void GiveOneMoreChance(llvm::Function* func)
    {
        using namespace llvm;
        Module* module = func->getParent();
        ReleaseAssert(module != nullptr);
        std::string fnName = func->getName().str();
        LLVMRepeatedInliningInhibitor rii(module);
        // This is a bit lame and fragile but the logic of PrepareForDCE() just suits our purpose
        //
        rii.PrepareForDCE();
        if (rii.m_list.count(fnName))
        {
            rii.m_list.erase(rii.m_list.find(fnName));
        }
        ReleaseAssert(!rii.m_list.count(fnName));
        rii.RestoreAfterDCE();
    }

    static constexpr const char* x_magic_global_name = "__deegen_llvm_repeated_inlining_inhibitor_recorder";

private:
    void CreateNewList()
    {
        using namespace llvm;
        LLVMContext& ctx = m_module->getContext();
        std::vector<Constant*> newList;
        for (auto& fnName : m_list)
        {
            Function* func = m_module->getFunction(fnName);
            if (func == nullptr)
            {
                ReleaseAssert(m_module->getNamedValue(fnName) == nullptr);
            }
            else
            {
                newList.push_back(func);
            }
        }

        Constant* ca = ConstantArray::get(ArrayType::get(llvm_type_of<void*>(ctx), newList.size()), newList);
        ReleaseAssert(m_module->getNamedValue(x_magic_global_name) == nullptr);
        GlobalVariable* newGv = new GlobalVariable(
            *m_module,
            ca->getType(),
            true /*isConstant*/,
            GlobalValue::ExternalLinkage,
            ca /*initializer*/,
            x_magic_global_name);
        ReleaseAssert(newGv->getName().str() == x_magic_global_name);

        AddUsedAttributeToGlobalValue(newGv);
    }

    llvm::Module* m_module;
    bool m_attrAnnotated;
    bool m_attrRestored;
    bool m_globalFound;
    std::unordered_set<std::string> m_list;
    std::unordered_set<std::string> m_functionToRemoveNoInlineAttr;
};

inline void RunLLVMOptimizePass(llvm::Module* module)
{
    using namespace llvm;

    LLVMRepeatedInliningInhibitor rii(module);
    rii.PrepareForInliningPass();

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

    rii.RestoreAfterInliningPass();

    // 'RunLLVMOptimizePass' should only be called once for the final codegen (either to binary, or for ExtractFunction).
    // So we should remove the RepeatedInliningInhibitor global variable at this step.
    //
    LLVMRepeatedInliningInhibitor::DropAllRecord(module);

    ValidateLLVMModule(module);
}

inline void RunLLVMDeadGlobalEliminationRaw(llvm::Module* module)
{
    using namespace llvm;

    ValidateLLVMModule(module);

    PassBuilder passBuilder;
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    passBuilder.registerModuleAnalyses(MAM);
    passBuilder.registerCGSCCAnalyses(CGAM);
    passBuilder.registerFunctionAnalyses(FAM);
    passBuilder.registerLoopAnalyses(LAM);
    passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM;

    // Delete unreachable globals
    //
    MPM.addPass(GlobalDCEPass());

    // Remove dead debug info
    //
    MPM.addPass(StripDeadDebugInfoPass());
    // Remove dead func decls
    //
    MPM.addPass(StripDeadPrototypesPass());

    MPM.run(*module, MAM);
}

inline void RunLLVMDeadGlobalElimination(llvm::Module* module)
{
    using namespace llvm;

    LLVMRepeatedInliningInhibitor rii(module);
    rii.PrepareForDCE();

    RunLLVMDeadGlobalEliminationRaw(module);

    rii.RestoreAfterDCE();
    ValidateLLVMModule(module);
}

// In unit tests, sometimes we simply assert on the whole content of the LLVM module
// However, Clang-generated modules contain a 'llvm.ident' metadata string which identifies the Clang version.
// We don't want to make test fail if we use a slightly different Clang version, so this funcion strips the
// 'llvm.ident'. It should only be used by unit tests.
//
inline void TestOnly_StripLLVMIdentMetadata(llvm::Module* module)
{
    using namespace llvm;
    NamedMDNode* mdNode = module->getNamedMetadata("llvm.ident");
    if (mdNode != nullptr)
    {
        module->eraseNamedMetadata(mdNode);
    }
}

inline int WARN_UNUSED StoiOrFail(const std::string& s)
{
    int res;
    try
    {
        res = std::stoi(s);
    }
    catch (...)
    {
        ReleaseAssert(false);
    }
    return res;
}

// Copy 'bytes' to 'dst'. 'bytes' may have at most 15 bytes
//
inline void EmitMemcpyLessThan16B(llvm::Value* dst, const std::vector<uint8_t>& bytes, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    LLVMContext& ctx = dst->getContext();

    size_t size = bytes.size();
    ReleaseAssert(size <= 15);
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> copyStrategies = {
        /*0*/   {},
        /*1*/   { {0, 1} },
        /*2*/   { {0, 2} },
        /*3*/   { {0, 2}, {2, 1} },
        /*4*/   { {0, 4} },
        /*5*/   { {0, 4}, {4, 1} },
        /*6*/   { {0, 4}, {4, 2} },
        /*7*/   { {0, 4}, {3, 4} },
        /*8*/   { {0, 8} },
        /*9*/   { {0, 8}, {8, 1} },
        /*10*/  { {0, 8}, {8, 2} },
        /*11*/  { {0, 8}, {7, 4} },
        /*12*/  { {0, 8}, {8, 4} },
        /*13*/  { {0, 8}, {5, 8} },
        /*14*/  { {0, 8}, {6, 8} },
        /*15*/  { {0, 8}, {7, 8} }
    };
    ReleaseAssert(size < copyStrategies.size());
    std::vector<std::pair<uint32_t, uint32_t>> strategy = copyStrategies[size];

    // Check the strategy is correct
    //
    {
        std::vector<bool> bytesFilled;
        bytesFilled.resize(size, false /*value*/);
        for (auto& item : strategy)
        {
            for (size_t i = item.first; i < item.first + item.second; i++)
            {
                ReleaseAssert(i < size);
                bytesFilled[i] = true;
            }
        }
        for (size_t i = 0; i < size; i++) { ReleaseAssert(bytesFilled[i]); }
    }

    for (auto& item : strategy)
    {
        size_t offset = item.first;
        uint32_t sizeBytes = item.second;

        uint64_t value;
        switch (sizeBytes)
        {
        case 1: { value = UnalignedLoad<uint8_t>(bytes.data() + offset); break; }
        case 2: { value = UnalignedLoad<uint16_t>(bytes.data() + offset); break; }
        case 4: { value = UnalignedLoad<uint32_t>(bytes.data() + offset); break; }
        case 8: { value = UnalignedLoad<uint64_t>(bytes.data() + offset); break; }
        default: { ReleaseAssert(false); }
        }   /*switch*/

        Value* valToStore = ConstantInt::get(ctx, APInt(sizeBytes * 8 /*numBits*/, value, false /*isSigned*/));
        Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), dst, { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertAtEnd);
        new StoreInst(valToStore, addr, false /*isVolatile*/, Align(1), insertAtEnd);
    }
}

// Emit a memcpy_inline.
// If 'mustBeExact' is false, this function will be allowed to overwrite at most 7 bytes (so that we can have less instructions)
// The caller is responsible for accounting for the extra bytes when doing the allocation in such case.
//
inline void EmitCopyLogicForJitCodeGen(llvm::Module* module,
                                       const std::vector<uint8_t>& bytes,
                                       llvm::Value* dst,
                                       const std::string& gvNameHint,
                                       llvm::BasicBlock* insertAtEnd,
                                       bool mustBeExact)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    ReleaseAssert(llvm_value_has_type<void*>(dst));
    size_t roundedLen = bytes.size();
    if (!mustBeExact)
    {
        roundedLen = RoundUpToMultipleOf<8>(roundedLen);
    }

    std::vector<uint8_t> paddedBytes = bytes;
    while (paddedBytes.size() < roundedLen)
    {
        paddedBytes.push_back(0);
    }
    ReleaseAssert(paddedBytes.size() == roundedLen);

    // LLVM will use SIMD registers to move the 16-byte chunks, then use GPR with literal constants to move the residue parts
    // Chop of the residue parts from the array since they will not be used by LLVM (LLVM cannot eliminate these by itself even though in theory it can)
    //
    size_t simdChunkSize = roundedLen / 16 * 16;
    if (simdChunkSize > 0)
    {
        std::vector<Constant*> arr;
        for (size_t i = 0; i < simdChunkSize; i++)
        {
            arr.push_back(CreateLLVMConstantInt<uint8_t>(ctx, paddedBytes[i]));
        }
        ArrayType* aty = ArrayType::get(llvm_type_of<uint8_t>(ctx), simdChunkSize);
        Constant* ca = ConstantArray::get(aty, arr);

        GlobalVariable* gv = new GlobalVariable(*module, aty, true /*isConstant*/, GlobalValue::PrivateLinkage, ca /*initializer*/, gvNameHint);
        gv->setAlignment(Align(16));
        gv->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

        // TODO: LLVM lowers memcpy.inline to 'rep movsb' if the buffer is too long (>128 bytes?),
        // which has disastrous performance on older CPUs without FSRM, but (should?) work well on Golden Cove
        // or later (but really? our output buffer is not aligned..)
        // Think about if we need to do anything to this later..
        //
        EmitLLVMIntrinsicMemcpy<true /*forceInline*/>(module, dst, gv, CreateLLVMConstantInt<uint64_t>(ctx, simdChunkSize), insertAtEnd);
    }

    if (simdChunkSize < roundedLen)
    {
        std::vector<uint8_t> residueBytes;
        residueBytes.insert(residueBytes.end(), paddedBytes.data() + simdChunkSize, paddedBytes.data() + roundedLen);
        ReleaseAssert(residueBytes.size() + simdChunkSize == roundedLen);

        Value* newDst = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), dst, { CreateLLVMConstantInt<uint64_t>(ctx, simdChunkSize) }, "", insertAtEnd);
        EmitMemcpyLessThan16B(newDst, residueBytes, insertAtEnd);
    }
}

// Find the first non-alloca instruction in the entry block of the specified function, which is the earliest safe place to insert new instructions
//
inline llvm::Instruction* WARN_UNUSED FindFirstNonAllocaInstInEntryBB(llvm::Function* func)
{
    using namespace llvm;
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
}

// For each function in the given module which function name starts with "prefixToFind",
// rename the function by changing the prefix into "prefixToReplace"
//
inline void RenameAllFunctionsStartingWithPrefix(llvm::Module* module, const std::string& prefixToFind, const std::string& prefixToReplace)
{
    using namespace llvm;
    std::vector<Function*> fnsToRename;
    for (Function& fn : *module)
    {
        if (fn.getName().starts_with(prefixToFind))
        {
            fnsToRename.push_back(&fn);
        }
    }
    for (Function* fn : fnsToRename)
    {
        std::string oldName = fn->getName().str();
        ReleaseAssert(oldName.starts_with(prefixToFind));
        std::string newName = prefixToReplace + oldName.substr(prefixToFind.length());
        fn->setName(newName);
        ReleaseAssert(fn->getName() == newName);
    }
}

inline std::string WARN_UNUSED ConvertStringToLowerCase(const std::string& input)
{
    std::string result = input;
    for (size_t i = 0; i < result.size(); i++)
    {
        if ('A' <= result[i] && result[i] <= 'Z')
        {
            result[i] = static_cast<char>(result[i] - 'A' + 'a');
        }
    }
    return result;
}

// The IR generated by these CPP struct accessor functions do not have proper TBAA information!
// So caller should be very careful to only use these functions when not having TBAA info is fine.
//
// To generate TBAA-safe LLVM IR one should use DeegenCommonSnippet.
//
template<auto member_object_ptr>
llvm::Value* WARN_UNUSED GetMemberFromCppStruct(llvm::Value* ptr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = ptr->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(ptr));

    constexpr size_t offset = offsetof_member_v<member_object_ptr>;
    using MemberTy = typeof_member_t<member_object_ptr>;

    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), ptr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);
    Type* ty = llvm_type_of<MemberTy>(ctx);
    return new LoadInst(ty, gep, "", insertBefore);
}

template<auto member_object_ptr>
llvm::Value* WARN_UNUSED GetMemberFromCppStruct(llvm::Value* ptr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(ptr->getContext(), insertAtEnd);
    Value* res = GetMemberFromCppStruct<member_object_ptr>(ptr, dummy);
    dummy->eraseFromParent();
    return res;
}

template<auto member_object_ptr>
void WriteCppStructMember(llvm::Value* ptr, llvm::Value* valueToWrite, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = ptr->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(ptr));

    constexpr size_t offset = offsetof_member_v<member_object_ptr>;
    using MemberTy = typeof_member_t<member_object_ptr>;

    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), ptr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);
    Type* ty = llvm_type_of<MemberTy>(ctx);
    ReleaseAssert(valueToWrite->getType() == ty);
    new StoreInst(valueToWrite, gep, insertBefore);
}

template<auto member_object_ptr>
void WriteCppStructMember(llvm::Value* ptr, llvm::Value* valueToWrite, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(ptr->getContext(), insertAtEnd);
    WriteCppStructMember<member_object_ptr>(ptr, valueToWrite, dummy);
    dummy->eraseFromParent();
}

// Populate multi-byte NOP instruction to an address range
//
inline void FillAddressRangeWithX64MultiByteNOPs(uint8_t* addr, size_t length)
{
    // From Intel's Manual:
    //    https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2b-manual.pdf
    //    Page 165, table 4-12, "Recommended Multi-Byte Sequence of NOP Instruction"
    //
    // AMD Manual recommends the same byte sequence.
    //
    static constexpr uint8_t nop1[] = { 0x90 };
    static constexpr uint8_t nop2[] = { 0x66, 0x90 };
    static constexpr uint8_t nop3[] = { 0x0F, 0x1F, 0x00 };
    static constexpr uint8_t nop4[] = { 0x0F, 0x1F, 0x40, 0x00 };
    static constexpr uint8_t nop5[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
    static constexpr uint8_t nop6[] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
    static constexpr uint8_t nop7[] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };
    static constexpr uint8_t nop8[] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static constexpr uint8_t nop9[] = { 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };

    // NOP 10-15: we use the NOP sequence from JavaScriptCore, see
    //     https://sillycross.github.io/r/WebKit/Source/JavaScriptCore/assembler/X86Assembler.h.html#3990
    //
    static constexpr uint8_t nop10[] = { 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 };
    static constexpr uint8_t nop11[] = { 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 };
    static constexpr uint8_t nop12[] = { 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 };
    static constexpr uint8_t nop13[] = { 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 };
    static constexpr uint8_t nop14[] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 };
    static constexpr uint8_t nop15[] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 };

    static constexpr const uint8_t* nops[16] = {
        nullptr, nop1, nop2, nop3, nop4, nop5, nop6, nop7, nop8, nop9, nop10, nop11, nop12, nop13, nop14, nop15
    };
    while (length > 0)
    {
        size_t choice = 15;
        choice = std::min(choice, length);
        memcpy(addr, nops[choice], choice);
        length -= choice;
        addr += choice;
    }
}

// Utility functions for repatchable jumps (jmp imm32)
//
struct X64PatchableJumpUtil
{
    static llvm::Value* WARN_UNUSED GetDest(llvm::Value* jmpEndAddr, llvm::BasicBlock* insertAtEnd)
    {
        using namespace llvm;
        LLVMContext& ctx = jmpEndAddr->getContext();
        ReleaseAssert(llvm_value_has_type<void*>(jmpEndAddr));
        GetElementPtrInst* ptr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), jmpEndAddr,
                                                                   { CreateLLVMConstantInt<uint64_t>(ctx, static_cast<uint64_t>(-4)) }, "", insertAtEnd);
        Value* val32 = new LoadInst(llvm_type_of<uint32_t>(ctx), ptr, "", false /*isVolatile*/, Align(1), insertAtEnd);
        Value* val64 = new SExtInst(val32, llvm_type_of<uint64_t>(ctx), "", insertAtEnd);
        GetElementPtrInst* dest = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), jmpEndAddr, { val64 }, "", insertAtEnd);
        return dest;
    }

    static void SetDest(llvm::Value* jmpEndAddr, llvm::Value* newDest, llvm::BasicBlock* insertAtEnd)
    {
        using namespace llvm;
        LLVMContext& ctx = jmpEndAddr->getContext();
        ReleaseAssert(llvm_value_has_type<void*>(jmpEndAddr));
        ReleaseAssert(llvm_value_has_type<void*>(newDest));
        Value* jmpEndAddr64 = new PtrToIntInst(jmpEndAddr, llvm_type_of<uint64_t>(ctx), "", insertAtEnd);
        Value* newDest64 = new PtrToIntInst(newDest, llvm_type_of<uint64_t>(ctx), "", insertAtEnd);
        Instruction* diff = CreateSub(newDest64, jmpEndAddr64);
        diff->insertBefore(insertAtEnd->end());
        Value* diff32 = new TruncInst(diff, llvm_type_of<uint32_t>(ctx), "", insertAtEnd);
        GetElementPtrInst* ptr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), jmpEndAddr,
                                                                   { CreateLLVMConstantInt<uint64_t>(ctx, static_cast<uint64_t>(-4)) }, "", insertAtEnd);
        new StoreInst(diff32, ptr, false /*isVolatile*/, Align(1), insertAtEnd);
    }
};

}   // namespace dast

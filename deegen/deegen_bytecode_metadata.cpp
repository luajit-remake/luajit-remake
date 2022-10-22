#include "deegen_bytecode_metadata.h"

namespace dast {

static constexpr const char* x_bytecodeMetadataElementAnnotationFnPrefix = "__DeegenImpl_GetBytecodeMetadataStructElement_";

static std::string WARN_UNUSED GetBytecodeMetadataElementAnnotationFnName(const BytecodeMetadataElement* e)
{
    return std::string(x_bytecodeMetadataElementAnnotationFnPrefix) + std::to_string(reinterpret_cast<uint64_t>(e));
}

static llvm::Function* CreateBytecodeMetadataElementAnnotationFn(llvm::Module* module, const BytecodeMetadataElement* e)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    std::string fnName = GetBytecodeMetadataElementAnnotationFnName(e);
    Function* f = module->getFunction(fnName);
    if (f != nullptr)
    {
        ReleaseAssert(f->empty());
        return f;
    }
    ReleaseAssert(module->getNamedValue(fnName) == nullptr);
    FunctionType* fty = FunctionType::get(llvm_type_of<void*>(ctx), { llvm_type_of<void*>(ctx) }, false /*isVarArg*/);
    f = Function::Create(fty, GlobalValue::ExternalLinkage, fnName, module);
    ReleaseAssert(f->getName() == fnName);
    f->addFnAttr(Attribute::AttrKind::NoUnwind);
    return f;
}

llvm::Instruction* WARN_UNUSED BytecodeMetadataElement::EmitGetAddress(llvm::Module* module, llvm::Value* metadataStructPtr, llvm::Instruction* insertBefore) const
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(metadataStructPtr));
    if (HasAssignedFinalOffset())
    {
        return GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), metadataStructPtr, { CreateLLVMConstantInt<uint64_t>(ctx, GetStructOffset()) }, "", insertBefore);
    }
    else
    {
        Function* callee = CreateBytecodeMetadataElementAnnotationFn(module, this);
        ReleaseAssert(callee->empty());
        ReleaseAssert(llvm_type_has_type<void*>(callee->getReturnType()));
        return CallInst::Create(callee, { metadataStructPtr }, "", insertBefore);
    }
}

llvm::Instruction* WARN_UNUSED BytecodeMetadataElement::EmitGetAddress(llvm::Module* module, llvm::Value* metadataStructPtr, llvm::BasicBlock* insertAtEnd) const
{
    using namespace llvm;
    UnreachableInst* tmp = new UnreachableInst(module->getContext(), insertAtEnd);
    Instruction* res = EmitGetAddress(module, metadataStructPtr, tmp /*insertBefore*/);
    tmp->eraseFromParent();
    return res;
}

void BytecodeMetadataStructBase::LowerAll(llvm::Module* module) const
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();
    std::vector<BytecodeMetadataElement*> list = CollectAllElements();
    for (BytecodeMetadataElement* e : list)
    {
        ReleaseAssert(e->HasAssignedFinalOffset());
        std::string fnName = GetBytecodeMetadataElementAnnotationFnName(e);
        Function* f = module->getFunction(fnName);
        if (f == nullptr)
        {
            ReleaseAssert(module->getNamedValue(fnName) == nullptr);
            continue;
        }
        ReleaseAssert(llvm_type_has_type<void*>(f->getReturnType()));
        ReleaseAssert(f->arg_size() == 1);
        Argument* arg = f->getArg(0);
        ReleaseAssert(llvm_value_has_type<void*>(arg));

        ReleaseAssert(f->empty());
        BasicBlock* bb = BasicBlock::Create(ctx, "", f);
        GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), arg, { CreateLLVMConstantInt<uint64_t>(ctx, e->GetStructOffset()) }, "", bb);
        ReturnInst::Create(ctx, gep, bb);

        f->setLinkage(GlobalValue::InternalLinkage);
        f->addFnAttr(Attribute::AttrKind::AlwaysInline);

        // Now, rename the function so that even if 'LowerAll' is called again, the function won't be lowered again
        // This also prevents an edge case where the user called 'LowerAll', but before calling any desugaring pass to inline the function,
        // freed the struct descriptor and then created another struct descriptor that happens to be allocated on the same the address.
        //
        std::string newFnName = GetFirstAvailableFunctionNameWithPrefix(module, "__deegen_bytecode_metadata_struct_member_accessor_impl_");
        f->setName(newFnName);
        ReleaseAssert(f->getName() == newFnName);

        ValidateLLVMFunction(f);
    }
}

// Currently, we simply use a naive approach where all members are layouted one after another in the order they are defined
// TODO: Clearly we could have done a lot better by reshuffling the elements to:
// (1) Minimize padding due to alignment
// (2) For unions, put elements with the same GC traits at the same place to make the GC parser logic simpler
//
static BytecodeMetadataStructBase::StructInfo WARN_UNUSED OptimizeStructLayoutAndAssignOffsets(BytecodeMetadataStructBase* root)
{
    size_t curOffset = 0;
    size_t maxAlignment = 1;

    auto assignOffsetToElement = [&](BytecodeMetadataElement* e)
    {
        size_t alignment = e->GetAlignment();
        size_t sz = e->GetSize();
        curOffset = (curOffset + alignment - 1) / alignment * alignment;
        e->AssignFinalOffset(curOffset);
        curOffset += sz;
        maxAlignment = std::max(maxAlignment, alignment);
    };

    std::function<void(BytecodeMetadataStructBase*)> assignOffsetToStruct = [&](BytecodeMetadataStructBase* sb)
    {
        BytecodeMetadataStructKind kind = sb->GetKind();
        if (kind == BytecodeMetadataStructKind::Element)
        {
            BytecodeMetadataStructElement* e = assert_cast<BytecodeMetadataStructElement*>(sb);
            assignOffsetToElement(e->GetElement());
            return;
        }
        if (kind == BytecodeMetadataStructKind::Struct)
        {
            BytecodeMetadataStruct* st = assert_cast<BytecodeMetadataStruct*>(sb);
            for (BytecodeMetadataStructBase* member : st->GetMembers())
            {
                assignOffsetToStruct(member);
            }
            return;
        }
        ReleaseAssert(kind == BytecodeMetadataStructKind::TaggedUnion);
        BytecodeMetadataTaggedUnion* tu = assert_cast<BytecodeMetadataTaggedUnion*>(sb);
        assignOffsetToElement(tu->GetTag());

        size_t offsetStartForUnion = curOffset;
        size_t maxOffsetAfterUnion = curOffset;
        for (BytecodeMetadataStructBase* member : tu->GetMembers())
        {
            curOffset = offsetStartForUnion;
            assignOffsetToStruct(member);
            maxOffsetAfterUnion = std::max(maxOffsetAfterUnion, curOffset);
        }
        curOffset = maxOffsetAfterUnion;
    };

    assignOffsetToStruct(root);

    size_t storeSize = curOffset;
    size_t allocSize = (storeSize + maxAlignment - 1) / maxAlignment * maxAlignment;

    return {
        .alignment = maxAlignment,
        .allocSize = allocSize,
        .storeSize = storeSize
    };
}

BytecodeMetadataStructBase::StructInfo WARN_UNUSED BytecodeMetadataStructBase::FinalizeStructAndAssignOffsets()
{
    std::vector<BytecodeMetadataElement*> checklist = CollectAllElements();
    {
        std::unordered_set<BytecodeMetadataElement*> checkUnique;
        for (BytecodeMetadataElement* e : checklist)
        {
            ReleaseAssert(!checkUnique.count(e));
            checkUnique.insert(e);
        }
    }
    for (BytecodeMetadataElement* e : checklist)
    {
        ReleaseAssert(!e->HasAssignedFinalOffset());
    }

    StructInfo res = OptimizeStructLayoutAndAssignOffsets(this);

    // Sanity check that all elements have their offsets assigned
    //
    for (BytecodeMetadataElement* e : checklist)
    {
        ReleaseAssert(e->HasAssignedFinalOffset());
    }

    return res;
}

}   // namespace dast

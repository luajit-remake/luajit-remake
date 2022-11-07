#include "deegen_interpreter_function_entry.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_bytecode_operand.h"
#include "deegen_ast_return.h"
#include "deegen_options.h"
#include "tvalue.h"
#include "tag_register_optimization.h"
#include "deegen_interpreter_bytecode_impl_creator.h"

namespace dast {

std::string InterpreterFunctionEntryLogicCreator::GetFunctionName()
{
    std::string name = "deegen_interpreter_enter_guest_language_function_";
    if (IsNumFixedParamSpecialized())
    {
        name += std::to_string(GetSpecializedNumFixedParam()) + "_params_";
    }
    else
    {
        name += "generic_";
    }
    if (m_acceptVarArgs)
    {
        name += "va";
    }
    else
    {
        name += "nova";
    }
    return name;
}

std::unique_ptr<llvm::Module> WARN_UNUSED InterpreterFunctionEntryLogicCreator::Get(llvm::LLVMContext& ctx)
{
    ReleaseAssert(!m_generated);
    m_generated = true;

    using namespace llvm;
    std::unique_ptr<Module> module = std::make_unique<Module>("generated_function_entry_logic", ctx);

    Function* func = InterpreterFunctionInterface::CreateFunction(module.get(), GetFunctionName());

    {
        // Just pull in some C++ function so we can set up the attributes..
        //
        Function* tmp = LinkInDeegenCommonSnippet(module.get(), "SimpleLeftToRightCopyMayOvercopy");
        func->addFnAttr(Attribute::AttrKind::NoUnwind);
        CopyFunctionAttributes(func /*dst*/, tmp /*src*/);
    }

    // TODO: add parameter attributes
    //

    ReleaseAssert(func->arg_size() == 16);
    Value* coroutineCtx = func->getArg(0);
    coroutineCtx->setName("coroCtx");
    Value* preFixupStackBase = func->getArg(1);
    preFixupStackBase->setName("preFixupStackBase");
    Value* numArgsAsPtr = func->getArg(2);
    Value* calleeCodeBlockHeapPtrAsNormalPtr = func->getArg(3);
    Value* isMustTail64 = func->getArg(6);

    BasicBlock* entryBB = BasicBlock::Create(ctx, "", func);

    ReleaseAssert(llvm_value_has_type<void*>(numArgsAsPtr));
    Value* numArgs = new PtrToIntInst(numArgsAsPtr, llvm_type_of<uint64_t>(ctx), "", entryBB);
    numArgs->setName("numProvidedArgs");

    ReleaseAssert(llvm_value_has_type<void*>(calleeCodeBlockHeapPtrAsNormalPtr));
    Value* calleeCodeBlockHeapPtr = new AddrSpaceCastInst(calleeCodeBlockHeapPtrAsNormalPtr, llvm_type_of<HeapPtr<void>>(ctx), "", entryBB);
    Value* calleeCodeBlock = CreateCallToDeegenCommonSnippet(module.get(), "SimpleTranslateToRawPointer", { calleeCodeBlockHeapPtr }, entryBB);
    ReleaseAssert(llvm_value_has_type<void*>(calleeCodeBlock));
    calleeCodeBlock->setName("calleeCodeBlock");

    Value* bytecodePtr = CreateCallToDeegenCommonSnippet(module.get(), "GetBytecodePtrFromCodeBlock", { calleeCodeBlock }, entryBB);
    bytecodePtr->setName("bytecodePtr");

    Value* stackBaseAfterFixUp = nullptr;
    if (!m_acceptVarArgs)
    {
        if (IsNumFixedParamSpecialized())
        {
            size_t numArgsCalleeAccepts = GetSpecializedNumFixedParam();
            if (numArgsCalleeAccepts == 0)
            {
                // Easiest case, no work to do
                //
            }
            else if (numArgsCalleeAccepts <= 2)
            {
                uint64_t nilValue = TValue::Nil().m_value;
                // We can completely get rid of the branches, by simply unconditionally writing 'numArgsCalleeAccepts' nils beginning at stackbase[numArgs]
                //
                GetElementPtrInst* base = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), preFixupStackBase, { numArgs }, "", entryBB);
                for (size_t i = 0; i < numArgsCalleeAccepts; i++)
                {
                    GetElementPtrInst* target = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), base, { CreateLLVMConstantInt<uint64_t>(ctx, i) }, "", entryBB);
                    std::ignore = new StoreInst(CreateLLVMConstantInt<uint64_t>(ctx, nilValue), target, entryBB);
                }
            }
            else
            {
                uint64_t nilValue = TValue::Nil().m_value;
                Constant* numFixedParams = CreateLLVMConstantInt<uint64_t>(ctx, numArgsCalleeAccepts);
                CreateCallToDeegenCommonSnippet(module.get(), "PopulateNilToUnprovidedParams", { preFixupStackBase, numArgs, numFixedParams, CreateLLVMConstantInt<uint64_t>(ctx, nilValue) }, entryBB);
            }
        }
        else
        {
            Value* numFixedParams = CreateCallToDeegenCommonSnippet(module.get(), "GetNumFixedParamsFromCodeBlock", { calleeCodeBlock }, entryBB);
            uint64_t nilValue = TValue::Nil().m_value;
            CreateCallToDeegenCommonSnippet(module.get(), "PopulateNilToUnprovidedParams", { preFixupStackBase, numArgs, numFixedParams, CreateLLVMConstantInt<uint64_t>(ctx, nilValue) }, entryBB);
        }
        stackBaseAfterFixUp = preFixupStackBase;
    }
    else
    {
        // TODO: consider rewrite this and split out the cold path for tail call memmove
        //
        Value* numFixedParams;
        if (IsNumFixedParamSpecialized())
        {
            numFixedParams = CreateLLVMConstantInt<uint64_t>(ctx, GetSpecializedNumFixedParam());
        }
        else
        {
            numFixedParams = CreateCallToDeegenCommonSnippet(module.get(), "GetNumFixedParamsFromCodeBlock", { calleeCodeBlock }, entryBB);
        }
        uint64_t nilValue = TValue::Nil().m_value;
        stackBaseAfterFixUp = CreateCallToDeegenCommonSnippet(module.get(), "FixupStackFrameForVariadicArgFunction", { preFixupStackBase, numArgs, numFixedParams, CreateLLVMConstantInt<uint64_t>(ctx, nilValue), isMustTail64 }, entryBB);
    }

    ReleaseAssert(llvm_value_has_type<void*>(stackBaseAfterFixUp));
    stackBaseAfterFixUp->setName("stackBaseAfterFixUp");

    // Unfortunately a bunch of APIs only take 'insertBefore', not 'insertAtEnd'
    // We workaround it by creating a temporary instruction and remove it in the end
    //
    UnreachableInst* dummyInst = new UnreachableInst(ctx, entryBB);

    Value* opcode = BytecodeVariantDefinition::DecodeBytecodeOpcode(bytecodePtr, dummyInst /*insertBefore*/);
    ReleaseAssert(llvm_value_has_type<uint64_t>(opcode));

    Value* targetFunction = GetInterpreterFunctionFromInterpreterOpcode(module.get(), opcode, dummyInst /*insertBefore*/);
    ReleaseAssert(llvm_value_has_type<void*>(targetFunction));

    Value* preloadedOpValue = nullptr;
    if (x_deegen_enable_interpreter_optimistic_preloading)
    {
        preloadedOpValue = BytecodeVariantDefinition::OptimisticPreloadBytecodeOperands(bytecodePtr, dummyInst /*insertBefore*/);
    }

    InterpreterFunctionInterface::CreateDispatchToBytecode(
        targetFunction,
        coroutineCtx,
        stackBaseAfterFixUp,
        bytecodePtr,
        calleeCodeBlock,
        preloadedOpValue,
        dummyInst /*insertBefore*/);

    dummyInst->eraseFromParent();

    bool shouldPutIntoHotCodeSection;
    if (m_acceptVarArgs)
    {
        shouldPutIntoHotCodeSection = IsNumFixedParamSpecialized() && GetSpecializedNumFixedParam() <= 2;
    }
    else
    {
        shouldPutIntoHotCodeSection = true;
    }
    if (shouldPutIntoHotCodeSection)
    {
        func->setSection(InterpreterBytecodeImplCreator::x_hot_code_section_name);
    }

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::Top);
    RunTagRegisterOptimizationPass(func);
    RunLLVMOptimizePass(module.get());

    return module;
}

}   // namespace dast

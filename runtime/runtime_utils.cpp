#include "runtime_utils.h"
#include "deegen_options.h"
#include "vm.h"
#include "table_object.h"
#include "deegen_enter_vm_from_c.h"

#include "generated/get_guest_language_function_interpreter_entry_point.h"
#include "json_utils.h"
#include "bytecode_builder.h"
#include "drt/baseline_jit_codegen_helper.h"

const size_t x_num_bytecode_metadata_struct_kinds_ = x_num_bytecode_metadata_struct_kinds;

TValue WARN_UNUSED MakeErrorMessage(const char* msg)
{
    return TValue::CreatePointer(VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(msg, static_cast<uint32_t>(strlen(msg))));
}

TValue WARN_UNUSED MakeErrorMessageForUnableToCall(TValue badValue)
{
    // The Lua message is "attmpt to call a (type of badValue) value"
    //
    char msg[100];
    auto makeMsg = [&](const char* ty)
    {
        snprintf(msg, 100, "attempt to call a %s value", ty);
    };
    auto makeMsg2 = [&](int d)
    {
        snprintf(msg, 100, "attempt to call a (internal type %d) value", d);
    };

    if (badValue.IsInt32())
    {
        makeMsg("number");
    }
    else if (badValue.IsDouble())
    {
        makeMsg("number");
    }
    else if (badValue.IsMIV())
    {
        MiscImmediateValue miv = badValue.AsMIV();
        if (miv.IsNil())
        {
            makeMsg("nil");
        }
        else
        {
            assert(miv.IsBoolean());
            makeMsg("boolean");
        }
    }
    else
    {
        assert(badValue.IsPointer());
        UserHeapGcObjectHeader* p = badValue.AsPointer<UserHeapGcObjectHeader>().As();
        if (p->m_type == HeapEntityType::String)
        {
            makeMsg("string");
        }
        else if (p->m_type == HeapEntityType::Function)
        {
            makeMsg("function");
        }
        else if (p->m_type == HeapEntityType::Table)
        {
            makeMsg("table");
        }
        else if (p->m_type == HeapEntityType::Thread)
        {
            makeMsg("thread");
        }
        else
        {
            // TODO: handle userdata type
            //
            makeMsg2(static_cast<int>(p->m_type));
        }
    }
    return MakeErrorMessage(msg);
}

void* WARN_UNUSED UnlinkedCodeBlock::GetInterpreterEntryPoint()
{
    return generated::GetGuestLanguageFunctionEntryPointForInterpreter(m_hasVariadicArguments, m_numFixedArguments);
}

CodeBlock* WARN_UNUSED CodeBlock::Create(VM* vm, UnlinkedCodeBlock* ucb, UserHeapPointer<TableObject> globalObject)
{
    assert(ucb->m_bytecodeMetadataLength % 8 == 0);
    size_t sizeToAllocate = GetTrailingArrayOffset() + ucb->m_bytecodeMetadataLength + sizeof(TValue) * ucb->m_cstTableLength + RoundUpToMultipleOf<8>(ucb->m_bytecodeLengthIncludingTailPadding);
    uint8_t* addressBegin = vm->AllocFromSystemHeap(static_cast<uint32_t>(sizeToAllocate)).AsNoAssert<uint8_t>();
    memcpy(addressBegin, ucb->m_cstTable, sizeof(TValue) * ucb->m_cstTableLength);

    CodeBlock* cb = reinterpret_cast<CodeBlock*>(addressBegin + sizeof(TValue) * ucb->m_cstTableLength);
    ConstructInPlace(cb);
    cb->SystemHeapGcObjectHeader::Populate<ExecutableCode>(cb);
    cb->m_executableCodeKind = Kind::BytecodeFunction;
    cb->m_hasVariadicArguments = ucb->m_hasVariadicArguments;
    cb->m_numFixedArguments = ucb->m_numFixedArguments;
    cb->m_globalObject = globalObject;
    cb->m_stackFrameNumSlots = ucb->m_stackFrameNumSlots;
    cb->m_numUpvalues = ucb->m_numUpvalues;
    cb->m_owner = ucb;
    cb->m_bestEntryPoint = ucb->GetInterpreterEntryPoint();
    cb->m_bytecodeLengthIncludingTailPadding = ucb->m_bytecodeLengthIncludingTailPadding;
    cb->m_bytecodeMetadataLength = ucb->m_bytecodeMetadataLength;
    cb->m_baselineCodeBlock = nullptr;
    cb->m_dfgCodeBlock = nullptr;
    if (vm->InterpreterCanTierUpFurther())
    {
        cb->m_interpreterTierUpCounter = x_interpreter_tier_up_threshold_bytecode_length_multiplier * ucb->m_bytecodeLengthIncludingTailPadding;
    }
    else
    {
        // We increment counter on forward edges, choose 2^62 to avoid overflow.
        //
        cb->m_interpreterTierUpCounter = 1LL << 62;
    }
    memcpy(cb->GetBytecodeStream(), ucb->m_bytecode, ucb->m_bytecodeLengthIncludingTailPadding);

    ForEachBytecodeMetadata(cb, []<typename T>(T* md) ALWAYS_INLINE {
        md->Init();
    });

    // Immediately compile the CodeBlock to baseline JIT code if requested by user.
    // Note that this must be done after we have set up all the fields in the CodeBlock
    //
    if (vm->IsEngineStartingTierBaselineJit())
    {
        BaselineCodeBlock* bcb = deegen_baseline_jit_do_codegen(cb);
        assert(cb->m_baselineCodeBlock == bcb);
        assert(cb->m_bestEntryPoint != ucb->GetInterpreterEntryPoint());
        assert(cb->m_bestEntryPoint == bcb->m_jitCodeEntry);
        std::ignore = bcb;
    }

    return cb;
}

void CodeBlock::UpdateBestEntryPoint(void* newEntryPoint)
{
    void* oldBestEntryPoint = m_bestEntryPoint;

    // Update all interpreter call IC to use the new entry point
    //
    {
        uint8_t* endAnchor = reinterpret_cast<uint8_t*>(&m_interpreterCallIcList);
        uint8_t* curAnchor = endAnchor;
        while (true)
        {
            curAnchor -= UnalignedLoad<int32_t>(curAnchor + 4);
            if (curAnchor == endAnchor)
            {
                break;
            }
            // We rely on the ABI layout that the codePtr resides right before the doubly link
            //
            assert(UnalignedLoad<void*>(curAnchor - 8) == oldBestEntryPoint);
            UnalignedStore<void*>(curAnchor - 8, newEntryPoint);
        }
    }

    // Update all JIT call IC to use the new entry point
    //
    {
        uint64_t diff = reinterpret_cast<uint64_t>(newEntryPoint) - reinterpret_cast<uint64_t>(oldBestEntryPoint);
        for (JitCallInlineCacheEntry* icEntry : m_jitCallIcList.elements())
        {
            icEntry->UpdateTargetFunctionCodePtr(diff);
        }
    }

    // Update m_bestEntryPoint so uncached calls also use the new entry point
    //
    m_bestEntryPoint = newEntryPoint;
}

std::pair<TValue* /*retStart*/, uint64_t /*numRet*/> VM::LaunchScript(ScriptModule* module)
{
    CoroutineRuntimeContext* rc = GetRootCoroutine();
    return DeegenEnterVMFromC(rc, module->m_defaultEntryPoint.As(), rc->m_stackBegin);
}

UserHeapPointer<FunctionObject> WARN_UNUSED NO_INLINE FunctionObject::CreateAndFillUpvalues(CodeBlock* cb, CoroutineRuntimeContext* rc, TValue* stackFrameBase, FunctionObject* parent, size_t selfOrdinalInStackFrame)
{
    UnlinkedCodeBlock* ucb = cb->m_owner;
    FunctionObject* r = Create(VM::GetActiveVMForCurrentThread(), cb).As();
    assert(parent->m_executable.As()->IsBytecodeFunction());
    assert(cb->m_owner->m_parent == static_cast<CodeBlock*>(parent->m_executable.As())->m_owner);
    uint32_t numUpvalues = cb->m_numUpvalues;
    UpvalueMetadata* upvalueInfo = ucb->m_upvalueInfo;
    for (uint32_t ord = 0; ord < numUpvalues; ord++)
    {
        UpvalueMetadata& uvmt = upvalueInfo[ord];
        assert(uvmt.m_immutabilityFieldFinalized);
        TValue uv;
        if (uvmt.m_isParentLocal)
        {
            if (uvmt.m_isImmutable)
            {
                if (uvmt.m_slot == selfOrdinalInStackFrame)
                {
                    uv = TValue::Create<tFunction>(r);
                }
                else
                {
                    uv = stackFrameBase[uvmt.m_slot];
                }
            }
            else
            {
                Upvalue* uvPtr = Upvalue::Create(rc, stackFrameBase + uvmt.m_slot, uvmt.m_isImmutable);
                uv = TValue::CreatePointer(uvPtr);
            }
        }
        else
        {
            uv = FunctionObject::GetMutableUpvaluePtrOrImmutableUpvalue(parent, uvmt.m_slot);
        }
        AssertIff(!uvmt.m_isImmutable, (uv.IsPointer() && uv.GetHeapEntityType() == HeapEntityType::Upvalue));
        r->m_upvalues[ord] = uv;
    }
    return r;
}

CoroutineRuntimeContext* CoroutineRuntimeContext::Create(VM* vm, UserHeapPointer<TableObject> globalObject, size_t numStackSlots)
{
    CoroutineRuntimeContext* r = vm->AllocFromUserHeap(static_cast<uint32_t>(sizeof(CoroutineRuntimeContext))).AsNoAssert<CoroutineRuntimeContext>();
    UserHeapGcObjectHeader::Populate(r);
    r->m_hiddenClass = x_hiddenClassForCoroutineRuntimeContext;
    r->m_coroutineStatus = CoroutineStatus::CreateInitStatus();
    r->m_globalObject = globalObject;
    r->m_numVariadicRets = 0;
    r->m_variadicRetSlotBegin = 0;
    r->m_upvalueList.m_value = 0;
    size_t bytesToAllocate = numStackSlots * sizeof(TValue);
    bytesToAllocate = RoundUpToMultipleOf<VM::x_pageSize>(bytesToAllocate);
    void* stackAreaWithOverflowProtection = mmap(nullptr, bytesToAllocate + x_stackOverflowProtectionAreaSize * 2,
                                                 PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    VM_FAIL_WITH_ERRNO_IF(stackAreaWithOverflowProtection == MAP_FAILED,
                          "Failed to reserve address range of length %llu",
                          static_cast<unsigned long long>(bytesToAllocate + x_stackOverflowProtectionAreaSize * 2));

    void* stackArea = mmap(reinterpret_cast<uint8_t*>(stackAreaWithOverflowProtection) + x_stackOverflowProtectionAreaSize,
                           bytesToAllocate, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    VM_FAIL_WITH_ERRNO_IF(stackArea == MAP_FAILED,
                          "Out of Memory: Allocation of length %llu failed", static_cast<unsigned long long>(bytesToAllocate));
    assert(stackArea == reinterpret_cast<uint8_t*>(stackAreaWithOverflowProtection) + x_stackOverflowProtectionAreaSize);
    r->m_stackBegin = reinterpret_cast<TValue*>(stackArea);
    return r;
}

BaselineCodeBlock* WARN_UNUSED BaselineCodeBlock::Create(CodeBlock* cb,
                                                         uint32_t numBytecodes,
                                                         uint32_t slowPathDataStreamLength,
                                                         void* jitCodeEntry,
                                                         void* jitRegionStart,
                                                         uint32_t jitRegionSize)
{
    size_t numEntriesInConstantTable = cb->m_owner->m_cstTableLength;
    static_assert(alignof(BaselineCodeBlock) == 8);         // the computation below relies on this
    size_t sizeToAllocate = sizeof(TValue) * numEntriesInConstantTable + GetTrailingArrayOffset() + sizeof(SlowPathDataAndBytecodeOffset) * numBytecodes + slowPathDataStreamLength;
    sizeToAllocate = RoundUpToMultipleOf<8>(sizeToAllocate);

    VM* vm = VM::GetActiveVMForCurrentThread();
    uint8_t* addressBegin = vm->AllocFromSystemHeap(static_cast<uint32_t>(sizeToAllocate)).AsNoAssert<uint8_t>();
    memcpy(addressBegin, cb->m_owner->m_cstTable, sizeof(TValue) * numEntriesInConstantTable);

    BaselineCodeBlock* res = reinterpret_cast<BaselineCodeBlock*>(addressBegin + sizeof(TValue) * numEntriesInConstantTable);
    ConstructInPlace(res);
    res->m_jitCodeEntry = jitCodeEntry;
    res->m_owner = cb;
    res->m_globalObject = cb->m_globalObject;
    res->m_numBytecodes = numBytecodes;
    res->m_stackFrameNumSlots = cb->m_stackFrameNumSlots;
    res->m_maxObservedNumVariadicArgs = 0;
    res->m_slowPathDataStreamLength = slowPathDataStreamLength;
    res->m_jitRegionStart = jitRegionStart;
    res->m_jitRegionSize = jitRegionSize;

    TestAssert(cb->m_baselineCodeBlock == nullptr);
    cb->m_baselineCodeBlock = res;

    return res;
}

JitCallInlineCacheEntry* WARN_UNUSED JitCallInlineCacheEntry::Create(VM* vm,
                                                                     ExecutableCode* targetExecutableCode,
                                                                     SpdsPtr<JitCallInlineCacheEntry> callSiteNextNode,
                                                                     void* entity,
                                                                     size_t icTraitKind)
{
    JitCallInlineCacheEntry* entry = vm->AllocateFromSpdsRegionUninitialized<JitCallInlineCacheEntry>();
    ConstructInPlace(entry);
    entry->m_callSiteNextNode = callSiteNextNode;
    entry->m_entity = entity;
    assert(icTraitKind <= std::numeric_limits<uint16_t>::max());
    const JitCallInlineCacheTraits* trait = deegen_jit_call_inline_cache_trait_table[icTraitKind];

    AssertImp(trait->m_isDirectCallMode, entry->m_entity.IsUserHeapPointer() && entry->m_entity.As<UserHeapGcObjectHeader>()->m_type == HeapEntityType::Function);
    AssertImp(!trait->m_isDirectCallMode, !entry->m_entity.IsUserHeapPointer() && entry->m_entity.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::ExecutableCode);
    AssertImp(!trait->m_isDirectCallMode, targetExecutableCode == entity);

    void* regionVoidPtr = vm->GetJITMemoryAlloc()->AllocateGivenStepping(trait->m_jitCodeAllocationLengthStepping);

    assert(reinterpret_cast<uint64_t>(regionVoidPtr) < (1ULL << 48));
    entry->m_taggedPtr = reinterpret_cast<uint64_t>(regionVoidPtr) | (static_cast<uint64_t>(icTraitKind) << 48);

    assert(entry->GetJitRegionStart() == regionVoidPtr);
    assert(entry->GetIcTrait() == trait);

    assert(!entry->IsOnDoublyLinkedList());
    if (targetExecutableCode->IsBytecodeFunction())
    {
        CodeBlock* targetCb = static_cast<CodeBlock*>(targetExecutableCode);
        targetCb->m_jitCallIcList.InsertAtHead(entry);
        assert(entry->IsOnDoublyLinkedList());
    }

    return entry;
}

void JitCallInlineCacheEntry::Destroy(VM* vm)
{
    AssertIff(IsOnDoublyLinkedList(), GetTargetExecutableCode()->IsBytecodeFunction());
    if (IsOnDoublyLinkedList())
    {
        RemoveFromDoublyLinkedList();
    }
    vm->GetJITMemoryAlloc()->Free(GetJitRegionStart());
    vm->DeallocateSpdsRegionObject(this);
}

void* WARN_UNUSED JitCallInlineCacheSite::InsertInDirectCallMode(uint16_t dcIcTraitKind, FunctionObject* func, uint8_t* transitedToCCMode /*out*/)
{
    assert(m_numEntries < x_maxEntries);
    assert(m_mode == Mode::DirectCall);
    assert(reinterpret_cast<UserHeapGcObjectHeader*>(func)->m_type == HeapEntityType::Function);

    VM* vm = VM::GetActiveVMForCurrentThread();

    // Compute bloom filter hash mask
    //
    uint16_t bloomFilterMask;
    {
        uint64_t hashValue64 = HashPrimitiveTypes(func->m_executable.m_value);
        bloomFilterMask = static_cast<uint16_t>((1 << (hashValue64 & 15)) | (1 << ((hashValue64 >> 8) & 15)));
    }

    ExecutableCode* targetEc = func->m_executable.As();

    // Figure out if we shall transition to closure call mode, we do this when we notice that
    // the passed-in call target has the same ExecutableCode as an existing cached target
    //
    bool shouldTransitToCCMode = false;
    if (unlikely((m_bloomFilter & bloomFilterMask) == bloomFilterMask))
    {
        SpdsPtr<JitCallInlineCacheEntry> linkListNode = m_linkedListHead;
        // if the IC site is empty, m_bloomFilter should be 0 and the above check shall never pass
        //
        assert(!linkListNode.IsInvalidPtr());
        do {
            JitCallInlineCacheEntry* entry = linkListNode.AsPtr();
            assert(entry->GetIcTraitKind() == dcIcTraitKind);
            if (entry->GetTargetExecutableCodeKnowingDirectCall() == targetEc)
            {
                shouldTransitToCCMode = true;
                break;
            }
            linkListNode = entry->m_callSiteNextNode;
        } while (!linkListNode.IsInvalidPtr());
    }

#ifndef NDEBUG
    // In debug mode, validate that the shouldTransitToCCMode decision is correct by brute force,
    // and also validate that assorted information of the linked list is as expected
    //
    {
        std::unordered_set<ExecutableCode*> checkUnique;
        bool goldDecision = false;
        SpdsPtr<JitCallInlineCacheEntry> linkListNode = m_linkedListHead;
        while (!linkListNode.IsInvalidPtr())
        {
            JitCallInlineCacheEntry* entry = linkListNode.AsPtr();
            assert(entry->GetIcTraitKind() == dcIcTraitKind);

            // We should never reach here if the IC ought to hit
            //
            assert(entry->m_entity.IsUserHeapPointer());
            assert(entry->m_entity.As() != func);

            // All the ExecutableCode in the IC list should be distinct
            //
            ExecutableCode* ec = entry->GetTargetExecutableCodeKnowingDirectCall();
            assert(!checkUnique.count(ec));
            checkUnique.insert(ec);

            if (ec == targetEc)
            {
                goldDecision = true;
            }

            linkListNode = entry->m_callSiteNextNode;
        }
        assert(checkUnique.size() == m_numEntries);
        assert(goldDecision == shouldTransitToCCMode);
    }
#endif

    if (likely(!shouldTransitToCCMode))
    {
        // No transition to closure-call mode, just create a new IC entry for the target
        //
        m_bloomFilter |= bloomFilterMask;
        m_numEntries++;

        JitCallInlineCacheEntry* newEntry = JitCallInlineCacheEntry::Create(vm,
                                                                            targetEc,
                                                                            m_linkedListHead /*callSiteNextNode*/,
                                                                            func,
                                                                            dcIcTraitKind);
        TCSet(m_linkedListHead, SpdsPtr<JitCallInlineCacheEntry> { newEntry });

        *transitedToCCMode = 0;
        return newEntry->GetJitRegionStart();
    }

    // We need to transit to closure-call mode
    //
    {
        // Invalidate all existing ICs
        //
        SpdsPtr<JitCallInlineCacheEntry> node = m_linkedListHead;
        assert(!node.IsInvalidPtr());
        do {
            JitCallInlineCacheEntry* entry = node.AsPtr();
            node = entry->m_callSiteNextNode;
            entry->Destroy(vm);
        } while (!node.IsInvalidPtr());
    }

    // Create the new IC entry
    //
    JitCallInlineCacheEntry* entry = JitCallInlineCacheEntry::Create(vm,
                                                                     targetEc,
                                                                     SpdsPtr<JitCallInlineCacheEntry> { 0 } /*callSiteNextNode*/,
                                                                     targetEc,
                                                                     dcIcTraitKind + 1 /*icTraitKind*/);
    TCSet(m_linkedListHead, SpdsPtr<JitCallInlineCacheEntry> { entry });
    m_mode = (m_numEntries > 1) ? Mode::ClosureCallWithMoreThanOneTargetObserved : Mode::ClosureCall;
    m_numEntries = 1;
    m_bloomFilter = 0;

    *transitedToCCMode = 1;
    return entry->GetJitRegionStart();
}

void* WARN_UNUSED JitCallInlineCacheSite::InsertInClosureCallMode(uint16_t dcIcTraitKind, FunctionObject* func)
{
    assert(m_numEntries < x_maxEntries);
    assert(m_mode == Mode::ClosureCall || m_mode == Mode::ClosureCallWithMoreThanOneTargetObserved);
    assert(reinterpret_cast<UserHeapGcObjectHeader*>(func)->m_type == HeapEntityType::Function);

    VM* vm = VM::GetActiveVMForCurrentThread();
    ExecutableCode* targetEc = func->m_executable.As();

#ifndef NDEBUG
    // In debug mode, validate that assorted information of the linked list is as expected
    //
    {
        std::unordered_set<ExecutableCode*> checkUnique;
        SpdsPtr<JitCallInlineCacheEntry> linkListNode = m_linkedListHead;
        while (!linkListNode.IsInvalidPtr())
        {
            JitCallInlineCacheEntry* entry = linkListNode.AsPtr();
            assert(entry->GetIcTraitKind() == dcIcTraitKind + 1);
            assert(!entry->GetIcTrait()->m_isDirectCallMode);

            // We should never reach here if the IC ought to hit
            //
            ExecutableCode* ec = entry->GetTargetExecutableCode();
            assert(ec != targetEc);

            // All the ExecutableCode in the IC list should be distinct
            //
            assert(!checkUnique.count(ec));
            checkUnique.insert(ec);

            linkListNode = entry->m_callSiteNextNode;
        }
        assert(checkUnique.size() == m_numEntries);
    }
#endif

    // Create the new IC entry
    //
    JitCallInlineCacheEntry* entry = JitCallInlineCacheEntry::Create(vm,
                                                                     targetEc,
                                                                     m_linkedListHead /*callSiteNextNode*/,
                                                                     targetEc,
                                                                     dcIcTraitKind + 1 /*icTraitKind*/);
    TCSet(m_linkedListHead, SpdsPtr<JitCallInlineCacheEntry> { entry });
    m_numEntries++;
    return entry->GetJitRegionStart();
}

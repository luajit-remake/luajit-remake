#include "bytecode.h"
#include "lj_opcode_info.h"
#include "deegen_def_lib_func_api.h"

#include "bytecode_builder.h"
#include "generated/get_guest_language_function_interpreter_entry_point.h"
#include "json_utils.h"

constexpr bool x_json_parser_force_use_double = true;

CodeBlock* WARN_UNUSED CodeBlock::Create2(VM* vm, UnlinkedCodeBlock* ucb, UserHeapPointer<TableObject> globalObject)
{
    size_t sizeToAllocate = GetTrailingArrayOffset() + RoundUpToMultipleOf<8>(ucb->m_bytecodeMetadataLength) + sizeof(TValue) * ucb->m_cstTableLength;
    uint8_t* addressBegin = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(static_cast<uint32_t>(sizeToAllocate)).AsNoAssert<uint8_t>());
    memcpy(addressBegin, ucb->m_cstTable, sizeof(TValue) * ucb->m_cstTableLength);

    CodeBlock* cb = reinterpret_cast<CodeBlock*>(addressBegin + sizeof(TValue) * ucb->m_cstTableLength);
    SystemHeapGcObjectHeader::Populate<ExecutableCode*>(cb);
    cb->m_hasVariadicArguments = ucb->m_hasVariadicArguments;
    cb->m_numFixedArguments = ucb->m_numFixedArguments;
    cb->m_bytecode = new uint8_t[ucb->m_bytecodeLength];
    memcpy(cb->m_bytecode, ucb->m_bytecode, ucb->m_bytecodeLength);
    cb->m_bestEntryPoint = reinterpret_cast<InterpreterFn>(generated::GetGuestLanguageFunctionEntryPointForInterpreter(ucb->m_hasVariadicArguments, ucb->m_numFixedArguments));
    cb->m_globalObject = globalObject;
    cb->m_stackFrameNumSlots = ucb->m_stackFrameNumSlots;
    cb->m_numUpvalues = ucb->m_numUpvalues;
    cb->m_bytecodeLength = ucb->m_bytecodeLength;
    cb->m_bytecodeMetadataLength = ucb->m_bytecodeMetadataLength;
    cb->m_baselineCodeBlock = nullptr;
    cb->m_floCodeBlock = nullptr;
    cb->m_owner = ucb;
    return cb;
}

DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_print);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_error);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_pcall);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_xpcall);

UserHeapPointer<TableObject> CreateGlobalObject2(VM* vm)
{
    HeapPtr<TableObject> globalObject = TableObject::CreateEmptyGlobalObject(vm);

    auto insertField = [&](HeapPtr<TableObject> r, const char* propName, TValue value)
    {
        UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(propName, static_cast<uint32_t>(strlen(propName)));
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(r, hs /*prop*/, icInfo /*out*/);
        TableObject::PutById(r, hs.As<void>(), value, icInfo);
    };

    auto insertCFunc = [&](HeapPtr<TableObject> r, const char* propName, void* func) -> UserHeapPointer<FunctionObject>
    {
        UserHeapPointer<FunctionObject> funcObj = FunctionObject::CreateCFunc(vm, ExecutableCode::CreateCFunction2(vm, func));
        insertField(r, propName, TValue::CreatePointer(funcObj));
        return funcObj;
    };

#if 0
    auto insertObject = [&](HeapPtr<TableObject> r, const char* propName, uint8_t inlineCapacity) -> HeapPtr<TableObject>
    {
        SystemHeapPointer<Structure> initialStructure = Structure::GetInitialStructureForInlineCapacity(vm, inlineCapacity);
        UserHeapPointer<TableObject> o = TableObject::CreateEmptyTableObject(vm, TranslateToRawPointer(vm, initialStructure.As()), 0 /*initialButterflyArrayPartCapacity*/);
        insertField(r, propName, TValue::CreatePointer(o));
        return o.As();
    };
#endif

    insertField(globalObject, "_G", TValue::CreatePointer(UserHeapPointer<TableObject> { globalObject }));

    insertCFunc(globalObject, "print", DEEGEN_CODE_POINTER_FOR_LIB_FUNC(base_print));
    UserHeapPointer<FunctionObject> baseDotError = insertCFunc(globalObject, "error", DEEGEN_CODE_POINTER_FOR_LIB_FUNC(base_error));
    vm->InitLibBaseDotErrorFunctionObject(TValue::Create<tFunction>(baseDotError.As()));
    insertCFunc(globalObject, "pcall", DEEGEN_CODE_POINTER_FOR_LIB_FUNC(base_pcall));
    insertCFunc(globalObject, "xpcall", DEEGEN_CODE_POINTER_FOR_LIB_FUNC(base_xpcall));

    return globalObject;
}

void VM::LaunchScript2(ScriptModule* module)
{
    CoroutineRuntimeContext* rc = GetRootCoroutine();
    HeapPtr<CodeBlock> cbHeapPtr = static_cast<HeapPtr<CodeBlock>>(TCGet(module->m_defaultEntryPoint.As()->m_executable).As());
    CodeBlock* cb = TranslateToRawPointer(cbHeapPtr);
    rc->m_codeBlock = cb;
    assert(cb->m_numFixedArguments == 0);
    StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(rc->m_stackBegin);
    sfh->m_caller = nullptr;
    // TODO: we need to fix this once we switch to GHC convention
    //
    sfh->m_retAddr = reinterpret_cast<void*>(LaunchScriptReturnEndpoint);
    sfh->m_func = module->m_defaultEntryPoint.As();
    sfh->m_callerBytecodeOffset = 0;
    sfh->m_numVariadicArguments = 0;
    void* stackbase = sfh + 1;
    // TODO: we need to fix this once we switch to GHC convention, we need to provide a wrapper for this...
    //
    // Currently the format expected by the entry function is 'coroCtx, stackbase, numArgs, cbHeapPtr, isMustTail
    //
    using Fn = void(*)(CoroutineRuntimeContext* coroCtx, void* stackBase, size_t numArgs, HeapPtr<CodeBlock> cbHeapPtr, size_t isMustTail);
    // TODO: remove this cast after we switch to the new interpreter
    //
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"
    Fn entryPoint = reinterpret_cast<Fn>(cb->m_bestEntryPoint);
#pragma clang diagnostic pop
    entryPoint(rc, stackbase, 0 /*numArgs*/, cbHeapPtr, 0 /*isMustTail*/);
}

ScriptModule* WARN_UNUSED ScriptModule::ParseFromJSON2(VM* vm, UserHeapPointer<TableObject> globalObject, const std::string& content)
{
    using namespace DeegenBytecodeBuilder;

    json module = json::parse(content);
    TestAssert(module.is_object());
    TestAssert(module.count("ChunkName") && module["ChunkName"].is_string());
    ScriptModule* r = new ScriptModule;
    r->m_name = module["ChunkName"].get<std::string>();
    r->m_defaultGlobalObject = globalObject;

    TestAssert(module.count("FunctionPrototypes") && module["FunctionPrototypes"].is_array());
    TestAssert(module["FunctionPrototypes"].size() > 0);
    for (auto& j : module["FunctionPrototypes"])
    {
        UnlinkedCodeBlock* ucb = new UnlinkedCodeBlock;
        ucb->m_defaultGlobalObject = globalObject.As();
        ucb->m_rareGOtoCBMap = nullptr;
        ucb->m_parent = nullptr;

        ucb->m_numFixedArguments = JSONCheckedGet<uint32_t>(j, "NumFixedParams");
        ucb->m_hasVariadicArguments = JSONCheckedGet<bool>(j, "TakesVarArg");
        ucb->m_stackFrameNumSlots = JSONCheckedGet<uint32_t>(j, "MaxFrameSize");

        TestAssert(j.count("Upvalues") && j["Upvalues"].is_array());
        uint32_t numUpvalues = SafeIntegerCast<uint32_t>(j["Upvalues"].size());
        ucb->m_numUpvalues = numUpvalues;

        // We always insert 3 constants 'nil', 'false', 'true', to make things easier
        //
        TestAssert(j.count("NumberConstants") && j["NumberConstants"].is_array());
        size_t numNumberConstants = j["NumberConstants"].size() + 3;
        ucb->m_numNumberConstants = SafeIntegerCast<uint32_t>(numNumberConstants);

        TestAssert(j.count("ObjectConstants") && j["ObjectConstants"].is_array());
        size_t numObjectConstants = j["ObjectConstants"].size();

        ucb->m_upvalueInfo = new UpvalueMetadata[ucb->m_numUpvalues];

        {
            uint32_t i = 0;
            for (auto u : j["Upvalues"])
            {
                bool isParentLocal = JSONCheckedGet<bool>(u, "IsParentLocal");
                ucb->m_upvalueInfo[i].m_isParentLocal = isParentLocal;
                if (isParentLocal)
                {
                    ucb->m_upvalueInfo[i].m_isImmutable = JSONCheckedGet<bool>(u, "IsImmutable");
                    ucb->m_upvalueInfo[i].m_slot = JSONCheckedGet<uint32_t>(u, "ParentLocalOrdinal");
                }
                else
                {
                    ucb->m_upvalueInfo[i].m_isImmutable = false;
                    ucb->m_upvalueInfo[i].m_slot = JSONCheckedGet<uint32_t>(u, "ParentUpvalueOrdinal");
                }
                i++;
            }
            TestAssert(i == ucb->m_numUpvalues);
        }

        ucb->m_cstTableLength = SafeIntegerCast<uint32_t>(numObjectConstants + ucb->m_numNumberConstants);
        ucb->m_cstTable = new BytecodeConstantTableEntry[ucb->m_cstTableLength];

        {
            uint32_t i = ucb->m_cstTableLength;
            {
                i--;
                ucb->m_cstTable[i].m_tv = TValue::Nil();
                i--;
                ucb->m_cstTable[i].m_tv = TValue::CreateFalse();
                i--;
                ucb->m_cstTable[i].m_tv = TValue::CreateTrue();
            }
            for (auto& c : j["NumberConstants"])
            {
                i--;
                std::string ty = JSONCheckedGet<std::string>(c, "Type");
                TestAssert(ty == "Int32" || ty == "Double");
                if (ty == "Int32")
                {
                    int32_t value = JSONCheckedGet<int32_t>(c, "Value");
                    TValue tv;
                    if (x_json_parser_force_use_double)
                    {
                        tv = TValue::CreateDouble(value);
                    }
                    else
                    {
                        tv = TValue::CreateInt32(value);
                    }
                    ucb->m_cstTable[i].m_tv = tv;
                }
                else
                {
                    double value = JSONCheckedGet<double>(c, "Value");
                    ucb->m_cstTable[i].m_tv = TValue::CreateDouble(value);
                }
            }
            TestAssert(i == numObjectConstants);

            for (auto& c : j["ObjectConstants"])
            {
                i--;
                std::string ty = JSONCheckedGet<std::string>(c, "Type");
                ReleaseAssert(ty == "String" || ty == "FunctionPrototype" || ty == "Table");
                if (ty == "String")
                {
                    std::string data = JSONCheckedGet<std::string>(c, "Value");
                    UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(data.c_str(), static_cast<uint32_t>(data.length()));
                    ucb->m_cstTable[i].m_tv = TValue::CreatePointer(hs);
                }
                else if (ty == "FunctionPrototype")
                {
                    uint32_t ordinal = JSONCheckedGet<uint32_t>(c, "Value");
                    TestAssert(ordinal < r->m_unlinkedCodeBlocks.size());
                    UnlinkedCodeBlock* childUcb = r->m_unlinkedCodeBlocks[ordinal];
                    TestAssert(childUcb->m_parent == nullptr);
                    childUcb->m_parent = ucb;
                    ucb->m_cstTable[i].m_ucb = childUcb;
                }
                else
                {
                    TestAssert(ty == "Table");
                    TestAssert(c.count("Value") && c["Value"].is_array());
                    auto& tab = c["Value"];
                    uint32_t numNamedProps = 0;
                    uint32_t initalArraySize = 0;
                    TValue booleanValues[2] = { TValue::Nil(), TValue::Nil() };    // holding the value for key 'false' and 'true'
                    // Insert string index in alphabetic order, to maximize the chance of reusing structures
                    //
                    std::map<std::string, TValue> namedPropKVs;
                    // Insert positive numeric index in increasing order, so we don't break continuity
                    //
                    std::map<int32_t, TValue> positiveIndexKVs;

                    auto convertTableValue = [&](json& tabEntryValue) -> TValue
                    {
                        std::string vty = JSONCheckedGet<std::string>(tabEntryValue, "Type");
                        TestAssert(vty == "Boolean" || vty == "String" || vty == "Int32" || vty == "Double");
                        if (vty == "Boolean")
                        {
                            bool value = JSONCheckedGet<bool>(tabEntryValue, "Value");
                            return TValue::CreateMIV(MiscImmediateValue::CreateBoolean(value));
                        }
                        else if (vty == "String")
                        {
                            std::string value = JSONCheckedGet<std::string>(tabEntryValue, "Value");
                            UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(value.c_str(), static_cast<uint32_t>(value.length()));
                            return TValue::CreatePointer(hs);
                        }
                        else if (vty == "Int32")
                        {
                            int32_t value = JSONCheckedGet<int32_t>(tabEntryValue, "Value");
                            TValue tv;
                            if (x_json_parser_force_use_double)
                            {
                                tv = TValue::CreateDouble(value);
                            }
                            else
                            {
                                tv = TValue::CreateInt32(value);
                            }
                            return tv;
                        }
                        else
                        {
                            TestAssert(vty == "Double");
                            double value = JSONCheckedGet<double>(tabEntryValue, "Value");
                            return TValue::CreateDouble(value);
                        }
                    };

                    for (auto& tabEntry : tab)
                    {
                        TestAssert(tabEntry.count("EntryKey") && tabEntry["EntryKey"].is_object());
                        auto& tabEntryKey = tabEntry["EntryKey"];
                        TestAssert(tabEntry.count("EntryValue") && tabEntry["EntryValue"].is_object());
                        auto& tabEntryValue = tabEntry["EntryValue"];

                        std::string kty = JSONCheckedGet<std::string>(tabEntryKey, "Type");
                        TestAssert(kty == "Boolean" || kty == "String" || kty == "Int32" || kty == "Double");
                        if (kty == "String")
                        {
                            numNamedProps++;
                            std::string key = JSONCheckedGet<std::string>(tabEntryKey, "Value");
                            TestAssert(!namedPropKVs.count(key));
                            namedPropKVs[key] = convertTableValue(tabEntryValue);
                        }
                        else if (kty == "Boolean")
                        {
                            numNamedProps++;
                            bool key = JSONCheckedGet<bool>(tabEntryKey, "Value");
                            TestAssert(booleanValues[key].IsNil());
                            booleanValues[key] = convertTableValue(tabEntryValue);
                        }
                        else if (kty == "Int32")
                        {
                            int32_t key = JSONCheckedGet<int32_t>(tabEntryKey, "Value");
                            if (key > 0)
                            {
                                uint32_t ukey = static_cast<uint32_t>(key);
                                if (ukey <= ArrayGrowthPolicy::x_alwaysVectorCutoff)
                                {
                                    initalArraySize = std::max(initalArraySize, ukey);
                                }
                                TestAssert(!positiveIndexKVs.count(key));
                                positiveIndexKVs[key] = convertTableValue(tabEntryValue);
                            }
                        }
                    }

                    uint32_t additionalNamedKeys = JSONCheckedGet<uint32_t>(c, "AdditionalNamedKeys");
                    uint32_t inlineCapcitySize = numNamedProps + additionalNamedKeys;

                    // TODO: if we have more than x_maxSlot keys, we'd better make it CacheableDictionary right now
                    //
                    SystemHeapPointer<Structure> structure = Structure::GetInitialStructureForInlineCapacity(vm, inlineCapcitySize);
                    HeapPtr<TableObject> obj = TableObject::CreateEmptyTableObject(vm, TranslateToRawPointer(vm, structure.As()), initalArraySize);

                    // Now, insert all the string properties in alphabetic order
                    //
                    uint32_t numPropsInserted = 0;
                    for (auto& it : namedPropKVs)
                    {
                        std::string s = it.first;
                        TValue val = it.second;
                        UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(s.c_str(), static_cast<uint32_t>(s.length()));

                        PutByIdICInfo icInfo;
                        TableObject::PreparePutById(obj, hs, icInfo /*out*/);
                        TableObject::PutById(obj, hs.As<void>(), val, icInfo);

                        numPropsInserted++;
                    }

                    // Insert the boolean properties
                    //
                    for (uint32_t k = 0; k <= 1; k++)
                    {
                        if (!booleanValues[k].IsNil())
                        {
                            UserHeapPointer<HeapString> key = vm->GetSpecialKeyForBoolean(static_cast<bool>(k));
                            TValue val = booleanValues[k];

                            PutByIdICInfo icInfo;
                            TableObject::PreparePutById(obj, key, icInfo /*out*/);
                            TableObject::PutById(obj, key.As<void>(), val, icInfo);

                            numPropsInserted++;
                        }
                    }

                    TestAssert(numPropsInserted == numNamedProps);

                    // Now, insert the positive range array elements in increasing order
                    //
                    uint32_t numericIndexInserted = 0;
                    for (auto& it : positiveIndexKVs)
                    {
                        int32_t idx = it.first;
                        TValue val = it.second;
                        TestAssert(idx > 0);
                        TableObject::RawPutByValIntegerIndex(obj, idx, val);
                        numericIndexInserted++;
                    }

                    // Finally, insert everything else
                    //
                    for (auto& tabEntry : tab)
                    {
                        TestAssert(tabEntry.count("EntryKey") && tabEntry["EntryKey"].is_object());
                        auto& tabEntryKey = tabEntry["EntryKey"];
                        TestAssert(tabEntry.count("EntryValue") && tabEntry["EntryValue"].is_object());
                        auto& tabEntryValue = tabEntry["EntryValue"];

                        std::string kty = JSONCheckedGet<std::string>(tabEntryKey, "Type");
                        TestAssert(kty == "Boolean" || kty == "String" || kty == "Int32" || kty == "Double");
                        if (kty == "Int32")
                        {
                            int32_t key = JSONCheckedGet<int32_t>(tabEntryKey, "Value");
                            if (key <= 0)   // positive index has been inserted earlier
                            {
                                TValue val = convertTableValue(tabEntryValue);
                                TableObject::RawPutByValIntegerIndex(obj, key, val);
                                numericIndexInserted++;
                            }
                        }
                        else if (kty == "Double")
                        {
                            double key = JSONCheckedGet<double>(tabEntryKey, "Value");
                            assert(!IsNaN(key));
                            TValue val = convertTableValue(tabEntryValue);
                            TableObject::RawPutByValDoubleIndex(obj, key, val);
                            numericIndexInserted++;
                        }
                    }

                    TestAssert(numericIndexInserted + numPropsInserted == tab.size());
                    std::ignore = numPropsInserted;
                    std::ignore = numericIndexInserted;

                    ucb->m_cstTable[i].m_tv = TValue::CreatePointer(UserHeapPointer<TableObject>(obj));

                    // TODO: we should assert that 'obj' contains exactly everything we expected
                }
            }
            TestAssert(i == 0);
        }

        auto priCst = [&](int32_t ord) -> CsTab
        {
            TestAssert(0 <= ord && ord < 3);
            return CsTab(static_cast<size_t>(ord));
        };

        auto numCst = [&](int32_t ord) -> CsTab
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(numNumberConstants) - 3);
            return CsTab(static_cast<size_t>(ord + 3));
        };

        auto objCst = [&](int32_t ord) -> CsTab
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(numObjectConstants));
            return CsTab(numNumberConstants + static_cast<size_t>(ord));
        };

        auto local = [&](int32_t ord) -> Local
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(ucb->m_stackFrameNumSlots));
            return Local(static_cast<size_t>(ord));
        };

        std::vector<size_t> bytecodeLocation;
        std::vector<std::pair<size_t /*targetBytecodeOrdinal*/, BranchTargetPopulator>> jumpPatches;

        BytecodeBuilder bw;
        TestAssert(j.count("Bytecode") && j["Bytecode"].is_array());
        auto& bytecodeList = j["Bytecode"];
        auto it = bytecodeList.begin();

        // In LuaJIT's format many bytecode must be followed by a JMP bytecode.
        // In our bytecode format we don't have this restriction.
        // This function decodes and skips the trailing JMP bytecode and returns the bytecode ordinal the JMP bytecode targets
        //
        auto decodeAndSkipNextJumpBytecode = [&]() WARN_UNUSED -> size_t
        {
            int32_t selfBytecodeOrdinal = static_cast<int32_t>(it - bytecodeList.begin());

            it++;
            TestAssert(it < bytecodeList.end());
            auto& nextBytecode = *it;
            TestAssert(JSONCheckedGet<std::string>(nextBytecode, "OpCode") == "JMP");
            TestAssert(nextBytecode.count("OpData") && nextBytecode["OpData"].is_array() && nextBytecode["OpData"].size() == 2);

            // The 'JMP' bytecode immediately following the comparsion should never be a valid jump target
            //
            bytecodeLocation.push_back(static_cast<size_t>(-1));

            auto& e = nextBytecode["OpData"][1];
            TestAssert(e.is_number_integer() || e.is_number_unsigned());
            int32_t jumpTargetOffset;
            if (e.is_number_integer())
            {
                jumpTargetOffset = SafeIntegerCast<int32_t>(e.get<int64_t>());
            }
            else
            {
                jumpTargetOffset = SafeIntegerCast<int32_t>(e.get<uint64_t>());
            }
            int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + 1 + jumpTargetOffset;
            TestAssert(jumpBytecodeOrdinal >= 0);

            return static_cast<size_t>(jumpBytecodeOrdinal);
        };

        for (/*no-op*/; it != bytecodeList.end(); it++)
        {
            bytecodeLocation.push_back(bw.GetCurLength());
            auto& b = *it;
            std::string opcodeString = JSONCheckedGet<std::string>(b, "OpCode");
            LJOpcode opcode = GetOpcodeFromString(opcodeString);
            TestAssert(b.count("OpData") && b["OpData"].is_array());
            std::vector<int> opdata;
            for (auto& e : b["OpData"])
            {
                TestAssert(e.is_number_integer() || e.is_number_unsigned());
                int32_t e32;
                if (e.is_number_integer())
                {
                    e32 = SafeIntegerCast<int32_t>(e.get<int64_t>());
                }
                else
                {
                    e32 = SafeIntegerCast<int32_t>(e.get<uint64_t>());
                }
                opdata.push_back(e32);
            }
            TestAssert(opdata.size() == b["OpData"].size());

            switch (opcode)
            {
            case LJOpcode::ADDVN:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateAdd({
                    .lhs = local(opdata[1]),
                    .rhs = numCst(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::SUBVN:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateSub({
                    .lhs = local(opdata[1]),
                    .rhs = numCst(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::MULVN:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateMul({
                    .lhs = local(opdata[1]),
                    .rhs = numCst(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::DIVVN:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateDiv({
                    .lhs = local(opdata[1]),
                    .rhs = numCst(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::MODVN:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateMod({
                    .lhs = local(opdata[1]),
                    .rhs = numCst(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::ADDNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateAdd({
                    .lhs = numCst(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::SUBNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateSub({
                    .lhs = numCst(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::MULNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateMul({
                    .lhs = numCst(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::DIVNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateDiv({
                    .lhs = numCst(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::MODNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateMod({
                    .lhs = numCst(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::ADDVV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateAdd({
                    .lhs = local(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::SUBVV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateSub({
                    .lhs = local(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::MULVV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateMul({
                    .lhs = local(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::DIVVV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateDiv({
                    .lhs = local(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::MODVV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateMod({
                    .lhs = local(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::POW:
            {
                TestAssert(opdata.size() == 3);
                bw.CreatePow({
                    .lhs = local(opdata[1]),
                    .rhs = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::CAT:
            {
                ReleaseAssert(false && "unimplemented");
            }
            case LJOpcode::KSHORT:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateSetConstInt16({
                    .value = static_cast<int16_t>(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::ISLT:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                BranchTargetPopulator p = bw.CreateBranchIfLT({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                jumpPatches.push_back(std::make_pair(brTarget, p));
                break;
            }
            case LJOpcode::ISGE:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                BranchTargetPopulator p = bw.CreateBranchIfNLT({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                jumpPatches.push_back(std::make_pair(brTarget, p));
                break;
            }
            case LJOpcode::ISLE:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                BranchTargetPopulator p = bw.CreateBranchIfLE({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                jumpPatches.push_back(std::make_pair(brTarget, p));
                break;
            }
            case LJOpcode::ISGT:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                BranchTargetPopulator p = bw.CreateBranchIfNLE({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                jumpPatches.push_back(std::make_pair(brTarget, p));
                break;
            }
            case LJOpcode::ISEQV:
            case LJOpcode::ISNEV:
            case LJOpcode::ISEQS:
            case LJOpcode::ISNES:
            case LJOpcode::ISEQN:
            case LJOpcode::ISNEN:
            case LJOpcode::ISEQP:
            case LJOpcode::ISNEP:
            case LJOpcode::ISTC:
            case LJOpcode::ISFC:
            case LJOpcode::IST:
            case LJOpcode::ISF:
            {
                ReleaseAssert(false && "unimplemented");
            }
            case LJOpcode::GGET:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateGlobalGet({
                    .index = objCst(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::GSET:
            {
                TestAssert(opdata.size() == 2);
                TestAssert(opdata.size() == 2);
                bw.CreateGlobalPut({
                    .index = objCst(opdata[1]),
                    .value = local(opdata[0])
                });
                break;
            }
            case LJOpcode::RETM:
            {
                TestAssert(opdata.size() == 2);
                // For RETM, D holds # of fixed return values
                //
                uint16_t numReturnValues = SafeIntegerCast<uint16_t>(opdata[1]);
                bw.CreateRetM({
                    .retStart = local(opdata[0]),
                    .numRet = numReturnValues
                });
                break;
            }
            case LJOpcode::RET:
            {
                TestAssert(opdata.size() == 2);
                // For RET, D holds 1 + # ret values
                //
                TestAssert(opdata[1] >= 1);
                uint16_t numReturnValues = SafeIntegerCast<uint16_t>(opdata[1] - 1);
                bw.CreateRet({
                    .retStart = local(opdata[0]),
                    .numRet = numReturnValues
                });
                break;
            }
            case LJOpcode::RET0:
            {
                bw.CreateRet0();
                break;
            }
            case LJOpcode::RET1:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateRet({
                    .retStart = local(opdata[0]),
                    .numRet = 1
                });
                break;
            }
            case LJOpcode::CALLM:
            {
                TestAssert(opdata.size() == 3);
                // B stores # fixed results + 1, and if opdata[1] == 0, it stores all results
                // Coincidentally we use -1 to represent 'store all results', so we can simply subtract 1
                //
                int32_t numResults = opdata[1] - 1;
                // For CALLM, C holds # of fixed params
                //
                uint32_t numFixedParams = SafeIntegerCast<uint32_t>(opdata[2]);
                bw.CreateCallM({
                    .base = local(opdata[0]),
                    .numArgs = numFixedParams,
                    .numRets = numResults
                });
                break;
            }
            case LJOpcode::CALL:
            {
                TestAssert(opdata.size() == 3);
                // B stores # fixed results + 1, and if opdata[1] == 0, it stores all results
                // Coincidentally we use -1 to represent 'store all results', so we can simply subtract 1
                //
                int32_t numResults = opdata[1] - 1;
                // For CALL, C holds 1 + # of fixed params
                //
                uint32_t numFixedParams = SafeIntegerCast<uint32_t>(opdata[2] - 1);
                bw.CreateCall({
                    .base = local(opdata[0]),
                    .numArgs = numFixedParams,
                    .numRets = numResults
                });
                break;
            }
            case LJOpcode::CALLMT:
            {
                TestAssert(opdata.size() == 2);
                // For CALLMT, D holds # of fixed params
                //
                uint32_t numFixedParams = SafeIntegerCast<uint32_t>(opdata[1]);
                bw.CreateCallMT({
                    .base = local(opdata[0]),
                    .numArgs = numFixedParams
                });
                break;
            }
            case LJOpcode::CALLT:
            {
                TestAssert(opdata.size() == 2);
                uint32_t numFixedParams = SafeIntegerCast<uint32_t>(opdata[1] - 1);
                bw.CreateCallT({
                    .base = local(opdata[0]),
                    .numArgs = numFixedParams
                });
                break;
            }
            case LJOpcode::MOV:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateMov({
                    .input = local(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::NOT:
            {
                ReleaseAssert(false && "unimplemented");
            }
            case LJOpcode::UNM:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateUnaryMinus({
                    .input = local(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::LEN:
            {
                ReleaseAssert(false && "unimplemented");
            }
            case LJOpcode::KSTR:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateMov({
                    .input = objCst(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::KNUM:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateMov({
                    .input = numCst(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::KPRI:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateMov({
                    .input = priCst(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::FNEW:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateNewClosure({
                    .unlinkedCb = objCst(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TNEW:
            {
                TestAssert(opdata.size() == 2);
                // For TNEW, the second parameter should be interpreted as uint32_t and split into two parts
                //
                uint32_t tdata = static_cast<uint32_t>(opdata[1]);
                uint32_t arrayPartHint = tdata & 2047;
                uint32_t hashPartLog2Hint = tdata >> 11;
                uint32_t inlineCapacity;
                // TODO: refine this strategy
                //
                if (hashPartLog2Hint == 0)
                {
                    inlineCapacity = 0;
                }
                else if (hashPartLog2Hint <= 4)
                {
                    inlineCapacity = (1U << hashPartLog2Hint);
                }
                else if (hashPartLog2Hint <= 8)
                {
                    inlineCapacity = (1U << (hashPartLog2Hint - 1));
                }
                else
                {
                    inlineCapacity = 0;
                }

                uint8_t stepping = Structure::GetInitialStructureSteppingForInlineCapacity(inlineCapacity);
                // Create the structure now, so we can call GetInitialStructureForSteppingKnowingAlreadyBuilt at runtime
                //
                std::ignore = Structure::GetInitialStructureForStepping(vm, stepping);

                bw.CreateTableNew({
                    .inlineStorageSizeStepping = stepping,
                    .arrayPartSizeHint = static_cast<uint16_t>(arrayPartHint),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TDUP:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateTableDup({
                    .src = objCst(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TGETV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateTableGetByVal({
                    .base = local(opdata[1]),
                    .index = local(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TGETS:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateTableGetById({
                    .base = local(opdata[1]),
                    .index = objCst(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TSETV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateTablePutByVal({
                    .base = local(opdata[1]),
                    .index = local(opdata[2]),
                    .value = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TSETS:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateTablePutById({
                    .base = local(opdata[1]),
                    .index = objCst(opdata[2]),
                    .value = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TGETB:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateTableGetByImm({
                    .base = local(opdata[1]),
                    .index = SafeIntegerCast<int16_t>(opdata[2]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TSETB:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateTablePutByImm({
                    .base = local(opdata[1]),
                    .index = SafeIntegerCast<int16_t>(opdata[2]),
                    .value = local(opdata[0])
                });
                break;
            }
            case LJOpcode::TSETM:
            case LJOpcode::UGET:
            case LJOpcode::USETV:
            case LJOpcode::USETS:
            case LJOpcode::USETN:
            case LJOpcode::USETP:
            case LJOpcode::UCLO:
            case LJOpcode::FORI:
            case LJOpcode::FORL:
            case LJOpcode::LOOP:
            case LJOpcode::JMP:
            case LJOpcode::VARG:
            case LJOpcode::KNIL:
            case LJOpcode::ITERN:
            case LJOpcode::ITERC:
            case LJOpcode::ITERL:
            case LJOpcode::ISNEXT:
            {
                ReleaseAssert(false && "unimplemented");
            }
            case LJOpcode::KCDATA:
            case LJOpcode::ISTYPE:
            case LJOpcode::ISNUM:
            case LJOpcode::TGETR:
            case LJOpcode::TSETR:
            case LJOpcode::FUNCF:
            case LJOpcode::IFUNCF:
            case LJOpcode::JFUNCF:
            case LJOpcode::FUNCV:
            case LJOpcode::IFUNCV:
            case LJOpcode::JFUNCV:
            case LJOpcode::FUNCC:
            case LJOpcode::FUNCCW:
            case LJOpcode::JFORI:
            case LJOpcode::IFORL:
            case LJOpcode::JFORL:
            case LJOpcode::IITERL:
            case LJOpcode::JITERL:
            case LJOpcode::ILOOP:
            case LJOpcode::JLOOP:
            {
                // These opcodes should never be generated by LuaJIT parser
                //
                ReleaseAssert(false && "Unexpected opcode");
            }
            }
        }
        ReleaseAssert(bytecodeLocation.size() == bytecodeList.size());

        for (auto& jumpPatch : jumpPatches)
        {
            size_t ljBytecodeOrd = jumpPatch.first;
            BranchTargetPopulator p = jumpPatch.second;
            assert(ljBytecodeOrd < bytecodeLocation.size());
            size_t bytecodeOffset = bytecodeLocation[ljBytecodeOrd];
            assert(bytecodeOffset < bw.GetCurLength());
            p.PopulateBranchTarget(bw, bytecodeOffset);
        }

        std::pair<uint8_t*, size_t> bytecodeData = bw.GetBuiltBytecodeSequence();
        ucb->m_bytecode = bytecodeData.first;
        ucb->m_bytecodeLength = static_cast<uint32_t>(bytecodeData.second);
        ucb->m_bytecodeMetadataLength = 0;
        ucb->m_defaultCodeBlock = CodeBlock::Create2(vm, ucb, globalObject);
        r->m_unlinkedCodeBlocks.push_back(ucb);

        /*
        for (size_t i = 0; i < bytecodeLocation.size(); i++)
        {
            if (bytecodeLocation[i] != -1)
            {
                printf("Opcode %d at %p\n", static_cast<int>(ucb->m_defaultCodeBlock->m_bytecode[bytecodeLocation[i]]), static_cast<void*>(ucb->m_defaultCodeBlock->m_bytecode + bytecodeLocation[i]));
            }
        }
        */
    }

    UnlinkedCodeBlock* chunkFn = r->m_unlinkedCodeBlocks.back();
#ifdef TESTBUILD
    for (UnlinkedCodeBlock* ucb : r->m_unlinkedCodeBlocks)
    {
        TestAssertIff(ucb != chunkFn, ucb->m_parent != nullptr);
    }
#endif

    TestAssert(chunkFn->m_numUpvalues == 0);
    UserHeapPointer<FunctionObject> entryPointFunc = FunctionObject::Create(vm, UnlinkedCodeBlock::GetCodeBlock(chunkFn, globalObject));
    r->m_defaultEntryPoint = entryPointFunc;

    return r;
}

#include "runtime_utils.h"
#include "api_define_lib_function.h"

#include "bytecode_builder.h"
#include "json_utils.h"

#define LJ_OPCODE_LIST    \
    ISLT,   \
    ISGE,   \
    ISLE,   \
    ISGT,   \
    ISEQV,  \
    ISNEV,  \
    ISEQS,  \
    ISNES,  \
    ISEQN,  \
    ISNEN,  \
    ISEQP,  \
    ISNEP,  \
    ISTC,   \
    ISFC,   \
    IST,    \
    ISF,    \
    ISTYPE, \
    ISNUM,  \
    MOV,    \
    NOT,    \
    UNM,    \
    LEN,    \
    ADDVN,  \
    SUBVN,  \
    MULVN,  \
    DIVVN,  \
    MODVN,  \
    ADDNV,  \
    SUBNV,  \
    MULNV,  \
    DIVNV,  \
    MODNV,  \
    ADDVV,  \
    SUBVV,  \
    MULVV,  \
    DIVVV,  \
    MODVV,  \
    POW,    \
    CAT,    \
    KSTR,   \
    KCDATA, \
    KSHORT, \
    KNUM,   \
    KPRI,   \
    KNIL,   \
    UGET,   \
    USETV,  \
    USETS,  \
    USETN,  \
    USETP,  \
    UCLO,   \
    FNEW,   \
    TNEW,   \
    TDUP,   \
    GGET,   \
    GSET,   \
    TGETV,  \
    TGETS,  \
    TGETB,  \
    TGETR,  \
    TSETV,  \
    TSETS,  \
    TSETB,  \
    TSETM,  \
    TSETR,  \
    CALLM,  \
    CALL,   \
    CALLMT, \
    CALLT,  \
    ITERC,  \
    ITERN,  \
    VARG,   \
    ISNEXT, \
    RETM,   \
    RET,    \
    RET0,   \
    RET1,   \
    FORI,   \
    JFORI,  \
    FORL,   \
    IFORL,  \
    JFORL,  \
    ITERL,  \
    IITERL, \
    JITERL, \
    LOOP,   \
    ILOOP,  \
    JLOOP,  \
    JMP,    \
    FUNCF,  \
    IFUNCF, \
    JFUNCF, \
    FUNCV,  \
    IFUNCV, \
    JFUNCV, \
    FUNCC,  \
    FUNCCW

enum class LJOpcode
{
    LJ_OPCODE_LIST
};

#define macro(ljopcode) + 1
constexpr size_t x_numLJOpcodes = 0 PP_FOR_EACH(macro, LJ_OPCODE_LIST);
#undef macro

constexpr const char* x_LJOpcodeStrings[x_numLJOpcodes + 1] = {
#define macro(ljopcode) PP_STRINGIFY(ljopcode),
    PP_FOR_EACH(macro, LJ_OPCODE_LIST)
#undef macro
    ""
};

inline LJOpcode WARN_UNUSED GetOpcodeFromString(const std::string& s)
{
    for (uint32_t i = 0; i < x_numLJOpcodes; i++)
    {
        if (s == x_LJOpcodeStrings[i])
        {
            return static_cast<LJOpcode>(i);
        }
    }
    fprintf(stderr, "Bad opcode \"%s\"!\n", s.c_str());
    abort();
}

constexpr bool x_json_parser_force_use_double = true;

ScriptModule* WARN_UNUSED ScriptModule::ParseFromJSON(VM* vm, UserHeapPointer<TableObject> globalObject, const std::string& content)
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
        UnlinkedCodeBlock* ucb = UnlinkedCodeBlock::Create(vm, globalObject.As());

        ucb->m_numFixedArguments = JSONCheckedGet<uint32_t>(j, "NumFixedParams");
        ucb->m_hasVariadicArguments = JSONCheckedGet<bool>(j, "TakesVarArg");
        ucb->m_stackFrameNumSlots = JSONCheckedGet<uint32_t>(j, "MaxFrameSize");

        TestAssert(j.count("Upvalues") && j["Upvalues"].is_array());
        uint32_t numUpvalues = SafeIntegerCast<uint32_t>(j["Upvalues"].size());
        ucb->m_numUpvalues = numUpvalues;
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

        std::vector<TValue> numberCstList;
        TestAssert(j.count("NumberConstants") && j["NumberConstants"].is_array());
        [[maybe_unused]] size_t numNumberConstants = j["NumberConstants"].size();
        {
            for (auto& c : j["NumberConstants"])
            {
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
                    numberCstList.push_back(tv);
                }
                else
                {
                    double value = JSONCheckedGet<double>(c, "Value");
                    numberCstList.push_back(TValue::CreateDouble(value));
                }
            }
            TestAssert(numNumberConstants == numberCstList.size());
        }

        TestAssert(j.count("ObjectConstants") && j["ObjectConstants"].is_array());
        [[maybe_unused]] size_t numObjectConstants = j["ObjectConstants"].size();
        std::vector<TValue> objectCstList;
        {
            for (auto& c : j["ObjectConstants"])
            {
                std::string ty = JSONCheckedGet<std::string>(c, "Type");
                ReleaseAssert(ty == "String" || ty == "FunctionPrototype" || ty == "Table");
                if (ty == "String")
                {
                    std::string data = JSONCheckedGet<std::string>(c, "Value");
                    UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(data.c_str(), static_cast<uint32_t>(data.length()));
                    objectCstList.push_back(TValue::CreatePointer(hs));
                }
                else if (ty == "FunctionPrototype")
                {
                    uint32_t ordinal = JSONCheckedGet<uint32_t>(c, "Value");
                    TestAssert(ordinal < r->m_unlinkedCodeBlocks.size());
                    UnlinkedCodeBlock* childUcb = r->m_unlinkedCodeBlocks[ordinal];
                    TestAssert(childUcb->m_parent == nullptr);
                    childUcb->m_parent = ucb;
                    // TODO: this really shouldn't be that hacky..
                    //
                    TValue tmp;
                    tmp.m_value = reinterpret_cast<uint64_t>(childUcb);
                    objectCstList.push_back(tmp);
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

                    objectCstList.push_back(TValue::CreatePointer(UserHeapPointer<TableObject>(obj)));

                    // TODO: we should assert that 'obj' contains exactly everything we expected
                }
            }
            TestAssert(objectCstList.size() == numObjectConstants);
        }

        auto priCst = [&](int32_t ord)
        {
            TestAssert(0 <= ord && ord < 3);
            if (ord == 0)
            {
                return Cst<tNil>();
            }
            else if (ord == 1)
            {
                return Cst<tBool>(false);
            }
            else
            {
                return Cst<tBool>(true);
            }
        };

        auto numCst = [&](int32_t ord) -> TValue
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(numNumberConstants));
            return numberCstList[static_cast<size_t>(ord)];
        };

        auto objCst = [&](int32_t ord) -> TValue
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(numObjectConstants));
            return objectCstList[static_cast<size_t>(ord)];
        };

        auto local = [&](int32_t ord) -> Local
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(ucb->m_stackFrameNumSlots));
            return Local(static_cast<size_t>(ord));
        };

        std::vector<size_t> bytecodeLocation;
        std::vector<std::pair<size_t /*targetBytecodeOrdinal*/, size_t /*jumpBytecodePos*/>> jumpPatches;

        BytecodeBuilder bw;
        TestAssert(j.count("Bytecode") && j["Bytecode"].is_array());
        auto& bytecodeList = j["Bytecode"];
        auto it = bytecodeList.begin();

        auto getIntValue = [](const json& e) -> int32_t
        {
            TestAssert(e.is_number_integer() || e.is_number_unsigned());
            int32_t res;
            if (e.is_number_integer())
            {
                res = SafeIntegerCast<int32_t>(e.get<int64_t>());
            }
            else
            {
                res = SafeIntegerCast<int32_t>(e.get<uint64_t>());
            }
            return res;
        };

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

            int32_t jumpTargetOffset = getIntValue(nextBytecode["OpData"][1]);
            int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + 1 + jumpTargetOffset;
            TestAssert(jumpBytecodeOrdinal >= 0);
            return static_cast<size_t>(jumpBytecodeOrdinal);
        };

        // Similar to 'decodeAndSkipNextJumpBytecode', but skips the ITERL bytecode
        //
        auto decodeAndSkipNextITERLBytecode = [&]() WARN_UNUSED -> size_t
        {
            int32_t selfBytecodeOrdinal = static_cast<int32_t>(it - bytecodeList.begin());

            auto& curBytecode = *it;
            TestAssert(JSONCheckedGet<std::string>(curBytecode, "OpCode") == "ITERC" || JSONCheckedGet<std::string>(curBytecode, "OpCode") == "ITERN");
            TestAssert(curBytecode.count("OpData") && curBytecode["OpData"].is_array() && curBytecode["OpData"].size() > 0);
            [[maybe_unused]] int32_t curBase = getIntValue(curBytecode["OpData"][0]);

            it++;
            TestAssert(it < bytecodeList.end());
            auto& nextBytecode = *it;
            TestAssert(JSONCheckedGet<std::string>(nextBytecode, "OpCode") == "ITERL");
            TestAssert(nextBytecode.count("OpData") && nextBytecode["OpData"].is_array() && nextBytecode["OpData"].size() == 2);

            // This 'ITERL' bytecode should never be a valid jump target
            //
            bytecodeLocation.push_back(static_cast<size_t>(-1));

            // The 'ITERL' bytecode should have the same base as the ITERC/ITERN bytecode
            //
            [[maybe_unused]] int32_t base = getIntValue(nextBytecode["OpData"][0]);
            TestAssert(base == curBase);

            int32_t jumpTargetOffset = getIntValue(nextBytecode["OpData"][1]);
            int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + 1 + jumpTargetOffset;
            TestAssert(jumpBytecodeOrdinal >= 0);
            return static_cast<size_t>(jumpBytecodeOrdinal);
        };

        auto getBytecodeOrdinalOfJump = [&](int32_t offset) -> size_t
        {
            int32_t selfBytecodeOrdinal = static_cast<int32_t>(it - bytecodeList.begin());
            int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + offset;
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
                    .lhs = numCst(opdata[2]),
                    .rhs = local(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::SUBNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateSub({
                    .lhs = numCst(opdata[2]),
                    .rhs = local(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::MULNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateMul({
                    .lhs = numCst(opdata[2]),
                    .rhs = local(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::DIVNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateDiv({
                    .lhs = numCst(opdata[2]),
                    .rhs = local(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::MODNV:
            {
                TestAssert(opdata.size() == 3);
                bw.CreateMod({
                    .lhs = numCst(opdata[2]),
                    .rhs = local(opdata[1]),
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
                TestAssert(opdata.size() == 3);
                TestAssert(opdata[2] >= opdata[1]);
                uint16_t num = SafeIntegerCast<uint16_t>(opdata[2] - opdata[1] + 1);
                bw.CreateConcat({
                    .base = local(opdata[1]),
                    .num = num,
                    .output = local(opdata[0])
                });
                break;
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
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfLT({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                break;
            }
            case LJOpcode::ISGE:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfNLT({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                break;
            }
            case LJOpcode::ISLE:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfLE({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                break;
            }
            case LJOpcode::ISGT:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfNLE({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                break;
            }
            case LJOpcode::ISEQV:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfEq({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                break;
            }
            case LJOpcode::ISNEV:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfNotEq({
                    .lhs = local(opdata[0]),
                    .rhs = local(opdata[1])
                });
                break;
            }
            case LJOpcode::ISEQS:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfEq({
                    .lhs = local(opdata[0]),
                    .rhs = objCst(opdata[1])
                });
                break;
            }
            case LJOpcode::ISNES:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfNotEq({
                    .lhs = local(opdata[0]),
                    .rhs = objCst(opdata[1])
                });
                break;
            }
            case LJOpcode::ISEQN:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfEq({
                    .lhs = local(opdata[0]),
                    .rhs = numCst(opdata[1])
                });
                break;
            }
            case LJOpcode::ISNEN:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfNotEq({
                    .lhs = local(opdata[0]),
                    .rhs = numCst(opdata[1])
                });
                break;
            }
            case LJOpcode::ISEQP:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfEq({
                    .lhs = local(opdata[0]),
                    .rhs = priCst(opdata[1])
                });
                break;
            }
            case LJOpcode::ISNEP:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfNotEq({
                    .lhs = local(opdata[0]),
                    .rhs = priCst(opdata[1])
                });
                break;
            }
            case LJOpcode::ISTC:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateSelectAndBranchIfTruthy({
                    .testValue = local(opdata[1]),
                    .defaultValue = local(opdata[0]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::ISFC:
            {
                TestAssert(opdata.size() == 2);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateSelectAndBranchIfFalsy({
                    .testValue = local(opdata[1]),
                    .defaultValue = local(opdata[0]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::IST:
            {
                TestAssert(opdata.size() == 1);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfTruthy({
                    .testValue = local(opdata[0])
                });
                break;
            }
            case LJOpcode::ISF:
            {
                TestAssert(opdata.size() == 1);
                size_t brTarget = decodeAndSkipNextJumpBytecode();
                jumpPatches.push_back(std::make_pair(brTarget, bw.GetCurLength()));
                bw.CreateBranchIfFalsy({
                    .testValue = local(opdata[0])
                });
                break;
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
                TestAssert(opdata.size() == 2);
                bw.CreateLogicalNot({
                    .value = local(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
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
                TestAssert(opdata.size() == 2);
                bw.CreateLengthOf({
                    .input = local(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
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
                TValue tv = objCst(opdata[1]);
                TestAssert(tv.Is<tTable>());
                HeapPtr<TableObject> tab = tv.As<tTable>();
                bool usedSpecializedTableDup = false;
                if (TCGet(tab->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure)
                {
                    HeapPtr<Structure> structure = TCGet(tab->m_hiddenClass).As<Structure>();
                    if (structure->m_butterflyNamedStorageCapacity == 0 && !TCGet(tab->m_arrayType).HasSparseMap())
                    {
                        uint8_t inlineCapacity = structure->m_inlineNamedStorageCapacity;
                        uint8_t stepping = Structure::GetInitialStructureSteppingForInlineCapacity(inlineCapacity);
                        TestAssert(internal::x_inlineStorageSizeForSteppingArray[stepping] == inlineCapacity);
                        if (tab->m_butterfly == nullptr)
                        {
                            if (stepping <= TableObject::TableDupMaxInlineCapacitySteppingForNoButterflyCase())
                            {
                                usedSpecializedTableDup = true;
                                bw.CreateTableDup({
                                    .src = tv,
                                    .inlineCapacityStepping = stepping,
                                    .hasButterfly = 0,
                                    .output = local(opdata[0])
                                });
                            }
                        }
                        else
                        {
                            if (stepping <= TableObject::TableDupMaxInlineCapacitySteppingForHasButterflyCase())
                            {
                                usedSpecializedTableDup = true;
                                bw.CreateTableDup({
                                    .src = tv,
                                    .inlineCapacityStepping = stepping,
                                    .hasButterfly = 1,
                                    .output = local(opdata[0])
                                });
                            }
                        }
                    }
                }
                if (!usedSpecializedTableDup)
                {
                    bw.CreateTableDupGeneral({
                        .src = tv,
                        .output = local(opdata[0])
                    });
                }
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
            {
                TestAssert(opdata.size() == 2);
                // This opcode reads from slot A-1...
                //
                TestAssert(opdata[0] >= 1);
                int32_t localSlot = opdata[0] - 1;
                TValue tvIndex = numCst(opdata[1]);
                TestAssert(tvIndex.Is<tDouble>());
                // For some reason LuaJIT's format stores the real index as the low-32 bit of the double...
                //
                int32_t idx = BitwiseTruncateTo<int32_t>(tvIndex.m_value);
                bw.CreateTableVariadicPutBySeq({
                    .base = local(localSlot),
                    .index = Cst<tInt32>(idx)
                });
                break;
            }
            case LJOpcode::UGET:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateUpvalueGet({
                    .ord = SafeIntegerCast<uint16_t>(opdata[1]),
                    .output = local(opdata[0])
                });
                break;
            }
            case LJOpcode::USETV:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateUpvaluePut({
                    .ord = SafeIntegerCast<uint16_t>(opdata[0]),
                    .value = local(opdata[1])
                });
                break;
            }
            case LJOpcode::USETS:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateUpvaluePut({
                    .ord = SafeIntegerCast<uint16_t>(opdata[0]),
                    .value = objCst(opdata[1])
                });
                break;
            }
            case LJOpcode::USETN:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateUpvaluePut({
                    .ord = SafeIntegerCast<uint16_t>(opdata[0]),
                    .value = numCst(opdata[1])
                });
                break;
            }
            case LJOpcode::USETP:
            {
                TestAssert(opdata.size() == 2);
                bw.CreateUpvaluePut({
                    .ord = SafeIntegerCast<uint16_t>(opdata[0]),
                    .value = priCst(opdata[1])
                });
                break;
            }
            case LJOpcode::UCLO:
            {
                TestAssert(opdata.size() == 2);
                size_t jumpTarget = getBytecodeOrdinalOfJump(opdata[1]);
                jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
                bw.CreateUpvalueClose({
                    .base = local(opdata[0])
                });
                break;
            }
            case LJOpcode::FORI:
            {
                // Loop init
                // semantics:
                // [A] = tonumber([A]), [A+1] = tonumber([A+1]), [A+2] = tonumber([A+2])
                // if (!([A+2] > 0 && [A] <= [A+1]) || ([A+2] <= 0 && [A] >= [A+1])): jump
                // [A+3] = [A]
                //
                TestAssert(opdata.size() == 2);
                size_t jumpTarget = getBytecodeOrdinalOfJump(opdata[1]);
                jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
                bw.CreateForLoopInit({
                    .base = local(opdata[0])
                });
                break;
            }
            case LJOpcode::FORL:
            {
                // Loop step
                // semantics:
                // [A] += [A+2]
                // if ([A+2] > 0 && [A] <= [A+1]) || ([A+2] <= 0 && [A] >= [A+1]): jump
                //
                TestAssert(opdata.size() == 2);
                size_t jumpTarget = getBytecodeOrdinalOfJump(opdata[1]);
                jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
                bw.CreateForLoopStep({
                    .base = local(opdata[0])
                });
                break;
            }
            case LJOpcode::LOOP:
            {
                // LOOP is a no-op used for LuaJIT's internal profiling
                // For now make it a no-op for us as well
                //
                break;
            }
            case LJOpcode::JMP:
            {
                TestAssert(opdata.size() == 2);
                size_t jumpTarget = getBytecodeOrdinalOfJump(opdata[1]);
                jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
                // opdata[0] is unused for now (indicates stack frame size after jump)
                bw.CreateBranch();
                break;
            }
            case LJOpcode::VARG:
            {
                // should have 3 opdata, despite field C is ignored by us
                //
                TestAssert(opdata.size() == 3);
                int32_t fieldB = opdata[1];
                if (fieldB == 0)
                {
                    // Put vararg as variadic returns
                    //
                    bw.CreateStoreVarArgsAsVariadicResults();
                }
                else
                {
                    bw.CreateGetVarArgsPrefix({
                        .base = local(opdata[0]),
                        .numToPut = SafeIntegerCast<uint16_t>(fieldB - 1)
                    });
                }
                break;
            }
            case LJOpcode::KNIL:
            {
                TestAssert(opdata.size() == 2);
                TestAssert(opdata[1] >= opdata[0]);
                uint32_t numSlotsToFill = static_cast<uint32_t>(opdata[1] - opdata[0] + 1);
                bw.CreateRangeFillNils({
                    .base = local(opdata[0]),
                    .numToPut = SafeIntegerCast<uint16_t>(numSlotsToFill)
                });
                break;
            }
            case LJOpcode::ITERN:
            {
                // semantics:
                // [A], ... [A+B-2] = [A-3]([A-2], [A-1])
                //
                TestAssert(opdata.size() == 3);
                TestAssert(opdata[2] == 3);
                TestAssert(opdata[1] >= 2);
                TestAssert(opdata[0] >= 3);
                uint8_t numRets = SafeIntegerCast<uint8_t>(opdata[1] - 1);
                TestAssert(1 <= numRets && numRets <= 2);
                size_t jumpTarget = decodeAndSkipNextITERLBytecode();
                jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
                bw.CreateKVLoopIter({
                    .base = local(opdata[0] - 3),
                    .numRets = numRets
                });
                break;
            }
            case LJOpcode::ITERC:
            {
                // semantics:
                // [A], ... [A+B-2] = [A-3]([A-2], [A-1])
                //
                TestAssert(opdata.size() == 3);
                TestAssert(opdata[2] == 3);
                TestAssert(opdata[1] >= 2);
                TestAssert(opdata[0] >= 3);
                uint16_t numRets = SafeIntegerCast<uint16_t>(opdata[1] - 1);
                size_t jumpTarget = decodeAndSkipNextITERLBytecode();
                jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
                bw.CreateForLoopIter({
                    .base = local(opdata[0] - 3),
                    .numRets = numRets
                });
                break;
            }
            case LJOpcode::ITERL:
            {
                ReleaseAssert(false && "should never hit here since ITERL should always be after a ITERN/ITERC and skipped when we process the ITERN/ITERC");
            }
            case LJOpcode::ISNEXT:
            {
                TestAssert(opdata.size() == 2);
                TestAssert(opdata[0] >= 3);
                size_t jumpTarget = getBytecodeOrdinalOfJump(opdata[1]);
                jumpPatches.push_back(std::make_pair(jumpTarget, bw.GetCurLength()));
                bw.CreateValidateIsNextAndBranch({
                    .base = local(opdata[0] - 3)
                });
                break;
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
            size_t bcPos = jumpPatch.second;
            assert(ljBytecodeOrd < bytecodeLocation.size());
            size_t bytecodeOffset = bytecodeLocation[ljBytecodeOrd];
            assert(bytecodeOffset < bw.GetCurLength());
            if (unlikely(!bw.SetBranchTarget(bcPos, bytecodeOffset)))
            {
                // TODO: gracefully handle
                fprintf(stderr, "[LOCKDOWN] Branch bytecode exceeded maximum branch offset limit. Maybe make your function smaller?\n");
                abort();
            }
        }

        std::pair<uint8_t*, size_t> bytecodeData = bw.GetBuiltBytecodeSequence();
        std::pair<uint64_t*, size_t> constantTableData = bw.GetBuiltConstantTable();

        ucb->m_cstTableLength = static_cast<uint32_t>(constantTableData.second);
        ucb->m_cstTable = constantTableData.first;

        ucb->m_bytecode = bytecodeData.first;
        ucb->m_bytecodeLength = static_cast<uint32_t>(bytecodeData.second);
        ucb->m_bytecodeMetadataLength = bw.GetBytecodeMetadataTotalLength();
        const auto& bmUseCounts = bw.GetBytecodeMetadataUseCountArray();
        assert(bmUseCounts.size() == x_num_bytecode_metadata_struct_kinds_);
        memcpy(ucb->m_bytecodeMetadataUseCounts, bmUseCounts.data(), bmUseCounts.size() * sizeof(uint16_t));

        // CodeBlock::Create must be called after populated everything (including the metadata counts) in ucb
        //
        ucb->m_defaultCodeBlock = CodeBlock::Create(vm, ucb, globalObject);

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

    TestAssert(chunkFn->m_numFixedArguments == 0);
    TestAssert(chunkFn->m_numUpvalues == 0);
    UserHeapPointer<FunctionObject> entryPointFunc = FunctionObject::Create(vm, chunkFn->GetCodeBlock(globalObject));
    r->m_defaultEntryPoint = entryPointFunc;

    return r;
}

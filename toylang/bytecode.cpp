#include "bytecode.h"
#include "vm.h"
#include "table_object.h"

#include "json_utils.h"

namespace ToyLang
{

using namespace CommonUtils;

const InterpreterFn x_interpreter_dispatches[x_numOpcodes] = {
#define macro(opcodeCppName) &opcodeCppName::Execute,
    PP_FOR_EACH(macro, OPCODE_LIST)
#undef macro
};

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

LJOpcode WARN_UNUSED GetOpcodeFromString(const std::string& s)
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

struct BytecodeWriter
{
    BytecodeWriter()
    {
        constexpr size_t initialSize = 128;
        m_bytecodeStart = new uint8_t[initialSize];
        m_bytecodeCur = m_bytecodeStart;
        m_bytecodeEnd = m_bytecodeStart + initialSize;
    }

    ~BytecodeWriter()
    {
        delete [] m_bytecodeStart;
    }

    template<typename T>
    void Append(T value)
    {
        static_assert(alignof(T) == 1);
        Reserve(sizeof(T));
        *reinterpret_cast<T*>(m_bytecodeCur) = value;
        m_bytecodeCur += sizeof(T);
        assert(m_bytecodeCur <= m_bytecodeEnd);
    }

    int32_t CurrentBytecodeOffset()
    {
        return static_cast<int32_t>(m_bytecodeCur - m_bytecodeStart);
    }

    std::pair<uint8_t*, size_t> Get()
    {
        size_t length = static_cast<size_t>(m_bytecodeCur - m_bytecodeStart);
        uint8_t* r = new uint8_t[length];
        memcpy(r, m_bytecodeStart, length);
        return std::make_pair(r, length);
    }

    void Reserve(size_t size)
    {
        if (likely(m_bytecodeEnd - m_bytecodeCur >= static_cast<ssize_t>(size)))
        {
            return;
        }
        size_t cap = static_cast<size_t>(m_bytecodeEnd - m_bytecodeStart);
        size_t current = static_cast<size_t>(m_bytecodeCur - m_bytecodeStart);
        size_t needed = current + size;
        while (cap < needed) { cap *= 2; }
        uint8_t* newArray = new uint8_t[cap];
        memcpy(newArray, m_bytecodeStart, current);
        delete [] m_bytecodeStart;
        m_bytecodeStart = newArray;
        m_bytecodeCur = newArray + current;
        m_bytecodeEnd = newArray + cap;
    }

    uint8_t* m_bytecodeStart;
    uint8_t* m_bytecodeCur;
    uint8_t* m_bytecodeEnd;
};

struct BytecodeJumpPatch
{
    void Patch(uint8_t* bytecode, const std::vector<int32_t>& bytecodeOffsetList)
    {
        TestAssert(m_targetOrdinal >= 0 && static_cast<size_t>(m_targetOrdinal) < bytecodeOffsetList.size());
        int32_t targetBytecodeOffset = bytecodeOffsetList[static_cast<size_t>(m_targetOrdinal)];
        TestAssert(targetBytecodeOffset != -1);
        int32_t diff = targetBytecodeOffset - m_selfOffset;
        UnalignedStore<int32_t>(bytecode + m_loc, diff);
    }

    int32_t m_loc;             // location to write
    int32_t m_selfOffset;       // The offset of the jumping bytecode
    int32_t m_targetOrdinal;    // The ordinal of the target bytecode
};

constexpr bool x_json_parser_force_use_double = true;

ScriptModule* WARN_UNUSED ScriptModule::ParseFromJSON(VM* vm, UserHeapPointer<TableObject> globalObject, const std::string& content)
{
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
                ucb->m_cstTable[i].m_tv = TValue::CreateMIV(MiscImmediateValue::CreateFalse(), TValue::x_mivTag);
                i--;
                ucb->m_cstTable[i].m_tv = TValue::CreateMIV(MiscImmediateValue::CreateTrue(), TValue::x_mivTag);
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
                        tv = TValue::CreateInt32(value, TValue::x_int32Tag);
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
                            return TValue::CreateMIV(MiscImmediateValue::CreateBoolean(value), TValue::x_mivTag);
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
                                tv = TValue::CreateInt32(value, TValue::x_int32Tag);
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
                            UserHeapPointer<void> key = vm->GetSpecialKeyForBoolean(static_cast<bool>(k));
                            TValue val = booleanValues[k];

                            PutByIdICInfo icInfo;
                            TableObject::PreparePutById(obj, key, icInfo /*out*/);
                            TableObject::PutById(obj, key, val, icInfo);

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
                        TableObject::PutByValIntegerIndex(obj, idx, val);
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
                                TableObject::PutByValIntegerIndex(obj, key, val);
                                numericIndexInserted++;
                            }
                        }
                        else if (kty == "Double")
                        {
                            double key = JSONCheckedGet<double>(tabEntryKey, "Value");
                            TValue val = convertTableValue(tabEntryValue);
                            TableObject::PutByValDoubleIndex(obj, key, val);
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

        auto bytecodeSlotFromPrimaryConstant = [&](int32_t primaryConstant) -> BytecodeSlot
        {
            TestAssert(0 <= primaryConstant && primaryConstant < 3);
            return BytecodeSlot::Constant(-(primaryConstant + 1));
        };

        auto bytecodeSlotFromNumberConstant = [&](int32_t ord) -> BytecodeSlot
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(numNumberConstants) - 3);
            return BytecodeSlot::Constant(-(ord + 1 + 3));
        };

        auto bytecodeSlotFromObjectConstant = [&](int32_t ord) -> BytecodeSlot
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(numObjectConstants));
            return BytecodeSlot::Constant(-(static_cast<int>(numNumberConstants) + ord + 1));
        };

        auto bytecodeSlotFromVariableSlot = [&](int32_t ord) -> BytecodeSlot
        {
            TestAssert(0 <= ord && ord < static_cast<int32_t>(ucb->m_stackFrameNumSlots));
            return BytecodeSlot::Local(ord);
        };

        std::vector<int32_t> bytecodeLocation;
        std::vector<BytecodeJumpPatch> jumpPatches;
        BytecodeWriter bw;
        TestAssert(j.count("Bytecode") && j["Bytecode"].is_array());
        auto& bytecodeList = j["Bytecode"];
        for (auto it = bytecodeList.begin(); it != bytecodeList.end(); it++)
        {
            bytecodeLocation.push_back(bw.CurrentBytecodeOffset());
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
            case LJOpcode::ADDVN: [[fallthrough]];
            case LJOpcode::SUBVN: [[fallthrough]];
            case LJOpcode::MULVN: [[fallthrough]];
            case LJOpcode::DIVVN: [[fallthrough]];
            case LJOpcode::MODVN: [[fallthrough]];
            case LJOpcode::ADDNV: [[fallthrough]];
            case LJOpcode::SUBNV: [[fallthrough]];
            case LJOpcode::MULNV: [[fallthrough]];
            case LJOpcode::DIVNV: [[fallthrough]];
            case LJOpcode::MODNV: [[fallthrough]];
            case LJOpcode::ADDVV: [[fallthrough]];
            case LJOpcode::SUBVV: [[fallthrough]];
            case LJOpcode::MULVV: [[fallthrough]];
            case LJOpcode::DIVVV: [[fallthrough]];
            case LJOpcode::MODVV:
            {
                TestAssert(opdata.size() == 3);
                uint32_t kind = static_cast<uint32_t>(opcode) - static_cast<uint32_t>(LJOpcode::ADDVN);
                TestAssert(kind < 15);
                uint32_t opKind = kind % 5;
                BytecodeSlot lhs;
                BytecodeSlot rhs;
                if (kind < 5)
                {
                    // VN case: A = B op C, type(B)=V, type(C)=N
                    //
                    lhs = bytecodeSlotFromVariableSlot(opdata[1]);
                    rhs = bytecodeSlotFromNumberConstant(opdata[2]);
                }
                else if (kind < 10)
                {
                    // NV case: A = C op B, type(B)=V, type(C)=N
                    //
                    lhs = bytecodeSlotFromNumberConstant(opdata[2]);
                    rhs = bytecodeSlotFromVariableSlot(opdata[1]);
                }
                else
                {
                    // VV case: A = B op C, type(B)=V, type(C)=V
                    //
                    lhs = bytecodeSlotFromVariableSlot(opdata[1]);
                    rhs = bytecodeSlotFromVariableSlot(opdata[2]);
                }

                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                switch (opKind)
                {
                case 0:
                {
                    bw.Append(BcAdd(lhs, rhs, dst));
                    break;
                }
                case 1:
                {
                    bw.Append(BcSub(lhs, rhs, dst));
                    break;
                }
                case 2:
                {
                    bw.Append(BcMul(lhs, rhs, dst));
                    break;
                }
                case 3:
                {
                    bw.Append(BcDiv(lhs, rhs, dst));
                    break;
                }
                case 4:
                {
                    bw.Append(BcMod(lhs, rhs, dst));
                    break;
                }
                default:
                {
                    ReleaseAssert(false);
                }
                }
                break;
            }
            case LJOpcode::KSHORT:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                int32_t data = opdata[1];
                TValue tv;
                if (x_json_parser_force_use_double)
                {
                    tv = TValue::CreateDouble(data);
                }
                else
                {
                    tv = TValue::CreateInt32(data, TValue::x_int32Tag);
                }
                bw.Append(BcConstant(dst, tv));
                break;
            }
            case LJOpcode::ISLT: [[fallthrough]];
            case LJOpcode::ISGE: [[fallthrough]];
            case LJOpcode::ISLE: [[fallthrough]];
            case LJOpcode::ISGT: [[fallthrough]];
            case LJOpcode::ISEQV: [[fallthrough]];
            case LJOpcode::ISNEV: [[fallthrough]];
            case LJOpcode::ISEQS: [[fallthrough]];
            case LJOpcode::ISNES: [[fallthrough]];
            case LJOpcode::ISEQN: [[fallthrough]];
            case LJOpcode::ISNEN: [[fallthrough]];
            case LJOpcode::ISEQP: [[fallthrough]];
            case LJOpcode::ISNEP:
            {
                uint32_t opOrd = static_cast<uint32_t>(opcode) - static_cast<uint32_t>(LJOpcode::ISLT);
                TestAssert(opOrd < 12);
                uint32_t opKind;
                uint32_t rhsKind;
                if (opOrd < 4)
                {
                    opKind = opOrd;
                    rhsKind = 0;
                }
                else
                {
                    opKind = 4 + opOrd % 2;
                    rhsKind = (opOrd - 4) / 2;
                }

                TestAssert(opdata.size() == 2);

                int32_t selfBytecodeOrdinal = static_cast<int32_t>(it - bytecodeList.begin());
                int32_t selfOffset = bw.CurrentBytecodeOffset();
                BytecodeSlot lhs = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot rhs;
                if (rhsKind == 0)
                {
                    rhs = bytecodeSlotFromVariableSlot(opdata[1]);
                }
                else if (rhsKind == 1)
                {
                    rhs = bytecodeSlotFromObjectConstant(opdata[1]);
                }
                else if (rhsKind == 2)
                {
                    rhs = bytecodeSlotFromNumberConstant(opdata[1]);
                }
                else
                {
                    TestAssert(rhsKind == 3);
                    rhs = bytecodeSlotFromPrimaryConstant(opdata[1]);
                }

                it++;
                TestAssert(it < bytecodeList.end());
                auto& nextBytecode = *it;
                TestAssert(JSONCheckedGet<std::string>(nextBytecode, "OpCode") == "JMP");
                TestAssert(nextBytecode.count("OpData") && nextBytecode["OpData"].is_array() && nextBytecode["OpData"].size() == 2);

                // The 'JMP' bytecode immediately following the comparsion should never be a valid jump target
                //
                bytecodeLocation.push_back(-1);

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

                switch (opKind)
                {
                case 0:
                {
                    bw.Append(BcIsLT(lhs, rhs));
                    jumpPatches.push_back(BytecodeJumpPatch {
                                              .m_loc = selfOffset + BcIsLT::OffsetOfJump(),
                                              .m_selfOffset = selfOffset,
                                              .m_targetOrdinal = jumpBytecodeOrdinal
                                          });
                    break;
                }
                case 1:
                {
                    bw.Append(BcIsNLT(lhs, rhs));
                    jumpPatches.push_back(BytecodeJumpPatch {
                                              .m_loc = selfOffset + BcIsNLT::OffsetOfJump(),
                                              .m_selfOffset = selfOffset,
                                              .m_targetOrdinal = jumpBytecodeOrdinal
                                          });
                    break;
                }
                case 2:
                {
                    bw.Append(BcIsLE(lhs, rhs));
                    jumpPatches.push_back(BytecodeJumpPatch {
                                              .m_loc = selfOffset + BcIsLE::OffsetOfJump(),
                                              .m_selfOffset = selfOffset,
                                              .m_targetOrdinal = jumpBytecodeOrdinal
                                          });
                    break;
                }
                case 3:
                {
                    bw.Append(BcIsNLE(lhs, rhs));
                    jumpPatches.push_back(BytecodeJumpPatch {
                                              .m_loc = selfOffset + BcIsNLE::OffsetOfJump(),
                                              .m_selfOffset = selfOffset,
                                              .m_targetOrdinal = jumpBytecodeOrdinal
                                          });
                    break;
                }
                case 4:
                {
                    bw.Append(BcIsEQ(lhs, rhs));
                    jumpPatches.push_back(BytecodeJumpPatch {
                                              .m_loc = selfOffset + BcIsEQ::OffsetOfJump(),
                                              .m_selfOffset = selfOffset,
                                              .m_targetOrdinal = jumpBytecodeOrdinal
                                          });
                    break;
                }
                case 5:
                {
                    bw.Append(BcIsNEQ(lhs, rhs));
                    jumpPatches.push_back(BytecodeJumpPatch {
                                              .m_loc = selfOffset + BcIsNEQ::OffsetOfJump(),
                                              .m_selfOffset = selfOffset,
                                              .m_targetOrdinal = jumpBytecodeOrdinal
                                          });
                    break;
                }
                default:
                {
                    ReleaseAssert(false);
                }
                }
                break;
            }
            case LJOpcode::GGET:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot idx = bytecodeSlotFromObjectConstant(opdata[1]);
                bw.Append(BcGlobalGet(dst, idx));
                break;
            }
            case LJOpcode::GSET:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot src = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot idx = bytecodeSlotFromObjectConstant(opdata[1]);
                bw.Append(BcGlobalPut(src, idx));
                break;
            }
            case LJOpcode::RETM: [[fallthrough]];
            case LJOpcode::RET: [[fallthrough]];
            case LJOpcode::RET0: [[fallthrough]];
            case LJOpcode::RET1:
            {
                TestAssert(opdata.size() == 2);
                bool isVariadic = (opcode == LJOpcode::RETM);

                uint16_t numReturnValues;
                if (opcode == LJOpcode::RETM)
                {
                    // For RETM, D holds # of fixed return values
                    //
                    numReturnValues = SafeIntegerCast<uint16_t>(opdata[1]);
                }
                else
                {
                    // For RET0, RET1 and RET, D holds 1 + # ret values
                    //
                    TestAssert(opdata[1] >= 1);
                    numReturnValues = SafeIntegerCast<uint16_t>(opdata[1] - 1);
                }
                TestAssertImp(opcode == LJOpcode::RET0, numReturnValues == 0);
                TestAssertImp(opcode == LJOpcode::RET1, numReturnValues == 1);

                BytecodeSlot slotBegin = bytecodeSlotFromVariableSlot(opdata[0]);
                bw.Append(BcReturn(isVariadic, numReturnValues, slotBegin));
                break;
            }
            case LJOpcode::CALLM: [[fallthrough]];
            case LJOpcode::CALL:
            {
                TestAssert(opdata.size() == 3);
                bool passVarRetAsParams = (opcode == LJOpcode::CALLM);
                BytecodeSlot base = bytecodeSlotFromVariableSlot(opdata[0]);

                bool keepVariadicResult;
                uint32_t numFixedResults;
                int32_t fieldB = opdata[1];
                if (fieldB == 0)
                {
                    keepVariadicResult = true;
                    numFixedResults = 0;
                }
                else
                {
                    keepVariadicResult = false;
                    numFixedResults = SafeIntegerCast<uint32_t>(fieldB - 1);
                }

                uint32_t numFixedParams;
                int32_t fieldC = opdata[2];
                if (opcode == LJOpcode::CALLM)
                {
                    // For CALLM, C holds # of fixed params
                    //
                    numFixedParams = SafeIntegerCast<uint32_t>(fieldC);
                }
                else
                {
                    // For CALL, C holds 1 + # of fixed params
                    //
                    TestAssert(opcode == LJOpcode::CALL);
                    numFixedParams = SafeIntegerCast<uint32_t>(fieldC - 1);
                }

                bw.Append(BcCall(keepVariadicResult, passVarRetAsParams, numFixedParams, numFixedResults, base));
                break;
            }
            case LJOpcode::MOV:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot src = bytecodeSlotFromVariableSlot(opdata[1]);
                bw.Append(BcMove(src, dst));
                break;
            }
            case LJOpcode::KSTR:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot src = bytecodeSlotFromObjectConstant(opdata[1]);
                bw.Append(BcMove(src, dst));
                break;
            }
            case LJOpcode::KNUM:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot src = bytecodeSlotFromNumberConstant(opdata[1]);
                bw.Append(BcMove(src, dst));
                break;
            }
            case LJOpcode::KPRI:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                int32_t src = opdata[1];
                TValue val;
                TestAssert(src == 0 || src == 1 || src == 2);
                if (src == 0)
                {
                    val = TValue::Nil();
                }
                else if (src == 1)
                {
                    val = TValue::CreateMIV(MiscImmediateValue::CreateFalse(), TValue::x_mivTag);
                }
                else
                {
                    TestAssert(src == 2);
                    val = TValue::CreateMIV(MiscImmediateValue::CreateTrue(), TValue::x_mivTag);
                }
                bw.Append(BcConstant(dst, val));
                break;
            }
            case LJOpcode::FNEW:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot src = bytecodeSlotFromObjectConstant(opdata[1]);
                bw.Append(BcNewClosure(src, dst));
                break;
            }
            case LJOpcode::TNEW:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
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

                bw.Append(BcTableNew(dst, stepping, static_cast<uint16_t>(arrayPartHint)));
                break;
            }
            case LJOpcode::TDUP:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot src = bytecodeSlotFromObjectConstant(opdata[1]);
                bw.Append(BcTableDup(dst, src.ConstantOrd()));
                break;
            }
            case LJOpcode::TGETV: [[fallthrough]];
            case LJOpcode::TGETS: [[fallthrough]];
            case LJOpcode::TSETV: [[fallthrough]];
            case LJOpcode::TSETS:
            {
                TestAssert(opdata.size() == 3);
                BytecodeSlot srcOrDst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot base = bytecodeSlotFromVariableSlot(opdata[1]);
                if (opcode == LJOpcode::TGETV)
                {
                    BytecodeSlot index = bytecodeSlotFromVariableSlot(opdata[2]);
                    bw.Append(BcTableGetByVal(base, srcOrDst, index));
                }
                else if (opcode == LJOpcode::TGETS)
                {
                    BytecodeSlot index = bytecodeSlotFromObjectConstant(opdata[2]);
                    bw.Append(BcTableGetById(base, srcOrDst, index.ConstantOrd()));
                }
                else if (opcode == LJOpcode::TSETV)
                {
                    BytecodeSlot index = bytecodeSlotFromVariableSlot(opdata[2]);
                    bw.Append(BcTablePutByVal(base, srcOrDst, index));
                }
                else
                {
                    TestAssert(opcode == LJOpcode::TSETS);
                    BytecodeSlot index = bytecodeSlotFromObjectConstant(opdata[2]);
                    bw.Append(BcTablePutById(base, srcOrDst, index.ConstantOrd()));
                }
                break;
            }
            case LJOpcode::TGETB: [[fallthrough]];
            case LJOpcode::TSETB:
            {
                TestAssert(opdata.size() == 3);
                BytecodeSlot srcOrDst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot base = bytecodeSlotFromVariableSlot(opdata[1]);
                int16_t index = SafeIntegerCast<int16_t>(opdata[2]);
                if (opcode == LJOpcode::TGETB)
                {
                    bw.Append(BcTableGetByIntegerVal(base, srcOrDst, index));
                }
                else
                {
                    TestAssert(opcode == LJOpcode::TSETB);
                    bw.Append(BcTablePutByIntegerVal(base, srcOrDst, index));
                }
                break;
            }
            case LJOpcode::TSETM:
            {
                TestAssert(opdata.size() == 2);
                // This opcode reads from slot A-1...
                //
                TestAssert(opdata[0] >= 1);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                BytecodeSlot index = bytecodeSlotFromNumberConstant(opdata[1]);
                bw.Append(BcTableVariadicPutByIntegerValSeq(dst, index));
                break;
            }
            case LJOpcode::UGET:
            {
                TestAssert(opdata.size() == 2);
                BytecodeSlot dst = bytecodeSlotFromVariableSlot(opdata[0]);
                uint16_t index = SafeIntegerCast<uint16_t>(opdata[1]);
                bw.Append(BcUpvalueGet(dst, index));
                break;
            }
            case LJOpcode::USETV: [[fallthrough]];
            case LJOpcode::USETS: [[fallthrough]];
            case LJOpcode::USETN: [[fallthrough]];
            case LJOpcode::USETP:
            {
                TestAssert(opdata.size() == 2);
                uint16_t index = SafeIntegerCast<uint16_t>(opdata[0]);
                BytecodeSlot src;
                if (opcode == LJOpcode::USETV)
                {
                    src = bytecodeSlotFromVariableSlot(opdata[1]);
                }
                else if (opcode == LJOpcode::USETS)
                {
                    src = bytecodeSlotFromObjectConstant(opdata[1]);
                }
                else if (opcode == LJOpcode::USETN)
                {
                    src = bytecodeSlotFromNumberConstant(opdata[1]);
                }
                else
                {
                    TestAssert(opcode == LJOpcode::USETP);
                    src = bytecodeSlotFromPrimaryConstant(opdata[1]);
                }
                bw.Append(BcUpvaluePut(src, index));
                break;
            }
            case LJOpcode::UCLO:
            {
                TestAssert(opdata.size() == 2);
                int32_t selfBytecodeOrdinal = static_cast<int32_t>(it - bytecodeList.begin());
                int32_t selfOffset = bw.CurrentBytecodeOffset();
                int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + opdata[1];
                jumpPatches.push_back(BytecodeJumpPatch {
                                          .m_loc = selfOffset + BcUpvalueClose::OffsetOfJump(),
                                          .m_selfOffset = selfOffset,
                                          .m_targetOrdinal = jumpBytecodeOrdinal
                                      });
                BytecodeSlot base = bytecodeSlotFromVariableSlot(opdata[0]);
                bw.Append(BcUpvalueClose(base));
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
                int32_t selfBytecodeOrdinal = static_cast<int32_t>(it - bytecodeList.begin());
                int32_t selfOffset = bw.CurrentBytecodeOffset();
                int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + opdata[1];
                jumpPatches.push_back(BytecodeJumpPatch {
                                          .m_loc = selfOffset + BcForLoopInit::OffsetOfJump(),
                                          .m_selfOffset = selfOffset,
                                          .m_targetOrdinal = jumpBytecodeOrdinal
                                      });
                BytecodeSlot base = bytecodeSlotFromVariableSlot(opdata[0]);
                bw.Append(BcForLoopInit(base));
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
                int32_t selfBytecodeOrdinal = static_cast<int32_t>(it - bytecodeList.begin());
                int32_t selfOffset = bw.CurrentBytecodeOffset();
                int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + opdata[1];
                jumpPatches.push_back(BytecodeJumpPatch {
                                          .m_loc = selfOffset + BcForLoopStep::OffsetOfJump(),
                                          .m_selfOffset = selfOffset,
                                          .m_targetOrdinal = jumpBytecodeOrdinal
                                      });
                BytecodeSlot base = bytecodeSlotFromVariableSlot(opdata[0]);
                bw.Append(BcForLoopStep(base));
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
                int32_t selfBytecodeOrdinal = static_cast<int32_t>(it - bytecodeList.begin());
                int32_t selfOffset = bw.CurrentBytecodeOffset();
                // opdata[0] is unused for now (indicates stack frame size after jump)
                int32_t jumpBytecodeOrdinal = selfBytecodeOrdinal + opdata[1];
                jumpPatches.push_back(BytecodeJumpPatch {
                                          .m_loc = selfOffset + BcUnconditionalJump::OffsetOfJump(),
                                          .m_selfOffset = selfOffset,
                                          .m_targetOrdinal = jumpBytecodeOrdinal
                                      });
                bw.Append(BcUnconditionalJump());
                break;
            }
            case LJOpcode::POW: [[fallthrough]];
            case LJOpcode::CAT: [[fallthrough]];
            case LJOpcode::NOT: [[fallthrough]];
            case LJOpcode::UNM: [[fallthrough]];
            case LJOpcode::LEN: [[fallthrough]];
            case LJOpcode::ISTC: [[fallthrough]];
            case LJOpcode::ISFC: [[fallthrough]];
            case LJOpcode::IST: [[fallthrough]];
            case LJOpcode::ISF: [[fallthrough]];
            case LJOpcode::KCDATA: [[fallthrough]];
            case LJOpcode::KNIL: [[fallthrough]];
            case LJOpcode::CALLMT: [[fallthrough]];
            case LJOpcode::CALLT: [[fallthrough]];
            case LJOpcode::ITERC: [[fallthrough]];
            case LJOpcode::ITERN: [[fallthrough]];
            case LJOpcode::VARG: [[fallthrough]];
            case LJOpcode::ISNEXT: [[fallthrough]];
            case LJOpcode::ITERL:
            {
                ReleaseAssert(false && "unimplemented");
            }
            case LJOpcode::ISTYPE: [[fallthrough]];
            case LJOpcode::ISNUM: [[fallthrough]];
            case LJOpcode::TGETR: [[fallthrough]];
            case LJOpcode::TSETR: [[fallthrough]];
            case LJOpcode::FUNCF: [[fallthrough]];
            case LJOpcode::IFUNCF: [[fallthrough]];
            case LJOpcode::JFUNCF: [[fallthrough]];
            case LJOpcode::FUNCV: [[fallthrough]];
            case LJOpcode::IFUNCV: [[fallthrough]];
            case LJOpcode::JFUNCV: [[fallthrough]];
            case LJOpcode::FUNCC: [[fallthrough]];
            case LJOpcode::FUNCCW: [[fallthrough]];
            case LJOpcode::JFORI: [[fallthrough]];
            case LJOpcode::IFORL: [[fallthrough]];
            case LJOpcode::JFORL: [[fallthrough]];
            case LJOpcode::IITERL: [[fallthrough]];
            case LJOpcode::JITERL: [[fallthrough]];
            case LJOpcode::ILOOP: [[fallthrough]];
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
            jumpPatch.Patch(bw.m_bytecodeStart, bytecodeLocation);
        }

        std::pair<uint8_t*, size_t> bytecodeData = bw.Get();
        ucb->m_bytecode = bytecodeData.first;
        ucb->m_bytecodeLength = static_cast<uint32_t>(bytecodeData.second);
        ucb->m_bytecodeMetadataLength = 0;
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

    TestAssert(chunkFn->m_numUpvalues == 0);
    UserHeapPointer<FunctionObject> entryPointFunc = FunctionObject::Create(vm, UnlinkedCodeBlock::GetCodeBlock(chunkFn, globalObject));
    r->m_defaultEntryPoint = entryPointFunc;

    return r;
}

}   // namespace ToyLang

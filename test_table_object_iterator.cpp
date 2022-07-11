#include "bytecode.h"
#include "gtest/gtest.h"
#include "test_vm_utils.h"

using namespace ToyLang;

namespace {

TEST(TableObjectIterator, Sanity)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    StringList strings = GetStringList(VM::GetActiveVMForCurrentThread(), 1000 /*numStrings*/);

    for (bool putNamedProps : { false, true })
    {
        for (bool putArrayProps : { false, true })
        {
            if (!putNamedProps && !putArrayProps)
            {
                continue;
            }

            for (uint32_t testcase = 0; testcase < 3; testcase++)
            {
                uint32_t inlineCapacity = testcase * 8;
                Structure* structure = Structure::CreateInitialStructure(vm, static_cast<uint8_t>(inlineCapacity));
                HeapPtr<TableObject> obj = TableObject::CreateEmptyTableObject(vm, structure, 0 /*initButterflyCap*/);
                std::unordered_map<double, TValue> expectedArrayPart;
                std::unordered_map<int64_t, TValue> expectedNamedProps;

                auto validate = [&]()
                {
                    std::unordered_set<int64_t> showedUpNamedProps;
                    std::unordered_set<double> showedUpArrayProps;
                    TableObjectIterator iter;
                    while (true)
                    {
                        TableObjectIterator::KeyValuePair kv = iter.Advance(obj);
                        if (kv.m_key.IsNil())
                        {
                            ReleaseAssert(kv.m_value.IsNil());
                            break;
                        }
                        if (kv.m_key.IsDouble(TValue::x_int32Tag))
                        {
                            double key = kv.m_key.AsDouble();
                            ReleaseAssert(!showedUpArrayProps.count(key));
                            showedUpArrayProps.insert(key);
                            ReleaseAssert(expectedArrayPart.count(key));
                            ReleaseAssert(expectedArrayPart[key].m_value == kv.m_value.m_value);
                        }
                        else if (kv.m_key.IsInt32(TValue::x_int32Tag))
                        {
                            double key = kv.m_key.AsInt32();
                            ReleaseAssert(!showedUpArrayProps.count(key));
                            showedUpArrayProps.insert(key);
                            ReleaseAssert(expectedArrayPart.count(key));
                            ReleaseAssert(expectedArrayPart[key].m_value == kv.m_value.m_value);
                        }
                        else
                        {
                            ReleaseAssert(kv.m_key.IsPointer(TValue::x_mivTag));
                            int64_t key = kv.m_key.AsPointer().m_value;
                            ReleaseAssert(!showedUpNamedProps.count(key));
                            showedUpNamedProps.insert(key);
                            ReleaseAssert(expectedNamedProps.count(key));
                            ReleaseAssert(expectedNamedProps[key].m_value == kv.m_value.m_value);
                        }
                    }
                    ReleaseAssert(showedUpNamedProps.size() == expectedNamedProps.size());
                    ReleaseAssert(showedUpArrayProps.size() == expectedArrayPart.size());
                };

                for (uint32_t ops = 0; ops < 800; ops++)
                {
                    validate();

                    int dice = rand() % 2;
                    if (!putNamedProps) { dice = 1; }
                    if (!putArrayProps) { dice = 0; }

                    if (ops > 20 && rand() % 20 == 0)
                    {
                        // delete a prop
                        //
                        if (dice == 0)
                        {
                            if (expectedNamedProps.size() == 0)
                            {
                                continue;
                            }
                            uint32_t k = static_cast<uint32_t>(rand()) % static_cast<uint32_t>(expectedNamedProps.size());
                            auto it = expectedNamedProps.begin();
                            for (uint32_t i = 0; i < k; i++) { it++; }

                            UserHeapPointer<void> key;
                            key.m_value = it->first;
                            expectedNamedProps.erase(it);

                            PutByIdICInfo icInfo;
                            TableObject::PreparePutById(obj, key, icInfo /*out*/);
                            TableObject::PutById(obj, key.As<void>(), TValue::Nil(), icInfo);
                        }
                        else
                        {
                            if (expectedArrayPart.size() == 0)
                            {
                                continue;
                            }
                            uint32_t k = static_cast<uint32_t>(rand()) % static_cast<uint32_t>(expectedArrayPart.size());
                            auto it = expectedArrayPart.begin();
                            for (uint32_t i = 0; i < k; i++) { it++; }

                            double key = it->first;
                            expectedArrayPart.erase(key);

                            TableObject::PutByValDoubleIndex(obj, key, TValue::Nil());
                        }
                    }
                    else
                    {
                        if (dice == 0)
                        {
                            // Put a named prop
                            //
                            UserHeapPointer<HeapString> propToAdd;
                            while (true)
                            {
                                UserHeapPointer<HeapString> choice = strings[static_cast<size_t>(rand()) % strings.size()];
                                if (expectedNamedProps.count(choice.m_value))
                                {
                                    continue;
                                }
                                propToAdd = choice;
                                break;
                            }
                            TValue val = TValue::CreateInt32(rand(), TValue::x_int32Tag);
                            expectedNamedProps[propToAdd.m_value] = val;

                            PutByIdICInfo icInfo;
                            TableObject::PreparePutById(obj, propToAdd, icInfo /*out*/);
                            TableObject::PutById(obj, propToAdd.As<void>(), val, icInfo);
                        }
                        else
                        {
                            // Put an array part prop
                            //
                            dice = rand() % 15;
                            double key;
                            if (dice == 0)
                            {
                                key = rand() / 1000.0;
                            }
                            else if (dice == 1)
                            {
                                key = rand() - 1000000000;
                            }
                            else
                            {
                                key = rand() % 8000;
                            }
                            TValue val = TValue::CreateInt32(rand(), TValue::x_int32Tag);
                            expectedArrayPart[key] = val;

                            TableObject::PutByValDoubleIndex(obj, key, val);
                        }
                    }
                }
            }
        }
    }
}

}   // anonymous namespace

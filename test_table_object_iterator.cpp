#include "runtime_utils.h"
#include "gtest/gtest.h"
#include "test_vm_utils.h"

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
                TableObject* obj = TableObject::CreateEmptyTableObject(vm, structure, 0 /*initButterflyCap*/);
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
                        if (kv.m_key.IsDouble())
                        {
                            double key = kv.m_key.AsDouble();
                            ReleaseAssert(!showedUpArrayProps.count(key));
                            showedUpArrayProps.insert(key);
                            ReleaseAssert(expectedArrayPart.count(key));
                            ReleaseAssert(expectedArrayPart[key].m_value == kv.m_value.m_value);
                        }
                        else if (kv.m_key.IsInt32())
                        {
                            double key = kv.m_key.AsInt32();
                            ReleaseAssert(!showedUpArrayProps.count(key));
                            showedUpArrayProps.insert(key);
                            ReleaseAssert(expectedArrayPart.count(key));
                            ReleaseAssert(expectedArrayPart[key].m_value == kv.m_value.m_value);
                        }
                        else
                        {
                            ReleaseAssert(kv.m_key.IsPointer());
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

                            TableObject::RawPutByValDoubleIndex(obj, key, TValue::Nil());
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
                            TValue val = TValue::CreateInt32(rand());
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
                            TValue val = TValue::CreateInt32(rand());
                            expectedArrayPart[key] = val;

                            TableObject::RawPutByValDoubleIndex(obj, key, val);
                        }
                    }
                }
            }
        }
    }
}

// Lua explicitly states that deletion is allowed during iteration. This test check this scenario.
//
TEST(TableObjectIterator, IterateWithDeleteInBetween)
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

                for (bool testSlowNext : { false, true })
                {
                    for (int numOps : { 100, 200, 400, 800, 1000 })
                    {
                        Structure* structure = Structure::CreateInitialStructure(vm, static_cast<uint8_t>(inlineCapacity));
                        TableObject* obj = TableObject::CreateEmptyTableObject(vm, structure, 0 /*initButterflyCap*/);
                        std::unordered_map<double, TValue> expectedArrayPart;
                        std::unordered_map<int64_t, TValue> expectedNamedProps;

                        for (int ops = 0; ops < numOps; ops++)
                        {
                            int dice = rand() % 2;
                            if (!putNamedProps) { dice = 1; }
                            if (!putArrayProps) { dice = 0; }
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
                                TValue val = TValue::CreateInt32(rand());
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
                                TValue val = TValue::CreateInt32(rand());
                                expectedArrayPart[key] = val;

                                TableObject::RawPutByValDoubleIndex(obj, key, val);
                            }
                        }

                        std::vector<double> allArrayPartProps;
                        for (auto it : expectedArrayPart) { allArrayPartProps.push_back(it.first); }
                        std::vector<int64_t> allNamedProps;
                        for (auto it : expectedNamedProps) { allNamedProps.push_back(it.first); }

                        std::unordered_set<double> showedUpArrayPart;
                        std::unordered_set<int64_t> showedUpNamedProps;

                        std::unordered_set<double> deletedArrayPart;
                        std::unordered_set<int64_t> deletedNamedProps;

                        TableObjectIterator iter;
                        TValue lastKey = TValue::Nil();

                        while (true)
                        {
                            // Make one iteration
                            //
                            TableObjectIterator::KeyValuePair kv;

                            if (testSlowNext)
                            {
                                bool success = TableObjectIterator::GetNextFromKey(obj, lastKey, kv /*out*/);
                                ReleaseAssert(success);
                            }
                            else
                            {
                                kv = iter.Advance(obj);
                            }

                            {
                                TValue key = kv.m_key;
                                if (key.IsNil())
                                {
                                    break;
                                }

                                if (key.IsPointer())
                                {
                                    int64_t kp = key.AsPointer().m_value;
                                    ReleaseAssert(expectedNamedProps.count(kp));
                                    ReleaseAssert(!showedUpNamedProps.count(kp));
                                    ReleaseAssert(!deletedNamedProps.count(kp));
                                    ReleaseAssert(expectedNamedProps[kp].m_value == kv.m_value.m_value);
                                    showedUpNamedProps.insert(kp);
                                }
                                else
                                {
                                    double kd;
                                    if (key.IsInt32())
                                    {
                                        kd = key.AsInt32();
                                    }
                                    else
                                    {
                                        ReleaseAssert(key.IsDouble());
                                        kd = key.AsDouble();
                                    }
                                    ReleaseAssert(expectedArrayPart.count(kd));
                                    ReleaseAssert(!showedUpArrayPart.count(kd));
                                    ReleaseAssert(!deletedArrayPart.count(kd));
                                    ReleaseAssert(expectedArrayPart[kd].m_value == kv.m_value.m_value);
                                    showedUpArrayPart.insert(kd);
                                }
                            }

                            lastKey = kv.m_key;

                            // Make one deletion
                            // With 1/20 chance, delete the key we just iterated. Otherwise, delete a random key
                            //
                            TValue keyToDelete;
                            if (rand() % 20 == 0)
                            {
                                keyToDelete = lastKey;
                            }
                            else
                            {
                                while (true)
                                {
                                    int dice = rand() % 2;
                                    if (dice == 0 && allNamedProps.size() == 0) { dice = 1; }
                                    if (dice == 1 && allArrayPartProps.size() == 0) { dice = 0; }
                                    if (dice == 0)
                                    {
                                        // try delete a named prop
                                        //
                                        ReleaseAssert(allNamedProps.size() > 0);
                                        int64_t kp = allNamedProps[static_cast<size_t>(rand()) % allNamedProps.size()];
                                        if (deletedNamedProps.count(kp))
                                        {
                                            continue;
                                        }
                                        UserHeapPointer<void> p; p.m_value = kp;
                                        keyToDelete = TValue::CreatePointer(p);
                                        break;
                                    }
                                    else
                                    {
                                        // try delete an array part prop
                                        //
                                        ReleaseAssert(allArrayPartProps.size() > 0);
                                        double kd = allArrayPartProps[static_cast<size_t>(rand()) % allArrayPartProps.size()];
                                        if (deletedArrayPart.count(kd))
                                        {
                                            continue;
                                        }
                                        keyToDelete = TValue::CreateDouble(kd);
                                        break;
                                    }
                                }
                            }

                            if (keyToDelete.IsPointer())
                            {
                                int64_t kp = keyToDelete.AsPointer().m_value;
                                ReleaseAssert(expectedNamedProps.count(kp));
                                ReleaseAssert(!deletedNamedProps.count(kp));
                                deletedNamedProps.insert(kp);

                                PutByIdICInfo icInfo;
                                TableObject::PreparePutById(obj, keyToDelete.AsPointer(), icInfo /*out*/);
                                TableObject::PutById(obj, keyToDelete.AsPointer(), TValue::Nil(), icInfo);
                            }
                            else
                            {
                                double kd;
                                if (keyToDelete.IsInt32())
                                {
                                    kd = keyToDelete.AsInt32();
                                }
                                else
                                {
                                    ReleaseAssert(keyToDelete.IsDouble());
                                    kd = keyToDelete.AsDouble();
                                }
                                ReleaseAssert(expectedArrayPart.count(kd));
                                ReleaseAssert(!deletedArrayPart.count(kd));
                                deletedArrayPart.insert(kd);

                                TableObject::RawPutByValDoubleIndex(obj, kd, TValue::Nil());
                            }
                        }

                        // Now, validate the iteration has indeed gone through all keys that aren't deleted
                        //
                        for (auto it : expectedArrayPart)
                        {
                            double kd = it.first;
                            ReleaseAssert(deletedArrayPart.count(kd) || showedUpArrayPart.count(kd));
                        }

                        for (auto it : expectedNamedProps)
                        {
                            int64_t kp = it.first;
                            ReleaseAssert(deletedNamedProps.count(kp) || showedUpNamedProps.count(kp));
                        }
                    }
                }
            }
        }
    }
}

}   // anonymous namespace

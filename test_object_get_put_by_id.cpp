#include "runtime_utils.h"
#include "gtest/gtest.h"
#include "test_vm_utils.h"

namespace {

TEST(ObjectGetPutById, Sanity)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    StringList strings = GetStringList(VM::GetActiveVMForCurrentThread(), 1000 /*numStrings*/);

    uint32_t inlineCapacity = 8;
    uint32_t numTestCases = x_isDebugBuild ? 100 : 500;    // total # of objects
    uint32_t numProps = 253;    // props per object

    auto checkProperty = [&](HeapPtr<TableObject> obj, UserHeapPointer<HeapString> prop, bool expectExist, uint32_t expectedAbsoluteOrd, int32_t expectedVal)
    {
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(obj, prop, icInfo /*out*/);
        ReleaseAssert(!icInfo.m_mayHaveMetatable);
        if (!expectExist)
        {
            ReleaseAssert(icInfo.m_icKind == GetByIdICInfo::ICKind::MustBeNil);
        }
        else
        {
            if (expectedAbsoluteOrd < inlineCapacity)
            {
                ReleaseAssert(icInfo.m_icKind == GetByIdICInfo::ICKind::InlinedStorage);
                ReleaseAssert(icInfo.m_slot == static_cast<int32_t>(expectedAbsoluteOrd));
            }
            else
            {
                ReleaseAssert(icInfo.m_icKind == GetByIdICInfo::ICKind::OutlinedStorage);
                ReleaseAssert(icInfo.m_slot == static_cast<int32_t>(inlineCapacity - expectedAbsoluteOrd - 1));
            }
        }
        TValue result = TableObject::GetById(obj, prop.As<void>(), icInfo);
        if (!expectExist)
        {
            ReleaseAssert(result.IsNil());
        }
        else
        {
            ReleaseAssert(result.IsInt32());
            ReleaseAssert(expectedVal == result.AsInt32());
        }
    };

    Structure* initStructure = Structure::CreateInitialStructure(VM::GetActiveVMForCurrentThread(), static_cast<uint8_t>(inlineCapacity));
    std::vector<std::vector<UserHeapPointer<HeapString>>> allProps;
    std::vector<HeapPtr<TableObject>> allObjects;
    for (uint32_t testCase = 0; testCase < numTestCases; testCase++)
    {
        uint32_t initArrayPartSize = static_cast<uint32_t>(rand() % 5);
        HeapPtr<TableObject> curObject = TableObject::CreateEmptyTableObject(vm, initStructure, initArrayPartSize);
        // Sanity check the array part is initialized correctly
        //
        if (initArrayPartSize == 0)
        {
            ReleaseAssert(curObject->m_butterfly == nullptr);
        }
        else
        {
            ReleaseAssert(curObject->m_butterfly->GetHeader()->m_arrayStorageCapacity == static_cast<int32_t>(initArrayPartSize));
            ReleaseAssert(curObject->m_butterfly->GetHeader()->m_arrayLengthIfContinuous == ArrayGrowthPolicy::x_arrayBaseOrd);
            for (uint32_t i = 0; i < initArrayPartSize; i++)
            {
                ReleaseAssert(curObject->m_butterfly->UnsafeGetInVectorIndexAddr(static_cast<int32_t>(i + ArrayGrowthPolicy::x_arrayBaseOrd))->IsNil());
                *curObject->m_butterfly->UnsafeGetInVectorIndexAddr(static_cast<int32_t>(i + ArrayGrowthPolicy::x_arrayBaseOrd)) = TValue::CreateInt32(static_cast<int32_t>(i + 12345));
            }
        }

        std::set<int64_t> usedProps;
        allProps.push_back(std::vector<UserHeapPointer<HeapString>>());
        std::vector<UserHeapPointer<HeapString>>& curProps = allProps.back();
        for (uint32_t i = 0; i < numProps; i++)
        {
            UserHeapPointer<HeapString> propToAdd;
            while (true)
            {
                UserHeapPointer<HeapString> choice = strings[static_cast<size_t>(rand()) % strings.size()];
                if (usedProps.count(choice.m_value))
                {
                    continue;
                }
                usedProps.insert(choice.m_value);
                propToAdd = choice;
                break;
            }
            curProps.push_back(propToAdd);

            PutByIdICInfo icInfo;
            TableObject::PreparePutById(curObject, propToAdd, icInfo /*out*/);
            ReleaseAssert(!icInfo.m_propertyExists);
            ReleaseAssert(!icInfo.m_mayHaveMetatable);
            if (i < inlineCapacity)
            {
                ReleaseAssert(icInfo.m_icKind == PutByIdICInfo::ICKind::InlinedStorage);
                ReleaseAssert(icInfo.m_slot == static_cast<int32_t>(i));
            }
            else
            {
                ReleaseAssert(icInfo.m_icKind == PutByIdICInfo::ICKind::OutlinedStorage);
                ReleaseAssert(icInfo.m_slot == static_cast<int32_t>(inlineCapacity) - static_cast<int32_t>(i) - 1);
            }
            ReleaseAssert(TCGet(curObject->m_hiddenClass).As() == icInfo.m_hiddenClass.As<Structure>());
            if (i == icInfo.m_hiddenClass.As<Structure>()->m_butterflyNamedStorageCapacity + icInfo.m_hiddenClass.As<Structure>()->m_inlineNamedStorageCapacity)
            {
                ReleaseAssert(icInfo.m_shouldGrowButterfly);
            }
            else
            {
                ReleaseAssert(!icInfo.m_shouldGrowButterfly);
            }
            ReleaseAssert(icInfo.m_hiddenClass.As<Structure>() != icInfo.m_newStructure.As());

            TableObject::PutById(curObject, propToAdd.As<void>(), TValue::CreateInt32(static_cast<int32_t>(i + 456)), icInfo);

            // Check the PutById didn't screw the array part
            //
            if (initArrayPartSize == 0)
            {
                if (i < inlineCapacity)
                {
                    ReleaseAssert(curObject->m_butterfly == nullptr);
                }
                else
                {
                    ReleaseAssert(curObject->m_butterfly != nullptr);
                    ReleaseAssert(curObject->m_butterfly->GetHeader()->m_arrayStorageCapacity == 0);
                    ReleaseAssert(curObject->m_butterfly->GetHeader()->m_arrayLengthIfContinuous == ArrayGrowthPolicy::x_arrayBaseOrd);
                }
            }
            else
            {
                ReleaseAssert(curObject->m_butterfly != nullptr);
                ReleaseAssert(curObject->m_butterfly->GetHeader()->m_arrayStorageCapacity == static_cast<int32_t>(initArrayPartSize));
                ReleaseAssert(curObject->m_butterfly->GetHeader()->m_arrayLengthIfContinuous == ArrayGrowthPolicy::x_arrayBaseOrd);
                for (uint32_t k = 0; k < initArrayPartSize; k++)
                {
                    TValue val = *curObject->m_butterfly->UnsafeGetInVectorIndexAddr(static_cast<int32_t>(k) + ArrayGrowthPolicy::x_arrayBaseOrd);
                    ReleaseAssert(val.IsInt32() && val.AsInt32() == static_cast<int32_t>(k + 12345));
                }
            }

            // Check some existent fields
            //
            for (uint32_t numFieldToCheck = 0; numFieldToCheck < 3; numFieldToCheck++)
            {
                size_t propOrdToCheck;
                if (numFieldToCheck == 0)
                {
                    propOrdToCheck = i;
                }
                else
                {
                    propOrdToCheck = static_cast<size_t>(rand()) % curProps.size();
                }

                UserHeapPointer<HeapString> propToCheck = curProps[propOrdToCheck];
                checkProperty(curObject, propToCheck, true /*expectExist*/, static_cast<uint32_t>(propOrdToCheck) /*expectedAbsoluteOrd*/, static_cast<int32_t>(propOrdToCheck + 456) /*expectedVal*/);
            }

            // Check some non-existent fields
            //
            uint32_t totalFieldsToCheck = 3;
            if (i == numProps - 1)
            {
                // if this is the last insertion for this object, check a bit more
                //
                totalFieldsToCheck = 100;
            }
            for (uint32_t numFieldToCheck = 0; numFieldToCheck < totalFieldsToCheck; numFieldToCheck++)
            {
                UserHeapPointer<HeapString> propToCheck;
                while (true)
                {
                    propToCheck = strings[static_cast<size_t>(rand()) % strings.size()];
                    if (!usedProps.count(propToCheck.m_value))
                    {
                        break;
                    }
                }
                checkProperty(curObject, propToCheck, false /*expectExist*/, 0, 0);
            }
        }
        ReleaseAssert(curProps.size() == numProps);
        allObjects.push_back(curObject);
    }

    // Now, check all the objects are as expected, and that GetById also work correctly on all properties
    //
    ReleaseAssert(allObjects.size() == numTestCases);
    ReleaseAssert(allProps.size() == numTestCases);

    auto checkAllObjectsAreAsExpected = [&](uint32_t expectedBaseValue)
    {
        for (uint32_t testcase = 0; testcase < numTestCases; testcase++)
        {
            HeapPtr<TableObject> obj = allObjects[testcase];
            std::vector<UserHeapPointer<HeapString>>& propList = allProps[testcase];
            ReleaseAssert(propList.size() == numProps);

            for (uint32_t i = 0; i < inlineCapacity; i++)
            {
                TValue val = TCGet(obj->m_inlineStorage[i]);
                ReleaseAssert(val.IsInt32());
                ReleaseAssert(val.AsInt32() == static_cast<int32_t>(i + expectedBaseValue));
            }

            for (uint32_t i = inlineCapacity; i < numProps; i++)
            {
                TValue val = obj->m_butterfly->GetNamedProperty(static_cast<int32_t>(inlineCapacity - i - 1));
                ReleaseAssert(val.IsInt32());
                ReleaseAssert(val.AsInt32() == static_cast<int32_t>(i + expectedBaseValue));
            }

            for (uint32_t i = 0; i < numProps; i++)
            {
                UserHeapPointer<HeapString> propToCheck = propList[i];
                checkProperty(obj, propToCheck, true /*expectExist*/, i /*expectedAbsoluteOrd*/, static_cast<int32_t>(i + expectedBaseValue) /*expectedVal*/);
            }
        }
    };
    checkAllObjectsAreAsExpected(456);

    // Use PutById to rewrite all properties to new values, to validate that PutById works correctly on existent properties
    //
    for (uint32_t testcase = 0; testcase < numTestCases; testcase++)
    {
        HeapPtr<TableObject> obj = allObjects[testcase];
        std::vector<UserHeapPointer<HeapString>>& propList = allProps[testcase];

        for (uint32_t i = 0; i < numProps; i++)
        {
            UserHeapPointer<HeapString> prop = propList[i];

            PutByIdICInfo icInfo;
            TableObject::PreparePutById(obj, prop, icInfo /*out*/);
            ReleaseAssert(!icInfo.m_mayHaveMetatable);
            ReleaseAssert(icInfo.m_propertyExists);
            ReleaseAssert(!icInfo.m_shouldGrowButterfly);
            if (i < inlineCapacity)
            {
                ReleaseAssert(icInfo.m_icKind == PutByIdICInfo::ICKind::InlinedStorage);
                ReleaseAssert(icInfo.m_slot == static_cast<int32_t>(i));
            }
            else
            {
                ReleaseAssert(icInfo.m_icKind == PutByIdICInfo::ICKind::OutlinedStorage);
                ReleaseAssert(icInfo.m_slot == static_cast<int32_t>(inlineCapacity - i - 1));
            }

            TableObject::PutById(obj, prop.As<void>(), TValue::CreateInt32(static_cast<int32_t>(i + 7890)), icInfo);
        }
    }

    // Check that all the objects are expected, and GetById gives the expected values after all the writes
    //
    checkAllObjectsAreAsExpected(7890);

    // Finally, just to sanity check that the Structures are working, redo all the PutById from empty objects,
    // and validate the resulting structure is the same
    //
    std::vector<HeapPtr<TableObject>> newObjectList;
    for (uint32_t testcase = 0; testcase < numTestCases; testcase++)
    {
        uint32_t initArrayPartSize = static_cast<uint32_t>(allObjects[testcase]->m_butterfly->GetHeader()->m_arrayStorageCapacity);
        HeapPtr<TableObject> curObject = TableObject::CreateEmptyTableObject(vm, initStructure, initArrayPartSize);

        std::vector<UserHeapPointer<HeapString>>& propList = allProps[testcase];

        for (uint32_t i = 0; i < numProps; i++)
        {
            UserHeapPointer<HeapString> prop = propList[i];

            PutByIdICInfo icInfo;
            TableObject::PreparePutById(curObject, prop, icInfo /*out*/);
            ReleaseAssert(!icInfo.m_propertyExists);
            TableObject::PutById(curObject, prop.As<void>(), TValue::CreateInt32(static_cast<int32_t>(i + 45678)), icInfo);
        }

        ReleaseAssert(curObject != allObjects[testcase]);
        ReleaseAssert(curObject->m_butterfly != allObjects[testcase]->m_butterfly);
        ReleaseAssert(TCGet(curObject->m_hiddenClass) == TCGet(allObjects[testcase]->m_hiddenClass));
        allObjects[testcase] = curObject;
    }

    checkAllObjectsAreAsExpected(45678);
}

TEST(ObjectGetSetById, CacheableDictionary)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    uint32_t numStrings = 2000;
    StringList strings = GetStringList(VM::GetActiveVMForCurrentThread(), numStrings);

    Structure* initStructure = Structure::CreateInitialStructure(VM::GetActiveVMForCurrentThread(), 8 /*inlineCapacity*/);
    for (uint32_t testCase = 0; testCase < 50; testCase++)
    {
        const uint32_t maxArrayPartSize = 5;
        uint32_t initArrayPartSize = static_cast<uint32_t>(rand()) % maxArrayPartSize;
        uint32_t initArrayPartFillSize = static_cast<uint32_t>(rand()) % (initArrayPartSize + 1);
        HeapPtr<TableObject> obj = TableObject::CreateEmptyTableObject(vm, initStructure, initArrayPartSize);

        std::unordered_map<int64_t, TValue> expected;
        TValue arrayExpected[maxArrayPartSize + 2];
        for (uint32_t i = 0; i <= maxArrayPartSize; i++)
        {
            arrayExpected[i] = TValue::Nil();
        }

        auto getRandomValue = [&]() -> TValue
        {
            int dice = rand() % 3;
            if (dice == 0)
            {
                return TValue::CreateInt32(rand());
            }
            else if (dice == 1)
            {
                return TValue::CreateDouble(rand() / static_cast<double>(1000.0));
            }
            else
            {
                return TValue::CreatePointer(strings[static_cast<uint32_t>(rand()) % numStrings]);
            }
        };

        if (rand() % 2 == 0)
        {
            for (uint32_t i = 1; i <= initArrayPartFillSize; i++)
            {
                TValue val = getRandomValue();
                TableObject::RawPutByValIntegerIndex(obj, static_cast<int32_t>(i), val);
                arrayExpected[i] = val;
            }
        }
        else
        {
            for (uint32_t i = initArrayPartFillSize; i >= 1; i--)
            {
                TValue val = getRandomValue();
                TableObject::RawPutByValIntegerIndex(obj, static_cast<int32_t>(i), val);
                arrayExpected[i] = val;
            }
        }


        for (uint32_t testOp = 0; testOp < 3000; testOp++)
        {
            // Perform a put
            //
            {
                uint32_t ord = static_cast<uint32_t>(rand()) % numStrings;
                PutByIdICInfo icInfo;
                TableObject::PreparePutById(obj, strings[ord], icInfo /*out*/);
                TValue newVal = getRandomValue();
                TableObject::PutById(obj, strings[ord].As<void>(), newVal, icInfo);
                expected[strings[ord].m_value] = newVal;
            }
            // Perform a query
            //
            {
                UserHeapPointer<HeapString> prop = strings[static_cast<uint32_t>(rand()) % numStrings];
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(obj, prop, icInfo /*out*/);
                TValue result = TableObject::GetById(obj, prop.As<void>(), icInfo);
                if (expected.count(prop.m_value))
                {
                    ReleaseAssert(result.m_value == expected[prop.m_value].m_value);
                }
                else
                {
                    ReleaseAssert(result.IsNil());
                }
            }
        }

        for (uint32_t i = 0; i <= maxArrayPartSize; i++)
        {
            GetByIntegerIndexICInfo icInfo;
            TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
            {
                TValue result = TableObject::GetByDoubleVal(obj, static_cast<int32_t>(i), icInfo);
                ReleaseAssert(result.m_value == arrayExpected[i].m_value);
            }
            {
                TValue result = TableObject::GetByInt32Val(obj, static_cast<int32_t>(i), icInfo);
                ReleaseAssert(result.m_value == arrayExpected[i].m_value);
            }
        }
    }
}

// CacheableDictionary can only IC on hit GetById, not on miss GetById, because we have no way
// to know when the property is added in the future
//
TEST(ObjectGetPutById, MissedGetByIdIsUncacheableForCacheableDicitonary)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    const uint32_t numStrings = 500;
    StringList strings = GetStringList(VM::GetActiveVMForCurrentThread(), numStrings);
    Structure* initStructure = Structure::CreateInitialStructure(VM::GetActiveVMForCurrentThread(), 8 /*inlineCapacity*/);
    HeapPtr<TableObject> curObject = TableObject::CreateEmptyTableObject(vm, initStructure, 0 /*initArraySize*/);
    for (uint32_t i = 0; i < numStrings - 1; i++)
    {
        UserHeapPointer<HeapString> propToAdd = strings[i];
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(curObject, propToAdd, icInfo /*out*/);
        TableObject::PutById(curObject, propToAdd.As<void>(), TValue::CreateInt32(static_cast<int32_t>(i + 456)), icInfo);
    }

    {
        UserHeapPointer<HeapString> propToTest = strings[numStrings - 1];
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(curObject, propToTest, icInfo /*out*/);
        ReleaseAssert(icInfo.m_hiddenClass.As() == TCGet(curObject->m_hiddenClass).As());
        ReleaseAssert(icInfo.m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::CacheableDictionary);
        ReleaseAssert(icInfo.m_mayHaveMetatable == false);
        ReleaseAssert(icInfo.m_icKind == GetByIdICInfo::ICKind::MustBeNilButUncacheable);
    }
}

}   // anonymous namespace

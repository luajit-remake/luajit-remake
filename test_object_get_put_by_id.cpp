#include "bytecode.h"
#include "gtest/gtest.h"

using namespace ToyLang;

namespace {

using StringList = std::vector<UserHeapPointer<HeapString>>;

StringList GetStringList(VM* vm, size_t n)
{
    std::set<std::string> used;
    StringList result;
    std::set<int64_t> ptrSet;
    for (size_t i = 0; i < n; i++)
    {
        std::string s;
        while (true)
        {
            s = "";
            size_t len = static_cast<size_t>(rand() % 20);
            for (size_t k = 0; k < len; k++) s += 'a' + rand() % 26;
            if (!used.count(s))
            {
                break;
            }
        }
        used.insert(s);
        UserHeapPointer<HeapString> ptr = vm->CreateStringObjectFromRawString(s.c_str(), static_cast<uint32_t>(s.length()));
        result.push_back(ptr);
        ReleaseAssert(!ptrSet.count(ptr.m_value));
        ptrSet.insert(ptr.m_value);
    }
    ReleaseAssert(used.size() == n && ptrSet.size() == n && result.size() == n);
    return result;
}

TEST(ObjectGetSetById, Sanity)
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
        TValue result = TableObject::GetById(obj, prop, icInfo);
        if (!expectExist)
        {
            ReleaseAssert(result.IsNil());
        }
        else
        {
            ReleaseAssert(result.IsInt32(TValue::x_int32Tag));
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
                *curObject->m_butterfly->UnsafeGetInVectorIndexAddr(static_cast<int32_t>(i + ArrayGrowthPolicy::x_arrayBaseOrd)) = TValue::CreateInt32(static_cast<int32_t>(i + 12345), TValue::x_int32Tag);
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
            ReleaseAssert(TCGet(curObject->m_hiddenClass).As() == icInfo.m_structure.As());
            if (i == icInfo.m_structure.As()->m_butterflyNamedStorageCapacity + icInfo.m_structure.As()->m_inlineNamedStorageCapacity)
            {
                ReleaseAssert(icInfo.m_shouldGrowButterfly);
            }
            else
            {
                ReleaseAssert(!icInfo.m_shouldGrowButterfly);
            }
            ReleaseAssert(icInfo.m_structure.As() != icInfo.m_newStructure.As());

            TableObject::PutById(curObject, propToAdd, TValue::CreateInt32(static_cast<int32_t>(i + 456), TValue::x_int32Tag), icInfo);

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
                    ReleaseAssert(val.IsInt32(TValue::x_int32Tag) && val.AsInt32() == static_cast<int32_t>(k + 12345));
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
                ReleaseAssert(val.IsInt32(TValue::x_int32Tag));
                ReleaseAssert(val.AsInt32() == static_cast<int32_t>(i + expectedBaseValue));
            }

            for (uint32_t i = inlineCapacity; i < numProps; i++)
            {
                TValue val = obj->m_butterfly->GetNamedProperty(static_cast<int32_t>(inlineCapacity - i - 1));
                ReleaseAssert(val.IsInt32(TValue::x_int32Tag));
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

            TableObject::PutById(obj, prop, TValue::CreateInt32(static_cast<int32_t>(i + 7890), TValue::x_int32Tag), icInfo);
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
            TableObject::PutById(curObject, prop, TValue::CreateInt32(static_cast<int32_t>(i + 45678), TValue::x_int32Tag), icInfo);
        }

        ReleaseAssert(TCGet(curObject->m_hiddenClass) == TCGet(allObjects[testcase]->m_hiddenClass));
        allObjects[testcase] = curObject;
    }

    checkAllObjectsAreAsExpected(45678);
}

}   // anonymous namespace

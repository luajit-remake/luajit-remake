#include "runtime_utils.h"
#include "gtest/gtest.h"
#include "test_vm_utils.h"

namespace {

// Test the transition on NoButterflyPart, that is, test the array type is
// as expected when we put the first element into the array part
//
TEST(ObjectArrayPart, PutFirstElement)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    constexpr uint32_t numPropsToAdd = 4;
    StringList strings = GetStringList(vm, numPropsToAdd);

    for (uint32_t inlineCap : { 0U, numPropsToAdd / 2, numPropsToAdd })
    {
        Structure* initStructure = Structure::CreateInitialStructure(vm, static_cast<uint8_t>(inlineCap));
        for (uint32_t initButterflyCap : { 0U, 1U, 2U, 4U, 8U })
        {
            for (uint32_t numNamedProps : { 0U, numPropsToAdd })
            {
                for (bool hasButterfly : { false, true })
                {
                    // Rule out impossible case that a butterfly is required but hasButterfly == false
                    //
                    if (!hasButterfly)
                    {
                        if (numNamedProps > inlineCap || initButterflyCap > 0)
                        {
                            continue;
                        }
                    }

                    for (int64_t putLocation : { -1LL, 0LL, 1LL, 2LL, ArrayGrowthPolicy::x_alwaysVectorCutoff * 2LL, 50000000000LL})
                    {
                        enum PutType
                        {
                            Nil,
                            Int32,
                            Double,
                            Object
                        };

                        for (PutType valTy : { PutType::Nil, PutType::Int32, PutType::Double, PutType::Object })
                        {
                            auto checkArrayKindForNonNilValueAndNoSparseMapCase = [valTy](ArrayType ty)
                            {
                                switch (valTy)
                                {
                                case PutType::Nil:
                                {
                                    ReleaseAssert(false);
                                    break;
                                }
                                case PutType::Int32:
                                {
                                    ReleaseAssert(ty.ArrayKind() == ArrayType::Kind::Int32);
                                    break;
                                }
                                case PutType::Double:
                                {
                                    ReleaseAssert(ty.ArrayKind() == ArrayType::Kind::Double);
                                    break;
                                }
                                case PutType::Object:
                                {
                                    ReleaseAssert(ty.ArrayKind() == ArrayType::Kind::Any);
                                }
                                }
                            };

                            HeapPtr<TableObject> obj = TableObject::CreateEmptyTableObject(vm, initStructure, initButterflyCap);

                            for (uint32_t i = 0; i < numNamedProps; i++)
                            {
                                PutByIdICInfo icInfo;
                                TableObject::PreparePutById(obj, strings[i], icInfo /*out*/);
                                TableObject::PutById(obj, strings[i].As<void>(), TValue::CreateInt32(static_cast<int32_t>(12345 + i)), icInfo);
                            }

                            if (hasButterfly && obj->m_butterfly == nullptr)
                            {
                                TranslateToRawPointer(obj)->GrowButterfly<false /*isGrowNamedStorage*/>(0);
                            }

                            if (!hasButterfly)
                            {
                                ReleaseAssert(obj->m_butterfly == nullptr);
                            }
                            else
                            {
                                ReleaseAssert(obj->m_butterfly != nullptr);
                            }

                            // For sanity, check that the read on the array part returns nil
                            //
                            for (int64_t readLocation : { -1LL, 0LL, 1LL, 2LL, ArrayGrowthPolicy::x_alwaysVectorCutoff * 2LL, 50000000000LL})
                            {
                                GetByIntegerIndexICInfo icInfo;
                                TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
                                TValue result = TableObject::GetByIntegerIndex(obj, readLocation, icInfo);
                                ReleaseAssert(result.IsNil());
                            }

                            TValue value;
                            switch (valTy)
                            {
                            case PutType::Nil:
                            {
                                value = TValue::Nil();
                                break;
                            }
                            case PutType::Int32:
                            {
                                value = TValue::CreateInt32(6789);
                                break;
                            }
                            case PutType::Double:
                            {
                                value = TValue::CreateDouble(12.25);
                                break;
                            }
                            case PutType::Object:
                            {
                                value = TValue::CreatePointer(UserHeapPointer<TableObject> { obj });
                            }
                            }

                            {
                                bool expectFastPathSuccess = (valTy != PutType::Nil && hasButterfly && obj->m_butterfly->GetHeader()->m_arrayStorageCapacity > 0 && putLocation == ArrayGrowthPolicy::x_arrayBaseOrd);

                                PutByIntegerIndexICInfo icInfo;
                                TableObject::PreparePutByIntegerIndex(obj, putLocation, value, icInfo /*out*/);
                                bool fastPathSuccess = TableObject::TryPutByIntegerIndexFast(obj, putLocation, value, icInfo);

                                if (expectFastPathSuccess)
                                {
                                    ReleaseAssert(fastPathSuccess);
                                }
                                else
                                {
                                    ReleaseAssert(!fastPathSuccess);
                                    TranslateToRawPointer(vm, obj)->PutByIntegerIndexSlow(vm, putLocation, value);
                                }
                            }

                            // Validate the object after the put is good
                            //

                            // Check the array type is as expected
                            //
                            if (valTy == PutType::Nil)
                            {
                                // Currently, if the index is vector-qualifying, there is a check to no-op this put
                                // But for sparse map index, there is no check and we always create a sparse map
                                //
                                if (ArrayGrowthPolicy::x_arrayBaseOrd <= putLocation && putLocation <= ArrayGrowthPolicy::x_unconditionallySparseMapCutoff)
                                {
                                    ArrayType arrType = TCGet(obj->m_arrayType);
                                    ReleaseAssert(arrType.m_asValue == TCGet(obj->m_hiddenClass).As<Structure>()->m_arrayType.m_asValue);
                                    ReleaseAssert(!arrType.IsContinuous());
                                    ReleaseAssert(!arrType.HasSparseMap());
                                    ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::NoButterflyArrayPart);
                                }
                                else
                                {
                                    ArrayType arrType = TCGet(obj->m_arrayType);
                                    ReleaseAssert(arrType.m_asValue == TCGet(obj->m_hiddenClass).As<Structure>()->m_arrayType.m_asValue);
                                    ReleaseAssert(!arrType.IsContinuous());
                                    ReleaseAssert(arrType.HasSparseMap());
                                    ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                                }
                            }
                            else
                            {
                                ArrayType arrType = TCGet(obj->m_arrayType);
                                ReleaseAssert(arrType.m_asValue == TCGet(obj->m_hiddenClass).As<Structure>()->m_arrayType.m_asValue);

                                if (ArrayGrowthPolicy::x_arrayBaseOrd == putLocation)
                                {
                                    // Expect continuous array
                                    //
                                    ReleaseAssert(arrType.IsContinuous());
                                    ReleaseAssert(!arrType.HasSparseMap());
                                    ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                    ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous == ArrayGrowthPolicy::x_arrayBaseOrd + 1);
                                    checkArrayKindForNonNilValueAndNoSparseMapCase(arrType);
                                }
                                else if (ArrayGrowthPolicy::x_arrayBaseOrd <= putLocation && putLocation <= ArrayGrowthPolicy::x_alwaysVectorCutoff)
                                {
                                    // Expect not continuous, but vector array
                                    //
                                    ReleaseAssert(!arrType.IsContinuous());
                                    ReleaseAssert(!arrType.HasSparseMap());
                                    ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                    ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous == ArrayGrowthPolicy::x_arrayBaseOrd - 1);
                                    ReleaseAssert(obj->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(putLocation));
                                    checkArrayKindForNonNilValueAndNoSparseMapCase(arrType);
                                }
                                else if (ArrayGrowthPolicy::x_alwaysVectorCutoff < putLocation && putLocation <= ArrayGrowthPolicy::x_unconditionallySparseMapCutoff)
                                {
                                    // Expect sparse map overlapping vector index
                                    //
                                    ReleaseAssert(!arrType.IsContinuous());
                                    ReleaseAssert(arrType.HasSparseMap());
                                    ReleaseAssert(arrType.SparseMapContainsVectorIndex());
                                    ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous < ArrayGrowthPolicy::x_arrayBaseOrd - 1);
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                                }
                                else
                                {
                                    // Expect sparse map but not overlapping vector index
                                    //
                                    ReleaseAssert(!arrType.IsContinuous());
                                    ReleaseAssert(arrType.HasSparseMap());
                                    ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                    ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous < ArrayGrowthPolicy::x_arrayBaseOrd - 1);
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                                }
                            }

                            // Check reads: read the named properties first
                            //
                            for (uint32_t i = 0; i < numNamedProps; i++)
                            {
                                GetByIdICInfo icInfo;
                                TableObject::PrepareGetById(obj, strings[i], icInfo /*out*/);
                                TValue result = TableObject::GetById(obj, strings[i].As<void>(), icInfo);
                                ReleaseAssert(result.IsInt32());
                                ReleaseAssert(result.AsInt32() == static_cast<int32_t>(12345 + i));
                            }

                            // Check reads: read the array elements
                            //
                            for (int64_t readLocation : { -1LL, 0LL, 1LL, 2LL, 4LL, ArrayGrowthPolicy::x_alwaysVectorCutoff * 2LL, 50000000000LL})
                            {
                                GetByIntegerIndexICInfo icInfo;
                                TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
                                TValue result = TableObject::GetByIntegerIndex(obj, readLocation, icInfo);
                                if (readLocation == putLocation)
                                {
                                    ReleaseAssert(result.m_value == value.m_value);
                                }
                                else
                                {
                                    ReleaseAssert(result.IsNil());
                                }
                            }

                            // Put an out of bound array element to force a butterfly growth
                            //
                            if (valTy == PutType::Nil)
                            {
                                continue;
                            }

                            int64_t index2 = ArrayGrowthPolicy::x_alwaysVectorCutoff / 2;
                            int32_t oldVectorCapacity = obj->m_butterfly->GetHeader()->m_arrayStorageCapacity;

                            {
                                PutByIntegerIndexICInfo icInfo;
                                TableObject::PreparePutByIntegerIndex(obj, index2, value, icInfo /*out*/);
                                bool fastPathSuccess = TableObject::TryPutByIntegerIndexFast(obj, index2, value, icInfo);
                                ReleaseAssert(!fastPathSuccess);
                                TranslateToRawPointer(vm, obj)->PutByIntegerIndexSlow(vm, index2, value);
                            }

                            // Check the array type is as expected
                            //
                            {
                                ArrayType arrType = TCGet(obj->m_arrayType);
                                ReleaseAssert(arrType.m_asValue == TCGet(obj->m_hiddenClass).As<Structure>()->m_arrayType.m_asValue);

                                if (ArrayGrowthPolicy::x_alwaysVectorCutoff < putLocation && putLocation <= ArrayGrowthPolicy::x_unconditionallySparseMapCutoff)
                                {
                                    // The first index we put resulted in sparse map containing vector index
                                    // So vector index must not grow
                                    //
                                    ReleaseAssert(!arrType.IsContinuous());
                                    ReleaseAssert(arrType.HasSparseMap());
                                    ReleaseAssert(arrType.SparseMapContainsVectorIndex());
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                                    ReleaseAssert(!obj->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(index2));
                                    ReleaseAssert(oldVectorCapacity == obj->m_butterfly->GetHeader()->m_arrayStorageCapacity);
                                }
                                else if (ArrayGrowthPolicy::x_arrayBaseOrd <= putLocation && putLocation <= ArrayGrowthPolicy::x_alwaysVectorCutoff)
                                {
                                    ReleaseAssert(!arrType.IsContinuous());
                                    ReleaseAssert(!arrType.HasSparseMap());
                                    ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                    checkArrayKindForNonNilValueAndNoSparseMapCase(arrType);
                                    ReleaseAssert(obj->m_butterfly->GetHeader()->IndexFitsInVectorCapacity(index2));
                                }
                                else
                                {
                                    ReleaseAssert(!arrType.IsContinuous());
                                    ReleaseAssert(arrType.HasSparseMap());
                                    ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                                }
                            }

                            // Check reads: read the named properties first
                            //
                            for (uint32_t i = 0; i < numNamedProps; i++)
                            {
                                GetByIdICInfo icInfo;
                                TableObject::PrepareGetById(obj, strings[i], icInfo /*out*/);
                                TValue result = TableObject::GetById(obj, strings[i].As<void>(), icInfo);
                                ReleaseAssert(result.IsInt32());
                                ReleaseAssert(result.AsInt32() == static_cast<int32_t>(12345 + i));
                            }

                            // Check reads: read the array elements
                            //
                            for (int64_t readLocation : { -1LL, 0LL, 1LL, 2LL, 4LL, ArrayGrowthPolicy::x_alwaysVectorCutoff * 2LL, ArrayGrowthPolicy::x_alwaysVectorCutoff / 2LL, 50000000000LL})
                            {
                                GetByIntegerIndexICInfo icInfo;
                                TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
                                TValue result = TableObject::GetByIntegerIndex(obj, readLocation, icInfo);
                                if (readLocation == putLocation)
                                {
                                    ReleaseAssert(result.m_value == value.m_value);
                                }
                                else if (readLocation == index2)
                                {
                                    ReleaseAssert(result.m_value == value.m_value);
                                }
                                else
                                {
                                    ReleaseAssert(result.IsNil());
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST(ObjectArrayPart, ContinuousArray)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    constexpr uint32_t numAllString = 600;
    StringList strings = GetStringList(vm, numAllString);

    for (uint32_t inlineCap : { 0U, 2U, 4U })
    {
        Structure* initStructure = Structure::CreateInitialStructure(vm, static_cast<uint8_t>(inlineCap));
        for (uint32_t initButterflyCap : { 0U, 1U, 2U, 4U, 8U })
        {
            for (uint32_t numNamedProps : { 0U, 4U, numAllString })
            {
                enum PutType
                {
                    Int32,
                    Double,
                    Object
                };
                for (PutType baseType : { PutType::Int32, PutType::Double, PutType::Object })
                {
                    auto getValueForInsert = [&]() -> TValue
                    {
                        if (baseType == PutType::Int32)
                        {
                            return TValue::CreateInt32(rand() % 10000);
                        }
                        else if (baseType == PutType::Double)
                        {
                            return TValue::CreateDouble(rand() / static_cast<double>(100000.0));
                        }
                        else
                        {
                            return TValue::CreatePointer(strings[static_cast<size_t>(rand()) % numAllString]);
                        }
                    };

                    enum class EndTransitionType
                    {
                        InArrayNil,
                        InArrayNonNil,
                        EndOfArray,
                        MakeNotContinuous,
                        MakeNotContinuousAndOutOfRange,
                        MakeNotContinuousAndVectorRangeSparseMap,
                        MakeNotContinuousAndSparseMap,
                        X_END_OF_ENUM
                    };
                    for (bool lastInsertSameKindElement : { false, true })
                    {
                        for (int ett_ = 0; ett_ < static_cast<int>(EndTransitionType::X_END_OF_ENUM); ett_++)
                        {
                            EndTransitionType ett = static_cast<EndTransitionType>(ett_);
                            if (baseType == PutType::Object && !lastInsertSameKindElement)
                            {
                                continue;
                            }
                            if (lastInsertSameKindElement && (ett == EndTransitionType::InArrayNil || ett == EndTransitionType::InArrayNonNil || ett == EndTransitionType::EndOfArray))
                            {
                                continue;
                            }

                            HeapPtr<TableObject> obj = TableObject::CreateEmptyTableObject(vm, initStructure, initButterflyCap);

                            for (uint32_t i = 0; i < numNamedProps; i++)
                            {
                                PutByIdICInfo icInfo;
                                TableObject::PreparePutById(obj, strings[i], icInfo /*out*/);
                                TableObject::PutById(obj, strings[i].As<void>(), TValue::CreateInt32(static_cast<int32_t>(54321 + i)), icInfo);
                            }

                            constexpr size_t x_validateLen = 30;
                            TValue expected[x_validateLen];
                            for (size_t i = 0; i < x_validateLen; i++) { expected[i] = TValue::Nil(); }

                            auto validateEverything = [&](size_t expectedContinuousLen)
                            {
                                ArrayType arrType = TCGet(obj->m_arrayType);
                                SystemHeapPointer<void> hiddenClass = TCGet(obj->m_hiddenClass);
                                if (hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure)
                                {
                                    ReleaseAssert(arrType.m_asValue == hiddenClass.As<Structure>()->m_arrayType.m_asValue);
                                }
                                ReleaseAssert(arrType.IsContinuous());
                                ReleaseAssert(!arrType.HasSparseMap());
                                ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                if (baseType == PutType::Int32)
                                {
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Int32);
                                }
                                else if (baseType == PutType::Double)
                                {
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Double);
                                }
                                else
                                {
                                    ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                                }

                                ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous == static_cast<int32_t>(expectedContinuousLen));
                                ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayStorageCapacity + ArrayGrowthPolicy::x_arrayBaseOrd >= static_cast<int32_t>(expectedContinuousLen));
                                for (size_t i = 0; i < x_validateLen; i++)
                                {
                                    GetByIntegerIndexICInfo icInfo;
                                    TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
                                    TValue result = TableObject::GetByIntegerIndex(obj, static_cast<int64_t>(i), icInfo);
                                    ReleaseAssert(result.m_value == expected[i].m_value);
                                }

                                for (uint32_t i = 0; i < numNamedProps; i++)
                                {
                                    GetByIdICInfo icInfo;
                                    TableObject::PrepareGetById(obj, strings[i], icInfo /*out*/);
                                    TValue result = TableObject::GetById(obj, strings[i].As<void>(), icInfo);
                                    ReleaseAssert(result.IsInt32());
                                    ReleaseAssert(result.AsInt32() == static_cast<int32_t>(54321 + i));
                                }
                            };

                            constexpr size_t x_writeLen = 20;
                            for (size_t i = 1; i <= x_writeLen; i++)
                            {
                                // insert a new value at the end
                                //
                                TValue val = getValueForInsert();
                                TableObject::RawPutByValIntegerIndex(obj, static_cast<int64_t>(i), val);
                                expected[i] = val;

                                validateEverything(i + 1);

                                // overwrite a random value inside the array range
                                // the resulted array should still be continuous
                                //
                                val = getValueForInsert();
                                int64_t idx = rand() % static_cast<int64_t>(i) + 1;
                                TableObject::RawPutByValIntegerIndex(obj, idx, val);
                                expected[idx] = val;

                                validateEverything(i + 1);
                            }

                            // Put some nils outside array range, array should still be continuous
                            //
                            {
                                TValue val = TValue::Nil();
                                TableObject::RawPutByValIntegerIndex(obj, 123, val);
                                validateEverything(x_writeLen + 1);
                            }

                            // Replace the last element to nil, array should still be continuous with length one shorter
                            //
                            {
                                TValue val = TValue::Nil();
                                TableObject::RawPutByValIntegerIndex(obj, x_writeLen, val);
                                expected[x_writeLen] = val;
                                validateEverything(x_writeLen);

                                TableObject::RawPutByValIntegerIndex(obj, x_writeLen - 1, val);
                                expected[x_writeLen - 1] = val;
                                validateEverything(x_writeLen - 1);
                            }

                            // Do the end transition to break continuity, and validate everything is as expected
                            //
                            TValue lastInsertElement;
                            ArrayType::Kind expectNewKind;
                            int64_t indexToPut;
                            bool expectContinuous;
                            int32_t expectedContinuousLen = -1;
                            if (baseType == PutType::Int32)
                            {
                                expectNewKind = ArrayType::Kind::Int32;
                            }
                            else if (baseType == PutType::Double)
                            {
                                expectNewKind = ArrayType::Kind::Double;
                            }
                            else
                            {
                                expectNewKind = ArrayType::Kind::Any;
                            }

                            auto getDifferentKindElement = [&]() -> TValue
                            {
                                if (baseType == PutType::Int32)
                                {
                                    return TValue::CreateDouble(rand() / static_cast<double>(100000.0));
                                }
                                else if (baseType == PutType::Double)
                                {
                                    return TValue::CreateInt32(rand() % 10000);
                                }
                                else
                                {
                                    ReleaseAssert(false);
                                }
                            };

                            if (ett == EndTransitionType::InArrayNil)
                            {
                                lastInsertElement = TValue::Nil();
                                indexToPut = rand() % static_cast<int64_t>(x_writeLen - 3) + 1;
                                expectContinuous = false;
                            }
                            else if (ett == EndTransitionType::InArrayNonNil)
                            {
                                ReleaseAssert(!lastInsertSameKindElement);
                                lastInsertElement = getDifferentKindElement();
                                expectNewKind = ArrayType::Kind::Any;
                                indexToPut = rand() % static_cast<int64_t>(x_writeLen - 2) + 1;
                                expectContinuous = true;
                                expectedContinuousLen = x_writeLen - 1;
                            }
                            else if (ett == EndTransitionType::EndOfArray)
                            {
                                ReleaseAssert(!lastInsertSameKindElement);
                                lastInsertElement = getDifferentKindElement();
                                expectNewKind = ArrayType::Kind::Any;
                                indexToPut = static_cast<int64_t>(x_writeLen - 1);
                                expectContinuous = true;
                                expectedContinuousLen = x_writeLen;
                            }
                            else if (ett == EndTransitionType::MakeNotContinuous)
                            {
                                if (lastInsertSameKindElement)
                                {
                                    lastInsertElement = getValueForInsert();
                                }
                                else
                                {
                                    lastInsertElement = getDifferentKindElement();
                                    expectNewKind = ArrayType::Kind::Any;
                                }
                                indexToPut = x_writeLen;
                                expectContinuous = false;
                            }
                            else if (ett == EndTransitionType::MakeNotContinuousAndOutOfRange)
                            {
                                if (lastInsertSameKindElement)
                                {
                                    lastInsertElement = getValueForInsert();
                                }
                                else
                                {
                                    lastInsertElement = getDifferentKindElement();
                                    expectNewKind = ArrayType::Kind::Any;
                                }
                                indexToPut = 500;
                                expectContinuous = false;
                            }
                            else if (ett == EndTransitionType::MakeNotContinuousAndVectorRangeSparseMap)
                            {
                                if (lastInsertSameKindElement)
                                {
                                    lastInsertElement = getValueForInsert();
                                    expectNewKind = ArrayType::Kind::Any;
                                }
                                else
                                {
                                    lastInsertElement = getDifferentKindElement();
                                    expectNewKind = ArrayType::Kind::Any;
                                }
                                indexToPut = ArrayGrowthPolicy::x_unconditionallySparseMapCutoff;
                                expectContinuous = false;
                            }
                            else if (ett == EndTransitionType::MakeNotContinuousAndSparseMap)
                            {
                                if (lastInsertSameKindElement)
                                {
                                    lastInsertElement = getValueForInsert();
                                    expectNewKind = ArrayType::Kind::Any;
                                }
                                else
                                {
                                    lastInsertElement = getDifferentKindElement();
                                    expectNewKind = ArrayType::Kind::Any;
                                }
                                indexToPut = ArrayGrowthPolicy::x_unconditionallySparseMapCutoff + 1;
                                expectContinuous = false;
                            }
                            else
                            {
                                ReleaseAssert(false);
                            }

                            TableObject::RawPutByValIntegerIndex(obj, indexToPut, lastInsertElement);
                            if (0 <= indexToPut && indexToPut <= static_cast<int64_t>(x_validateLen))
                            {
                                expected[indexToPut] = lastInsertElement;
                            }

                            ArrayType arrType = TCGet(obj->m_arrayType);
                            SystemHeapPointer<void> hiddenClass = TCGet(obj->m_hiddenClass);
                            if (hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure)
                            {
                                ReleaseAssert(arrType.m_asValue == hiddenClass.As<Structure>()->m_arrayType.m_asValue);
                            }
                            ReleaseAssert(arrType.IsContinuous() == expectContinuous);
                            if (ett == EndTransitionType::MakeNotContinuousAndSparseMap)
                            {
                                ReleaseAssert(arrType.HasSparseMap());
                                ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous < ArrayGrowthPolicy::x_arrayBaseOrd - 1);
                            }
                            else if (ett == EndTransitionType::MakeNotContinuousAndVectorRangeSparseMap)
                            {
                                ReleaseAssert(arrType.HasSparseMap());
                                ReleaseAssert(arrType.SparseMapContainsVectorIndex());
                                ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous < ArrayGrowthPolicy::x_arrayBaseOrd - 1);
                            }
                            else
                            {
                                ReleaseAssert(!arrType.HasSparseMap());
                                ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                                if (expectContinuous)
                                {
                                    ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous == expectedContinuousLen);
                                }
                                else
                                {
                                    ReleaseAssert(obj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous == ArrayGrowthPolicy::x_arrayBaseOrd - 1);
                                }
                            }
                            ReleaseAssert(arrType.ArrayKind() == expectNewKind);

                            for (size_t i = 0; i < x_validateLen; i++)
                            {
                                GetByIntegerIndexICInfo icInfo;
                                TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
                                TValue result = TableObject::GetByIntegerIndex(obj, static_cast<int64_t>(i), icInfo);
                                ReleaseAssert(result.m_value == expected[i].m_value);
                            }

                            {
                                GetByIntegerIndexICInfo icInfo;
                                TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
                                TValue result = TableObject::GetByIntegerIndex(obj, indexToPut, icInfo);
                                ReleaseAssert(result.m_value == lastInsertElement.m_value);
                            }

                            for (uint32_t i = 0; i < numNamedProps; i++)
                            {
                                GetByIdICInfo icInfo;
                                TableObject::PrepareGetById(obj, strings[i], icInfo /*out*/);
                                TValue result = TableObject::GetById(obj, strings[i].As<void>(), icInfo);
                                ReleaseAssert(result.IsInt32());
                                ReleaseAssert(result.AsInt32() == static_cast<int32_t>(54321 + i));
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST(ObjectArrayPart, RandomTest)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    constexpr uint32_t maxNumProps = 2000;
    StringList strings = GetStringList(vm, maxNumProps);

    for (uint32_t testCase = 0; testCase < 1000; testCase++)
    {
        uint32_t inlineCap = static_cast<uint32_t>(rand() % 5);
        uint32_t initialButterflyCap = static_cast<uint32_t>(rand() % 5);

        uint32_t initialNamedProps = static_cast<uint32_t>(rand() % 5);

        uint32_t initArrayContinuousLen;
        {
            int dice = rand() % 3;
            if (dice == 0)
            {
                initArrayContinuousLen = 0;
            }
            else if (dice == 1)
            {
                initArrayContinuousLen = static_cast<uint32_t>(rand() % 30);
            }
            else if (dice == 2)
            {
                initArrayContinuousLen = static_cast<uint32_t>(rand() % 2000);
            }
            else
            {
                ReleaseAssert(false);
            }
        }

        Structure* initStructure = Structure::CreateInitialStructure(vm, static_cast<uint8_t>(inlineCap));
        HeapPtr<TableObject> obj = TableObject::CreateEmptyTableObject(vm, initStructure, initialButterflyCap);

        std::unordered_map<int64_t, TValue> namedPropMap;
        std::unordered_map<double, TValue> arrayPropMap;
        std::vector<double> allPutArrayProperties;

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
                return TValue::CreatePointer(strings[static_cast<uint32_t>(rand()) % maxNumProps]);
            }
        };

        uint32_t totalTestOp;
        int namedPropDiceLimit;
        uint32_t namedPropSelectionLimit;
        if (rand() % 10 == 0)
        {
            totalTestOp = 1000;
            namedPropDiceLimit = 8;
            namedPropSelectionLimit = maxNumProps;
        }
        else
        {
            totalTestOp = 500;
            namedPropDiceLimit = 1;
            namedPropSelectionLimit = 50;
        }

        auto putRandomNamedProp = [&]()
        {
            uint32_t ord = static_cast<uint32_t>(rand()) % namedPropSelectionLimit;
            PutByIdICInfo icInfo;
            TableObject::PreparePutById(obj, strings[ord], icInfo /*out*/);
            TValue newVal = getRandomValue();
            TableObject::PutById(obj, strings[ord].As<void>(), newVal, icInfo);
            namedPropMap[strings[ord].m_value] = newVal;
        };

        int putPropsBeforeArray = rand() % 2;

        if (putPropsBeforeArray)
        {
            for (uint32_t i = 0; i < initialNamedProps; i++)
            {
                putRandomNamedProp();
            }
        }

        {
            int dice = rand() % 3;
            auto getValueToPut = [&]() -> TValue
            {
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
                    return getRandomValue();
                }
            };

            for (uint32_t i = 0; i < initArrayContinuousLen; i++)
            {
                int64_t index = static_cast<int64_t>(i) + ArrayGrowthPolicy::x_arrayBaseOrd;
                TValue val = getValueToPut();
                TableObject::RawPutByValIntegerIndex(obj, index, val);
                arrayPropMap[static_cast<double>(index)] = val;
                allPutArrayProperties.push_back(static_cast<double>(index));
            }
        }

        if (!putPropsBeforeArray)
        {
            for (uint32_t i = 0; i < initialNamedProps; i++)
            {
                putRandomNamedProp();
            }
        }

        auto putRandomArrayProp = [&]()
        {
            int dice = rand() % 10;
            if (dice == 0)
            {
                // put a random double-index prop
                //
                double index = rand() / static_cast<double>(1000.0);
                TValue val = getRandomValue();
                TableObject::RawPutByValDoubleIndex(obj, index, val);
                arrayPropMap[index] = val;
                allPutArrayProperties.push_back(index);
                return;
            }

            // put at integer index
            //
            int64_t index;
            if (dice == 1)
            {
                index = rand();
                if (rand() % 2 == 0) { index = -index; }
            }
            else if (dice < 7)
            {
                index = rand() % 30;
            }
            else
            {
                index = rand() % 10000;
            }
            TValue val = getRandomValue();
            TableObject::RawPutByValIntegerIndex(obj, index, val);
            arrayPropMap[static_cast<double>(index)] = val;
            allPutArrayProperties.push_back(static_cast<double>(index));
        };

        auto testRead = [&]()
        {
            int dice = rand() % 10;
            if (dice < namedPropDiceLimit || allPutArrayProperties.empty())
            {
                // read a named property
                //
                UserHeapPointer<HeapString> prop = strings[static_cast<uint32_t>(rand()) % namedPropSelectionLimit];
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(obj, prop, icInfo /*out*/);
                TValue result = TableObject::GetById(obj, prop.As<void>(), icInfo);
                if (namedPropMap.count(prop.m_value))
                {
                    ReleaseAssert(result.m_value == namedPropMap[prop.m_value].m_value);
                }
                else
                {
                    ReleaseAssert(result.IsNil());
                }
            }
            else
            {
                // read an array property
                //
                double index;
                if (dice == 1)
                {
                    index = rand() / static_cast<double>(1000.0);
                }
                else
                {
                    index = allPutArrayProperties[static_cast<size_t>(rand()) % allPutArrayProperties.size()];
                }
                GetByIntegerIndexICInfo icInfo;
                TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
                TValue result = TableObject::GetByDoubleVal(obj, index, icInfo);
                if (arrayPropMap.count(index))
                {
                    ReleaseAssert(result.m_value == arrayPropMap[index].m_value);
                }
                else
                {
                    ReleaseAssert(result.IsNil());
                }
            }
        };

        auto doWrite = [&]()
        {
            int dice = rand() % 10;
            if (dice < namedPropDiceLimit)
            {
                putRandomNamedProp();
            }
            else
            {
                putRandomArrayProp();
            }
        };

        for (uint32_t testOp = 0; testOp < totalTestOp; testOp++)
        {
            testRead();
            doWrite();

            ArrayType arrType = TCGet(obj->m_arrayType);
            SystemHeapPointer<void> hiddenClass = TCGet(obj->m_hiddenClass);
            if (hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure)
            {
                ReleaseAssert(arrType.m_asValue == hiddenClass.As<Structure>()->m_arrayType.m_asValue);
            }
        }
    }
}

// Test the density check in our array policy is working
//
void ObjectArrayPartDensityTest(VM* vm, uint32_t numProps)
{
    StringList strings = GetStringList(vm, numProps);

    Structure* initStructure = Structure::CreateInitialStructure(vm, 2 /*inlineCap*/);
    for (uint32_t initButterflyCap : { 0U, 8U })
    {
        for (int gap : { 3, 9 })    // this value is determined based on ArrayGrowthPolicy::x_densityCutoff
        {
            enum PutType
            {
                Int32,
                Double,
                MixInt32Double,
                Object
            };

            for (PutType baseTy :  { PutType::Int32, PutType::Double, PutType::MixInt32Double, PutType::Object })
            {
                for (bool putNamedPropsBeforeArray : { false, true })
                {
                    HeapPtr<TableObject> obj = TableObject::CreateEmptyTableObject(vm, initStructure, initButterflyCap);

                    if (putNamedPropsBeforeArray)
                    {
                        for (uint32_t i = 0; i < numProps; i++)
                        {
                            PutByIdICInfo icInfo;
                            TableObject::PreparePutById(obj, strings[i], icInfo /*out*/);
                            TableObject::PutById(obj, strings[i].As<void>(), TValue::CreateInt32(static_cast<int32_t>(54321 + i)), icInfo);
                        }
                    }

                    auto getValueToPut = [&]()
                    {
                        int state = 0;
                        return [=]() mutable -> TValue
                        {
                            if (baseTy == PutType::Object)
                            {
                                return TValue::CreatePointer(strings[static_cast<uint32_t>(rand()) % numProps]);
                            }
                            if (baseTy == PutType::Int32 || (baseTy == PutType::MixInt32Double && state == 0))
                            {
                                state = 1;
                                return TValue::CreateInt32(rand());
                            }
                            else if (baseTy == PutType::Double || (baseTy == PutType::MixInt32Double && state == 1))
                            {
                                state = 0;
                                return TValue::CreateDouble(rand() / static_cast<double>(1000.0));
                            }
                            ReleaseAssert(false);
                        };
                    }();

                    std::vector<TValue> expected;

                    int multUplimit;
                    if (gap == 3)
                    {
                        // If the density is high, the array should be able to grow to up to x_sparseMapUnlessContinuousCutoff without becoming sparse map
                        //
                        multUplimit = ArrayGrowthPolicy::x_sparseMapUnlessContinuousCutoff / gap;
                    }
                    else
                    {
                        // If the density is low, the array is guaranteed to become sparse map at x_alwaysVectorCutoff * x_vectorGrowthFactor
                        //
                        multUplimit = static_cast<int>(ArrayGrowthPolicy::x_alwaysVectorCutoff * ArrayGrowthPolicy::x_vectorGrowthFactor / gap + 10);
                    }

                    expected.resize(static_cast<size_t>(multUplimit * gap + 10));
                    for (size_t i = 0; i < expected.size(); i++)
                    {
                        expected[i] = TValue::Nil();
                    }

                    for (int mult = 1; mult <= multUplimit; mult++)
                    {
                        int32_t index = gap * mult;
                        TValue val = getValueToPut();
                        TableObject::RawPutByValIntegerIndex(obj, index, val);
                        ReleaseAssert(static_cast<size_t>(index) < expected.size());
                        expected[static_cast<size_t>(index)] = val;
                    }

                    auto checkArrayType = [&]()
                    {
                        ArrayType arrType = TCGet(obj->m_arrayType);
                        SystemHeapPointer<void> hiddenClass = TCGet(obj->m_hiddenClass);
                        if (hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure)
                        {
                            ReleaseAssert(arrType.m_asValue == hiddenClass.As<Structure>()->m_arrayType.m_asValue);
                        }

                        if (gap == 3)
                        {
                            ReleaseAssert(!arrType.IsContinuous());
                            if (baseTy == PutType::Int32)
                            {
                                ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Int32);
                            }
                            else if (baseTy == PutType::Double)
                            {
                                ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Double);
                            }
                            else
                            {
                                ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                            }
                            ReleaseAssert(!arrType.HasSparseMap());
                            ReleaseAssert(!arrType.SparseMapContainsVectorIndex());
                        }
                        else
                        {
                            ReleaseAssert(!arrType.IsContinuous());
                            ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                            ReleaseAssert(arrType.HasSparseMap());
                            ReleaseAssert(arrType.SparseMapContainsVectorIndex());
                        }
                    };

                    checkArrayType();

                    if (!putNamedPropsBeforeArray)
                    {
                        for (uint32_t i = 0; i < numProps; i++)
                        {
                            PutByIdICInfo icInfo;
                            TableObject::PreparePutById(obj, strings[i], icInfo /*out*/);
                            TableObject::PutById(obj, strings[i].As<void>(), TValue::CreateInt32(static_cast<int32_t>(54321 + i)), icInfo);
                        }
                    }

                    checkArrayType();

                    for (size_t i = 0; i < expected.size(); i++)
                    {
                        GetByIntegerIndexICInfo icInfo;
                        TableObject::PrepareGetByIntegerIndex(obj, icInfo /*out*/);
                        TValue result = TableObject::GetByInt32Val(obj, static_cast<int32_t>(i), icInfo);
                        ReleaseAssert(result.m_value == expected[i].m_value);
                    }

                    for (uint32_t i = 0; i < numProps; i++)
                    {
                        GetByIdICInfo icInfo;
                        TableObject::PrepareGetById(obj, strings[i], icInfo /*out*/);
                        TValue result = TableObject::GetById(obj, strings[i].As<void>(), icInfo);
                        ReleaseAssert(result.IsInt32());
                        ReleaseAssert(result.AsInt32() == static_cast<int32_t>(54321 + i));
                    }

                    if (gap == 3)
                    {
                        int32_t index = static_cast<int32_t>(ArrayGrowthPolicy::x_sparseMapUnlessContinuousCutoff * ArrayGrowthPolicy::x_vectorGrowthFactor + 10);
                        TValue val = getValueToPut();
                        TableObject::RawPutByValIntegerIndex(obj, index, val);

                        ArrayType arrType = TCGet(obj->m_arrayType);
                        SystemHeapPointer<void> hiddenClass = TCGet(obj->m_hiddenClass);
                        if (hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure)
                        {
                            ReleaseAssert(arrType.m_asValue == hiddenClass.As<Structure>()->m_arrayType.m_asValue);
                        }
                        ReleaseAssert(!arrType.IsContinuous());
                        ReleaseAssert(arrType.ArrayKind() == ArrayType::Kind::Any);
                        ReleaseAssert(arrType.HasSparseMap());
                        ReleaseAssert(arrType.SparseMapContainsVectorIndex());
                    }
                }
            }
        }
    }
}

TEST(ObjectArrayPart, DensityTest1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    ObjectArrayPartDensityTest(vm, 4 /*numProps*/);
}

TEST(ObjectArrayPart, DensityTest2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    ObjectArrayPartDensityTest(vm, 2000 /*numProps*/);
}

}   // anonymous namespace

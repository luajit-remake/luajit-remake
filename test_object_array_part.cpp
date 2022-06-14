#include "bytecode.h"
#include "gtest/gtest.h"

using namespace ToyLang;

namespace {

// Test the transition on NoButterflyPart, that is, test the array type is
// as expected when we put the first element into the array part
//
TEST(ObjectArrayPart, PutFirstElement)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    constexpr uint32_t numPropsToAdd = 4;
    UserHeapPointer<HeapString> strings[numPropsToAdd];
    for (uint32_t i = 0; i < numPropsToAdd; i++)
    {
        std::string s = "";
        s = s + static_cast<char>('a' + i);
        strings[i] = vm->CreateStringObjectFromRawString(s.c_str(), static_cast<uint32_t>(s.length()));
    }

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
                                TableObject::PutById(obj, strings[i], TValue::CreateInt32(static_cast<int32_t>(12345 + i), TValue::x_int32Tag), icInfo);
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
                                value = TValue::CreateInt32(6789, TValue::x_int32Tag);
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
                                TValue result = TableObject::GetById(obj, strings[i], icInfo);
                                ReleaseAssert(result.IsInt32(TValue::x_int32Tag));
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
                                TValue result = TableObject::GetById(obj, strings[i], icInfo);
                                ReleaseAssert(result.IsInt32(TValue::x_int32Tag));
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

    constexpr uint32_t numPropsToAdd = 4;
    constexpr size_t numAllString = 100;
    UserHeapPointer<HeapString> strings[numAllString];
    for (uint32_t i = 0; i < 100; i++)
    {
        std::string s = "";
        s = s + static_cast<char>('a' + i);
        strings[i] = vm->CreateStringObjectFromRawString(s.c_str(), static_cast<uint32_t>(s.length()));
    }

    for (uint32_t inlineCap : { 0U, numPropsToAdd / 2, numPropsToAdd })
    {
        Structure* initStructure = Structure::CreateInitialStructure(vm, static_cast<uint8_t>(inlineCap));
        for (uint32_t initButterflyCap : { 0U, 1U, 2U, 4U })
        {
            for (uint32_t numNamedProps : { 0U, numPropsToAdd })
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
                            return TValue::CreateInt32(rand() % 10000, TValue::x_int32Tag);
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
                                TableObject::PutById(obj, strings[i], TValue::CreateInt32(static_cast<int32_t>(54321 + i), TValue::x_int32Tag), icInfo);
                            }

                            constexpr size_t x_validateLen = 30;
                            TValue expected[x_validateLen];
                            for (size_t i = 0; i < x_validateLen; i++) { expected[i] = TValue::Nil(); }

                            auto validateEverything = [&](size_t expectedContinuousLen)
                            {
                                ArrayType arrType = TCGet(obj->m_arrayType);
                                ReleaseAssert(arrType.m_asValue == TCGet(obj->m_hiddenClass).As<Structure>()->m_arrayType.m_asValue);
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
                                    TValue result = TableObject::GetById(obj, strings[i], icInfo);
                                    ReleaseAssert(result.IsInt32(TValue::x_int32Tag));
                                    ReleaseAssert(result.AsInt32() == static_cast<int32_t>(54321 + i));
                                }
                            };

                            constexpr size_t x_writeLen = 20;
                            for (size_t i = 1; i <= x_writeLen; i++)
                            {
                                // insert a new value at the end
                                //
                                TValue val = getValueForInsert();
                                TableObject::PutByValIntegerIndex(obj, static_cast<int64_t>(i), val);
                                expected[i] = val;

                                validateEverything(i + 1);

                                // overwrite a random value inside the array range
                                // the resulted array should still be continuous
                                //
                                val = getValueForInsert();
                                int64_t idx = rand() % static_cast<int64_t>(i) + 1;
                                TableObject::PutByValIntegerIndex(obj, idx, val);
                                expected[idx] = val;

                                validateEverything(i + 1);
                            }

                            // Put some nils outside array range, array should still be continuous
                            //
                            {
                                TValue val = TValue::Nil();
                                TableObject::PutByValIntegerIndex(obj, 123, val);
                                validateEverything(x_writeLen + 1);
                            }

                            // Replace the last element to nil, array should still be continuous with length one shorter
                            //
                            {
                                TValue val = TValue::Nil();
                                TableObject::PutByValIntegerIndex(obj, x_writeLen, val);
                                expected[x_writeLen] = val;
                                validateEverything(x_writeLen);

                                TableObject::PutByValIntegerIndex(obj, x_writeLen - 1, val);
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
                                    return TValue::CreateInt32(rand() % 10000, TValue::x_int32Tag);
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

                            TableObject::PutByValIntegerIndex(obj, indexToPut, lastInsertElement);
                            if (0 <= indexToPut && indexToPut <= static_cast<int64_t>(x_validateLen))
                            {
                                expected[indexToPut] = lastInsertElement;
                            }

                            ArrayType arrType = TCGet(obj->m_arrayType);
                            ReleaseAssert(arrType.m_asValue == TCGet(obj->m_hiddenClass).As<Structure>()->m_arrayType.m_asValue);
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
                                TValue result = TableObject::GetById(obj, strings[i], icInfo);
                                ReleaseAssert(result.IsInt32(TValue::x_int32Tag));
                                ReleaseAssert(result.AsInt32() == static_cast<int32_t>(54321 + i));
                            }
                        }
                    }
                }

            }
        }
    }
}

// TODO: density check test
// TODO: random test

}   // anonymous namespace

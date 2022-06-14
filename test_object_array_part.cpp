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
                            for (int64_t readLocation : { -1LL, 0LL, 1LL, 2LL, ArrayGrowthPolicy::x_alwaysVectorCutoff * 2LL, 50000000000LL})
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
                            for (int64_t readLocation : { -1LL, 0LL, 1LL, 2LL, ArrayGrowthPolicy::x_alwaysVectorCutoff * 2LL, ArrayGrowthPolicy::x_alwaysVectorCutoff / 2LL, 50000000000LL})
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


}   // anonymous namespace

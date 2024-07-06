#pragma once

#include "common_utils.h"
#include "heap_ptr_utils.h"

#define HOI_SYS_HEAP 1
#define HOI_USR_HEAP 2

#define LANGUAGE_EXPOSED_HEAP_OBJECT_INFO_LIST                                          \
  /* Enum Name                      C++ name                        Lives in       */   \
    (String,                        HeapString,                     HOI_USR_HEAP)       \
  , (Function,                      FunctionObject,                 HOI_USR_HEAP)       \
  , (Userdata,                      HeapCDataObject,                HOI_USR_HEAP)       \
  , (Thread,                        CoroutineRuntimeContext,        HOI_USR_HEAP)       \
  , (Table,                         TableObject,                    HOI_USR_HEAP)       \

#define HEAP_OBJECT_INFO_LIST                                                           \
  /* Enum Name                      C++ name                        Lives in       */   \
    LANGUAGE_EXPOSED_HEAP_OBJECT_INFO_LIST                                              \
  , (ArraySparseMap,                ArraySparseMap,                 HOI_USR_HEAP)       \
  , (Upvalue,                       Upvalue,                        HOI_USR_HEAP)       \
  , (UnlinkedCodeBlock,             UnlinkedCodeBlock,              HOI_SYS_HEAP)       \
  , (ExecutableCode,                ExecutableCode,                 HOI_SYS_HEAP)       \
  , (Structure,                     Structure,                      HOI_SYS_HEAP)       \
  , (StructureAnchorHashTable,      StructureAnchorHashTable,       HOI_SYS_HEAP)       \
  , (CacheableDictionary,           CacheableDictionary,            HOI_SYS_HEAP)       \
  , (UncacheableDictionary,         UncacheableDictionary,          HOI_SYS_HEAP)       \

#define HOI_ENUM_NAME(hoi) PP_TUPLE_GET_1(hoi)
#define HOI_CLASS_NAME(hoi) PP_TUPLE_GET_2(hoi)
#define HOI_HEAP_KIND(hoi) (PP_TUPLE_GET_3(hoi))

// Forward declare all the classes
//
#define macro(hoi) class HOI_CLASS_NAME(hoi);
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

enum class HeapEntityType : uint8_t
{
    // Declare enum for heap object types
    //
#define macro(hoi) HOI_ENUM_NAME(hoi),
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

    X_END_OF_ENUM
};

template<typename T>
struct TypeEnumForHeapObjectImpl
{
    static constexpr HeapEntityType value = HeapEntityType::X_END_OF_ENUM;
};

#define macro(hoi)                                                                      \
    template<> struct TypeEnumForHeapObjectImpl<HOI_CLASS_NAME(hoi)> {                  \
        static constexpr HeapEntityType value = HeapEntityType::HOI_ENUM_NAME(hoi);     \
    };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

// Type mapping from class name to enum name
//
template<typename T>
constexpr HeapEntityType TypeEnumForHeapObject = TypeEnumForHeapObjectImpl<T>::value;

template<typename T>
constexpr bool IsHeapObjectType = (TypeEnumForHeapObject<T> != HeapEntityType::X_END_OF_ENUM);

template<HeapEntityType ty>
struct HeapObjectTypeForEnumImpl;

#define macro(hoi)                                                                      \
    template<> struct HeapObjectTypeForEnumImpl<HeapEntityType::HOI_ENUM_NAME(hoi)> {   \
        using type = HOI_CLASS_NAME(hoi);                                               \
    };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

// Type mapping from class name to enum name
//
template<HeapEntityType ty>
using HeapObjectTypeForEnum = typename HeapObjectTypeForEnumImpl<ty>::type;

// FIXME
template<typename T>
struct TypeMayLiveInSystemHeapImpl : std::true_type { };

#define macro(hoi)                                                                      \
    template<> struct TypeMayLiveInSystemHeapImpl<HOI_CLASS_NAME(hoi)>                  \
        : std::integral_constant<bool, ((HOI_HEAP_KIND(hoi) & HOI_SYS_HEAP) > 0)> { };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

template<typename T>
constexpr bool TypeMayLiveInSystemHeap = TypeMayLiveInSystemHeapImpl<T>::value;

// FIXME
template<typename T>
struct TypeMayLiveInUserHeapImpl : std::true_type { };

#define macro(hoi)                                                                      \
    template<> struct TypeMayLiveInUserHeapImpl<HOI_CLASS_NAME(hoi)>                    \
        : std::integral_constant<bool, ((HOI_HEAP_KIND(hoi) & HOI_USR_HEAP) > 0)> { };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

template<typename T>
constexpr bool TypeMayLiveInUserHeap = TypeMayLiveInUserHeapImpl<T>::value;

enum class GcCellState: uint8_t
{
    Black = 0,
    White = 1
};
static constexpr GcCellState x_defaultCellState = GcCellState::White;

// All objects in the user heap must share the following 8-byte header:
//   SystemHeapPtr<void> m_hiddenClass
//   uint8_t m_type
//   uint8_t m_cellState    // reserved for GC
//   uint8_t m_opaque
//   ArrayType m_arrayType
//
// Note that m_hiddenClass must encompass at least the information of m_type (that is,
// different m_type must correspond to different m_hiddenClass)
//
// One can think of m_hiddenClass as what determines everything needed to interpret an
// object's bit representation. 'm_type' (and potentially info stored in 'm_opaque') are
// only caches of information in m_hiddenClass, so that accessing them doesn't need a dereference.
//
// Since 'm_hiddenClass' usually have different pointer types for different kinds of objects,
// and different kinds of objects also use m_opaque differently, this class is not inherited by
// the object classes. Instead, the object classes are responsible to make sure that the layout matches.
//
// Similar to 'm_hiddenClass', for all non-table objects, 'm_arrayType' must be x_invalidArrayType,
// so inline cache can cache on its field value directly, knowing that if the field value matches then
// the object must be a table.
//
class UserHeapGcObjectHeader
{
public:
    uint32_t m_hiddenClass;
    HeapEntityType m_type;
    GcCellState m_cellState;     // reserved for GC
    uint8_t m_opaque;
    uint8_t m_arrayType;

    // Does not populate 'm_opaque' or 'm_arrayType'
    //
    template<typename T>
    static void Populate(T self)
    {
        using RawType = std::remove_pointer_t<T>;
        static_assert(IsHeapObjectType<RawType>);
        static_assert(offsetof_member_v<&RawType::m_type> == offsetof_member_v<&UserHeapGcObjectHeader::m_type>);
        static_assert(offsetof_member_v<&RawType::m_cellState> == offsetof_member_v<&UserHeapGcObjectHeader::m_cellState>);
        self->m_type = TypeEnumForHeapObject<RawType>;
        self->m_cellState = GcCellState::White;
    }
};
static_assert(sizeof(UserHeapGcObjectHeader) == 8);
static_assert(sizeof(GcCellState) == 1 && offsetof_member_v<&UserHeapGcObjectHeader::m_cellState> == 5);

class SystemHeapGcObjectHeader
{
public:
    HeapEntityType m_type;
    GcCellState m_cellState;     // reserved for GC

    template<typename T>
    static void Populate(T* self)
    {
        static_assert(IsHeapObjectType<T>);
        static_assert(std::is_base_of_v<SystemHeapGcObjectHeader, T>);
        static_assert(TypeMayLiveInSystemHeap<T>);
        self->m_type = TypeEnumForHeapObject<T>;
        self->m_cellState = GcCellState::White;
    }
};

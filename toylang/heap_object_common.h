#pragma once

#include "common_utils.h"
#include "heap_ptr_utils.h"

namespace ToyLang
{

using namespace CommonUtils;

#define HOI_SYS_HEAP 1
#define HOI_USR_HEAP 2
#define HEAP_OBJECT_INFO_LIST                                                           \
  /* Enum Name                      C++ name                        Lives in       */   \
    (STRING,                        HeapString,                     HOI_USR_HEAP)       \
  , (FUNCTION,                      FunctionObject,                 HOI_USR_HEAP)       \
  , (USERDATA,                      HeapCDataObject,                HOI_USR_HEAP)       \
  , (THREAD,                        CoroutineRuntimeContext,        HOI_USR_HEAP)       \
  , (TABLE,                         TableObject,                    HOI_USR_HEAP)       \
  , (Structure,                     Structure,                      HOI_SYS_HEAP)       \
  , (DictionaryHiddenClass,         DictionaryHiddenClass,          HOI_SYS_HEAP)       \
  , (HiddenClassAnchorHashTable,    StructureAnchorHashTable,       HOI_SYS_HEAP)

#define HOI_ENUM_NAME(hoi) PP_TUPLE_GET_1(hoi)
#define HOI_CLASS_NAME(hoi) PP_TUPLE_GET_2(hoi)
#define HOI_HEAP_KIND(hoi) (PP_TUPLE_GET_3(hoi))

// Forward declare all the classes
//
#define macro(hoi) class HOI_CLASS_NAME(hoi);
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

enum class Type : uint8_t
{
    NIL,
    BOOLEAN,
    DOUBLE,

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
    static constexpr Type value = Type::X_END_OF_ENUM;
};

#define macro(hoi)                                                     \
    template<> struct TypeEnumForHeapObjectImpl<HOI_CLASS_NAME(hoi)> { \
        static constexpr Type value = Type::HOI_ENUM_NAME(hoi);        \
    };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

// Type mapping from class name to enum name
//
template<typename T>
constexpr Type TypeEnumForHeapObject = TypeEnumForHeapObjectImpl<T>::value;

template<typename T>
constexpr bool IsHeapObjectType = (TypeEnumForHeapObject<T> != Type::X_END_OF_ENUM);

template<typename T>
struct TypeMayLiveInSystemHeapImpl : std::true_type { };

#define macro(hoi)                                                                      \
    template<> struct TypeMayLiveInSystemHeapImpl<HOI_CLASS_NAME(hoi)>                  \
        : std::integral_constant<bool, ((HOI_HEAP_KIND(hoi) & HOI_SYS_HEAP) > 0)> { };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

template<typename T>
constexpr bool TypeMayLiveInSystemHeap = TypeMayLiveInSystemHeapImpl<T>::value;

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
    Grey = 1,
    White = 2
};
static constexpr GcCellState x_defaultCellState = GcCellState::White;

// All objects in the user heap must share the following 8-byte header:
//   SystemHeapPtr<void> m_structure
//   uint8_t m_type
//   uint8_t m_cellState    // reserved for GC
//   uint16_t m_opaque
//
// Note that m_structure must encompass at least the information of m_type (that is,
// different m_type must correspond to different m_structure)
//
// One can think of m_structure as what determines everything needed to interpret an
// object's bit representation. 'm_type' (and potentially info stored in 'm_opaque') are
// only caches of information in m_structure, so that accessing them doesn't need a dereference.
//
// Since 'm_structure' usually have different pointer types for different kinds of objects,
// and different kinds of objects also use m_opaque differently, this class is not inherited by
// the object classes. Instead, the object classes are responsible to make sure that the layout matches.
//
class UserHeapGcObjectHeader
{
public:
    uint32_t m_structure;
    Type m_type;
    GcCellState m_cellState;     // reserved for GC

    template<typename T>
    static void Populate(T self)
    {
        using RawTypePtr = remove_heap_ptr_t<T>;
        static_assert(std::is_pointer_v<RawTypePtr>);
        using RawType = std::remove_pointer_t<RawTypePtr>;
        static_assert(IsHeapObjectType<RawType>);
        self->m_type = TypeEnumForHeapObject<RawType>;
        self->m_cellState = GcCellState::White;
    }
};
static_assert(sizeof(UserHeapGcObjectHeader) == 8);
static_assert(sizeof(GcCellState) == 1 && offsetof_member_v<&UserHeapGcObjectHeader::m_cellState> == 5);

class SystemHeapGcObjectHeader
{
public:
    Type m_type;
    GcCellState m_cellState;     // reserved for GC

    template<typename T>
    static void Populate(T self)
    {
        using RawTypePtr = remove_heap_ptr_t<T>;
        static_assert(std::is_pointer_v<RawTypePtr>);
        using RawType = std::remove_pointer_t<RawTypePtr>;
        static_assert(IsHeapObjectType<RawType>);
        static_assert(std::is_base_of_v<SystemHeapGcObjectHeader, RawType>);
        static_assert(TypeMayLiveInSystemHeap<RawType>);
        self->m_type = TypeEnumForHeapObject<RawType>;
        self->m_cellState = GcCellState::White;
    }
};

}   // namespace ToyLang

#pragma once

#include "bytecode_builder_utils.h"
#include "runtime_utils.h"

#include "generated/all_bytecode_builder_apis.h"

namespace DeegenBytecodeBuilder {

class BytecodeBuilder;

#define macro(e) , e ## _BytecodeMetadataInfo
using BytecodeMetadataTypeListInfo = deduplicate_tuple<tuple_cat_t<std::tuple<> PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_CLASS_NAMES)>>;
#undef macro

#define macro(e) , public e<BytecodeBuilder>
class BytecodeBuilder final : public BytecodeBuilderBase<BytecodeMetadataTypeListInfo> PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_CLASS_NAMES) {
#undef macro

private:
    friend class DeegenInterpreterDispatchTableBuilder;

#define macro(e) friend class e<BytecodeBuilder>;
PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_CLASS_NAMES)
#undef macro

    template<typename T>
    static constexpr size_t GetBytecodeOpcodeBase()
    {
        size_t res = 0;
#define macro(e)                                            \
    if constexpr(std::is_same_v<T, e<BytecodeBuilder>>) {   \
        return res;                                         \
    } else {                                                \
        res += GetNumVariantsOfBytecode<e>();

        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_CLASS_NAMES)
#undef macro

        std::ignore = res;
        static_assert(type_dependent_false<T>::value, "bad type T!");
        return static_cast<size_t>(-1);

#define macro(e) }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_CLASS_NAMES)
#undef macro
    }

    template<template<typename> class T>
    static constexpr size_t GetNumVariantsOfBytecode()
    {
        static_assert(std::is_base_of_v<T<BytecodeBuilder>, BytecodeBuilder>);
        return T<BytecodeBuilder>::GetNumVariants();
    }
};

}   // namespace DeegenBytecodeBuilder

using BytecodeMetadataStructTypeList = DeegenBytecodeBuilder::BytecodeMetadataTypeListInfo::DeduplicatedTy;

constexpr size_t x_num_bytecode_metadata_struct_kinds = std::tuple_size_v<BytecodeMetadataStructTypeList>;

constexpr auto x_bytecode_metadata_struct_size_list = ComputeConstexprTraitArrayForTuple<uint32_t /*TraitType*/, BytecodeMetadataStructTypeList>(
    []<typename T>() -> uint32_t {
        return static_cast<uint32_t>(T::GetSize());
    });

constexpr auto x_bytecode_metadata_struct_log_2_alignment_list = ComputeConstexprTraitArrayForTuple<uint32_t /*TraitType*/, BytecodeMetadataStructTypeList>(
    []<typename T>() -> uint32_t {
        size_t alignment = T::GetAlignment();
        assert(is_power_of_2(alignment));
        uint32_t log2res = 0;
        while (alignment != 1)
        {
            log2res++;
            alignment /= 2;
        }
        return log2res;
    });

namespace detail {

template<typename Tuple>
struct ForEachBytecodeMetadataHelper
{
    template<size_t ord, typename Lambda>
    static ALWAYS_INLINE void Run(uintptr_t ptr, const uint16_t* cntArray, const Lambda& lambda)
    {
        if constexpr(ord < std::tuple_size_v<Tuple>)
        {
            using MetadataTy = std::tuple_element_t<ord, Tuple>;
            constexpr size_t alignment = MetadataTy::GetAlignment();
            uintptr_t mdAddr = (ptr + alignment - 1) / alignment * alignment;
            MetadataTy* md = MetadataTy::CastFromAddr(reinterpret_cast<void*>(mdAddr));
            MetadataTy* mdEnd = md + cntArray[ord];
            while (md < mdEnd)
            {
                lambda(md);
                md++;
            }
            Run<ord + 1>(reinterpret_cast<uintptr_t>(mdEnd), cntArray, lambda);
        }
    }
};

}   // namespace detail

// 'lambda' should be a generic lambda taking a T* parameter, where T will be an instantiation of DeegenMetadata
//
template<typename Lambda>
void ALWAYS_INLINE ForEachBytecodeMetadata(CodeBlock* cb, const Lambda& lambda)
{
    UnlinkedCodeBlock* ucb = cb->m_owner;
    uintptr_t mdStart = cb->GetBytecodeMetadataStart();
    detail::ForEachBytecodeMetadataHelper<BytecodeMetadataStructTypeList>::Run<0>(mdStart, ucb->m_bytecodeMetadataUseCounts, lambda);
}

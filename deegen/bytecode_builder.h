#pragma once

#include "bytecode_builder_utils.h"

#include "generated/all_bytecode_builder_apis.h"

namespace DeegenBytecodeBuilder {

#define macro(e) , public e<BytecodeBuilder>
class BytecodeBuilder final : public BytecodeBuilderBase PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_CLASS_NAMES) {
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

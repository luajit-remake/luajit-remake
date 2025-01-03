#include "gtest/gtest.h"

#include "misc_llvm_helper.h"

#include "read_file.h"
#include "runtime_utils.h"
#include "json_utils.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_stencil_runtime_constant_insertion_pass.h"
#include "deegen_stencil_lowering_pass.h"
#include "deegen_stencil_creator.h"
#include "deegen_jit_codegen_logic_creator.h"
#include "deegen_ast_inline_cache.h"
#include "lj_parser_wrapper.h"
#include "drt/baseline_jit_codegen_helper.h"
#include "deegen_function_entry_logic_creator.h"
#include "llvm_override_option.h"
#include "test_vm_utils.h"
#include "deegen_ast_make_call.h"
#include "llvm/IR/DIBuilder.h"
#include "deegen_parse_asm_text.h"
#include "deegen_call_inline_cache.h"
#include "deegen_stencil_inline_cache_extraction_pass.h"

using namespace dast;
using namespace llvm;

TEST(StencilCreator, DataSectionHandling_1)
{
    using namespace llvm::object;
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    DeegenStencil ds = DeegenStencil::ParseMainLogic(ctx, false /*isLastStencilInBytecode*/, ReadFileContentAsString("test_inputs/test_stencil_parser_1.o"));

    ReleaseAssert(ds.m_privateDataObject.m_bytes.size() == 64);
    ReleaseAssert(ds.m_privateDataObject.m_relocations.size() == 8);
    for (size_t i = 0; i < 8; i++)
    {
        ReleaseAssert(ds.m_privateDataObject.m_relocations[i].m_offset == 8 * i);
    }

    ReleaseAssert(ds.m_sharedDataObjs.size() == 3);
}

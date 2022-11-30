#include "gtest/gtest.h"

#include "misc_llvm_helper.h"

#include "read_file.h"
#include "runtime_utils.h"
#include "json_utils.h"
#include "deegen_bytecode_ir_components.h"

using namespace dast;
using namespace llvm;

TEST(BaselineJITGen, LoadBytecodeInfo)
{
    std::unique_ptr<LLVMContext> llvmCtxHolder(new LLVMContext);
    LLVMContext& ctx = *llvmCtxHolder.get();

    nlohmann::json j = nlohmann::json::parse(ReadFileContentAsString("test_expected_output/test_add_input.json"));
    BytecodeIrInfo b(ctx, j);
    ReleaseAssert(b.m_bytecodeDef->m_bytecodeName == "Add");
    ReleaseAssert(!b.m_bytecodeDef->m_hasConditionalBranchTarget);
    ReleaseAssert(b.m_bytecodeDef->m_hasOutputValue);
}

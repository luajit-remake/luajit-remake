#pragma once

// All intrinsics understood by the Deegen compiler
// DeclareAsIntrinsic() API tells Deegen that the semantics of the bytecode encompasses the specific intrinsic
// If a bytecode encompasses an intrinsic, one must report it for correctness.
//
#define DEEGEN_BYTECODE_INTRINSIC_LIST                                                                  \
    /* Create the closure based on this prototype, 'proto' should be the UnlinkedCodeBlock */           \
    (CreateClosure, proto)                                                                              \
                                                                                                        \
    /* Exit function with no return values */                                                           \
  , (FunctionReturn0)                                                                                   \
                                                                                                        \
    /* Exit function with return value local [start, start + length) */                                 \
  , (FunctionReturn, start, length)                                                                     \
                                                                                                        \
    /* Exit function with return value local [start, start + length) plus all the variadic results */   \
  , (FunctionReturnAppendingVarRet, start, length)                                                      \
                                                                                                        \
    /* Get immutable upvalue at ordinal 'ord' */                                                        \
  , (UpvalueGetImmutable, ord)                                                                          \
                                                                                                        \
    /* Get mutable upvalue at ordinal 'ord' */                                                          \
  , (UpvalueGetMutable, ord)                                                                            \
                                                                                                        \
    /* Store 'value' into upvalue at ordinal 'ord' */                                                   \
  , (UpvaluePut, ord, value)                                                                            \
                                                                                                        \
    /* Close upvalues for all local >= start, 'start' should be a BytecodeRangeBaseRO */                \
  , (UpvalueClose, start)                                                                               \
                                                                                                        \
    /* Store the first 'num' variadic arguments to 'base', appending nil as needed */                   \
  , (GetVarArgPrefix, num, dst)                                                                         \
                                                                                                        \
    /* Store all variadic arguments as variadic result */                                               \
  , (GetAllVarArgsAsVarRet)                                                                             \



// Copyright 2019 UCWeb Co., Ltd.
#ifndef V8_CODE_ASSEMBLE_H
#define V8_CODE_ASSEMBLE_H
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
struct CompilerState;
bool AssembleCode(Isolate* isolate, const CompilerState& state,
                  TurboAssembler* tasm,
                  SafepointTableBuilder* safepoint_builder,
                  int* handler_table_offset, Zone* zone);
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#endif  // V8_CODE_ASSEMBLE_H

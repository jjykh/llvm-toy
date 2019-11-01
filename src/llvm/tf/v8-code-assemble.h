// Copyright 2019 UCWeb Co., Ltd.
#ifndef V8_CODE_ASSEMBLE_H
#define V8_CODE_ASSEMBLE_H

namespace v8 {
namespace internal {
class SafepointTableBuilder;
class TurboAssembler;
namespace tf_llvm {
struct CompilerState;
bool AssembleCode(const CompilerState& state, TurboAssembler* tasm,
                  SafepointTableBuilder* safepoint_builder,
                  int* handler_table_offset, Zone* zone);
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#endif  // V8_CODE_ASSEMBLE_H

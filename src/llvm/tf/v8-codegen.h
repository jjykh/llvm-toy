// Copyright 2019 UCWeb Co., Ltd.

#ifndef V8_CODEGEN_H
#define V8_CODEGEN_H
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
struct CompilerState;
Handle<Code> GenerateCode(Isolate* isolate, const CompilerState& state);
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_H

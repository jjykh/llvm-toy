// Copyright 2019 UCWeb Co., Ltd.

#ifndef V8_PASS_MANAGER_H
#define V8_PASS_MANAGER_H
#include "src/objects/code.h"
namespace v8 {
namespace internal {
namespace compiler {
class Schedule;
class CallDescriptor;
}  // namespace compiler
namespace tf_llvm {
class V8PassManager {
 public:
  Handle<Code> Run(Isolate* isolate, compiler::Schedule*,
                   compiler::CallDescriptor*, const char* name, Code::Kind kind,
                   uint32_t stub_key, int32_t builtin_index);
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#endif  // V8_PASS_MANAGER_:

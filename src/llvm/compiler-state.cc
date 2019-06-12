// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/compiler-state.h"
#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {

CompilerState::CompilerState(const char* function_name)
    : stackMapsSection_(nullptr),
      exception_table_(nullptr),
      module_(nullptr),
      function_(nullptr),
      context_(nullptr),
      entryPoint_(nullptr),
      function_name_(function_name),
      code_kind_(0),
      prologue_kind_(PrologueKind::Unset),
      builtin_index_(0),
      stub_key_(0),
      needs_frame_(false) {
  context_ = LLVMContextCreate();
  module_ = LLVMModuleCreateWithNameInContext("main", context_);
#if 0
    LLVMSetTarget(module_, "x86_64-unknown-linux-gnu");
#else
  LLVMSetTarget(module_, "armv7-linux-android");
#endif
}

CompilerState::~CompilerState() { LLVMContextDispose(context_); }
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#include "src/llvm/compiler-state.h"

namespace v8 {
namespace internal {
namespace tf_llvm {

CompilerState::CompilerState(const char* moduleName)
    : stackMapsSection_(nullptr),
      module_(nullptr),
      function_(nullptr),
      context_(nullptr),
      entryPoint_(nullptr) {
  context_ = LLVMContextCreate();
  module_ = LLVMModuleCreateWithNameInContext("test", context_);
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

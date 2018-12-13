#ifndef ABBREVIATED_TYPES_H
#define ABBREVIATED_TYPES_H
#include "src/llvm/llvm-headers.h"
namespace v8 {
namespace internal {
namespace tf_llvm {
typedef LLVMAtomicOrdering LAtomicOrdering;
typedef LLVMBasicBlockRef LBasicBlock;
typedef LLVMBuilderRef LBuilder;
typedef LLVMCallConv LCallConv;
typedef LLVMContextRef LContext;
typedef LLVMIntPredicate LIntPredicate;
typedef LLVMLinkage LLinkage;
typedef LLVMModuleRef LModule;
typedef LLVMRealPredicate LRealPredicate;
typedef LLVMTypeRef LType;
typedef LLVMValueRef LValue;
typedef LLVMMemoryBufferRef LMemoryBuffer;
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // ABBREVIATED_TYPES_H

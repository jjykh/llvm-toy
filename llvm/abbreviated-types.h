#ifndef ABBREVIATED_TYPES_H
#define ABBREVIATED_TYPES_H
#include "llvm-headers.h"
namespace jit {
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
}  // namespace jit
#endif  // ABBREVIATED_TYPES_H

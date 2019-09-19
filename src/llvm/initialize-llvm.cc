// Copyright 2019 UCWeb Co., Ltd.

#include <assert.h>
#include <stdio.h>

#include <llvm-c/Support.h>
#include "src/llvm/initialize-llvm.h"
#include "src/llvm/llvm-headers.h"
#include "src/llvm/log.h"

namespace v8 {
namespace internal {
namespace tf_llvm {

static void llvmCrash(const char*) __attribute__((noreturn));

void llvmCrash(const char* reason) {
  fprintf(stderr, "LLVM fatal error: %s", reason);
  EMASSERT(false);
  __builtin_unreachable();
}

static void initializeAndGetLLVMAPI(void) {
  LLVMInstallFatalErrorHandler(llvmCrash);
  const char* options[] = {
    "v8 builtins compiler",
#if defined(FEATURE_USE_SAMPLE_PGO)
    "-sample-profile-file=../../v8/src/llvm/sample.prof",
#endif
    "-vectorize-loops",
    "-runtime-memory-check-threshold=16",
  };
  LLVMParseCommandLineOptions(sizeof(options) / sizeof(const char*), options,
                              nullptr);

  // You think you want to call LLVMInitializeNativeTarget()? Think again. This
  // presumes that LLVM was ./configured correctly, which won't be the case in
  // cross-compilation situations.
  LLVMLinkInMCJIT();
  LLVMInitializeARMTargetInfo();
  LLVMInitializeARMTarget();
  LLVMInitializeARMTargetMC();
  LLVMInitializeARMAsmPrinter();
  LLVMInitializeARMDisassembler();
  LLVMInitializeARMAsmParser();
}

void initLLVM(void) { initializeAndGetLLVMAPI(); }
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

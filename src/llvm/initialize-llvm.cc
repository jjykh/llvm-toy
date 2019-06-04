// Copyright 2019 UCWeb Co., Ltd.

#include <assert.h>
#include <stdio.h>

#include <llvm-c/Support.h>
#include "src/llvm/initialize-llvm.h"
#include "src/llvm/llvm-headers.h"
#include "src/llvm/log.h"
#if 0
#include <llvm/Support/CommandLine.h>
#endif

namespace v8 {
namespace internal {
namespace tf_llvm {
#if 0
template <typename... Args>
void initCommandLine(Args... args) {
  const char* theArgs[] = {args...};
  llvm::cl::ParseCommandLineOptions(sizeof(theArgs) / sizeof(const char*),
                                    theArgs);
}
#endif
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

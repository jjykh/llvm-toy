#include "InitializeLLVM.h"
#include "LLVMHeaders.h"
#include <assert.h>
#include <llvm/Support/CommandLine.h>
#include <stdio.h>

template <typename... Args>
void initCommandLine(Args... args)
{
    const char* theArgs[] = { args... };
    llvm::cl::ParseCommandLineOptions(sizeof(theArgs) / sizeof(const char*), theArgs);
}
static void llvmCrash(const char*) __attribute__((noreturn));

void llvmCrash(const char* reason)
{
    fprintf(stderr, "LLVM fatal error: %s", reason);
    assert(false);
    __builtin_unreachable();
}

static void initializeAndGetLLVMAPI(void)
{

    LLVMInstallFatalErrorHandler(llvmCrash);

    if (!LLVMStartMultithreaded()) {
        llvmCrash("Could not start LLVM multithreading");
    }

    LLVMLinkInMCJIT();

    // You think you want to call LLVMInitializeNativeTarget()? Think again. This presumes that
    // LLVM was ./configured correctly, which won't be the case in cross-compilation situations.

    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmPrinter();
    LLVMInitializeARMDisassembler();

    initCommandLine("-enable-patchpoint-liveness=true");
}

void initLLVM(void)
{
    initializeAndGetLLVMAPI();
}

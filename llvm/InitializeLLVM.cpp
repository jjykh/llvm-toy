#include "LLVMAPI.h"

template<typename... Args>
void initCommandLine(Args... args)
{
    const char* theArgs[] = { args... };
    llvm::cl::ParseCommandLineOptions(sizeof(theArgs) / sizeof(const char*), theArgs);
}

static LLVMAPI* initializeAndGetLLVMAPI(
    void (*callback)(const char*, ...),
    bool* enableFastISel)
{
    g_llvmTrapCallback = callback;
    
    LLVMInstallFatalErrorHandler(llvmCrash);

    if (!LLVMStartMultithreaded())
        callback("Could not start LLVM multithreading");
    
    LLVMLinkInMCJIT();
    
    // You think you want to call LLVMInitializeNativeTarget()? Think again. This presumes that
    // LLVM was ./configured correctly, which won't be the case in cross-compilation situations.
    
#if CPU(X86_64)
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86Disassembler();
#elif CPU(ARM64)
#else
    UNREACHABLE_FOR_PLATFORM();
#endif
    
#if LLVM_VERSION_MAJOR >= 4 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR >= 6)
    // It's OK to have fast ISel, if it was requested.
#else
    // We don't have enough support for fast ISel. Disable it.
    *enableFastISel = false;
#endif

    if (*enableFastISel)
        initCommandLine("-enable-misched=false", "-regalloc=basic");
    else
        initCommandLine("-enable-patchpoint-liveness=true");
    
    LLVMAPI* result = new LLVMAPI;
    
    // Initialize the whole thing to null.
    memset(result, 0, sizeof(*result));
    
#define LLVM_API_FUNCTION_ASSIGNMENT(returnType, name, signature) \
    result->name = LLVM##name;
    FOR_EACH_LLVM_API_FUNCTION(LLVM_API_FUNCTION_ASSIGNMENT);
#undef LLVM_API_FUNCTION_ASSIGNMENT
    
    return result;
}

static void constructor(void) __attribute__((constructor));

void constructor(void)
{
    llvm = initializeAndGetLLVMAPI();
}

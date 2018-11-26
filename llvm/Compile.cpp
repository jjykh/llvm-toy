#include <assert.h>
#include <string.h>
#include "log.h"
#include "CompilerState.h"
#include "Compile.h"
#define SECTION_NAME_PREFIX "."
#define SECTION_NAME(NAME) (SECTION_NAME_PREFIX NAME)

namespace jit {
typedef CompilerState State;

static inline size_t round_up(size_t s, unsigned alignment)
{
    return (s + alignment - 1) & ~(alignment - 1);
}

static uint8_t* mmAllocateCodeSection(
    void* opaqueState, uintptr_t size, unsigned alignment, unsigned, const char* sectionName)
{
    State& state = *static_cast<State*>(opaqueState);

    state.m_codeSectionList.push_back(jit::ByteBuffer());
    state.m_codeSectionNames.push_back(sectionName);

    jit::ByteBuffer& bb(state.m_codeSectionList.back());
    size_t additionSize = state.m_platformDesc.m_prologueSize;
    size += additionSize;
    bb.resize(size);
    assert((reinterpret_cast<uintptr_t>(bb.data()) & (alignment - 1)) == 0);

    return const_cast<uint8_t*>(bb.data() + additionSize);
}

static uint8_t* mmAllocateDataSection(
    void* opaqueState, uintptr_t size, unsigned alignment, unsigned,
    const char* sectionName, LLVMBool)
{
    State& state = *static_cast<State*>(opaqueState);

    state.m_dataSectionList.push_back(jit::ByteBuffer());
    state.m_dataSectionNames.push_back(sectionName);

    jit::ByteBuffer& bb(state.m_dataSectionList.back());
    bb.resize(size);
    assert((reinterpret_cast<uintptr_t>(bb.data()) & (alignment - 1)) == 0);
    if (!strcmp(sectionName, SECTION_NAME("llvm_stackmaps"))) {
        state.m_stackMapsSection = &bb;
    }

    return const_cast<uint8_t*>(bb.data());
}

static LLVMBool mmApplyPermissions(void*, char**)
{
    return false;
}

static void mmDestroy(void*)
{
}

void compile(State& state)
{
    LLVMMCJITCompilerOptions options;
    LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
    options.OptLevel = 2;
    LLVMExecutionEngineRef engine;
    char* error = 0;
    options.MCJMM = LLVMCreateSimpleMCJITMemoryManager(
        &state, mmAllocateCodeSection, mmAllocateDataSection, mmApplyPermissions, mmDestroy);
    if (LLVMCreateMCJITCompilerForModule(&engine, state.m_module, &options, sizeof(options), &error)) {
        LOGE("FATAL: Could not create LLVM execution engine: %s", error);
        assert(false);
    }
    LLVMModuleRef module = state.m_module;
    LLVMPassManagerRef functionPasses = 0;
    LLVMPassManagerRef modulePasses;
    LLVMTargetDataRef targetData = LLVMGetExecutionEngineTargetData(engine);
    char* stringRepOfTargetData = LLVMCopyStringRepOfTargetData(targetData);
    LLVMSetDataLayout(module, stringRepOfTargetData);
    free(stringRepOfTargetData);

    LLVMPassManagerBuilderRef passBuilder = LLVMPassManagerBuilderCreate();
    LLVMPassManagerBuilderSetOptLevel(passBuilder, 2);
    LLVMPassManagerBuilderUseInlinerWithThreshold(passBuilder, 275);
    LLVMPassManagerBuilderSetSizeLevel(passBuilder, 0);

    functionPasses = LLVMCreateFunctionPassManagerForModule(module);
    modulePasses = LLVMCreatePassManager();

    LLVMPassManagerBuilderPopulateFunctionPassManager(passBuilder, functionPasses);
    LLVMPassManagerBuilderPopulateModulePassManager(passBuilder, modulePasses);

    LLVMPassManagerBuilderDispose(passBuilder);

    LLVMInitializeFunctionPassManager(functionPasses);
    for (LLVMValueRef function = LLVMGetFirstFunction(module); function; function = LLVMGetNextFunction(function))
        LLVMRunFunctionPassManager(functionPasses, function);
    LLVMFinalizeFunctionPassManager(functionPasses);

    LLVMRunPassManager(modulePasses, module);
    state.m_entryPoint = reinterpret_cast<void*>(LLVMGetPointerToGlobal(engine, state.m_function));

    if (functionPasses)
        LLVMDisposePassManager(functionPasses);
    LLVMDisposePassManager(modulePasses);
    LLVMDisposeExecutionEngine(engine);
}
}

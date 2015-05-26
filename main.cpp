#include <assert.h>
#include <string.h>
#include "LLVMAPI.h"
#include "InitializeLLVM.h"
#include "CompilerState.h"
typedef jit::CompilerState State;
#define SECTION_NAME_PREFIX "."
#define SECTION_NAME(NAME) (SECTION_NAME_PREFIX NAME)

static uint8_t* mmAllocateCodeSection(
    void* opaqueState, uintptr_t size, unsigned alignment, unsigned, const char* sectionName)
{
    State& state = *static_cast<State*>(opaqueState);

    jit::ByteBuffer bb(size);
    bb.resize(size);
    assert((reinterpret_cast<uintptr_t>(bb.data()) & (alignment - 1)) == 0);

    state.m_codeSectionList.push_back(std::move(bb));
    state.m_codeSectionNames.push_back(sectionName);

    return const_cast<uint8_t*>(state.m_codeSectionList.back().data());
}

static uint8_t* mmAllocateDataSection(
    void* opaqueState, uintptr_t size, unsigned alignment, unsigned,
    const char* sectionName, LLVMBool)
{
    State& state = *static_cast<State*>(opaqueState);
    jit::ByteBuffer bb(size);
    bb.resize(size);
    assert((reinterpret_cast<uintptr_t>(bb.data()) & (alignment - 1)) == 0);
    state.m_codeSectionList.push_back(std::move(bb));
    state.m_codeSectionNames.push_back(sectionName);
    jit::ByteBuffer& bb2 = state.m_codeSectionList.back();

    if (!strcmp(sectionName, SECTION_NAME("llvm_stackmaps")))
        state.m_stackMapsSection = &bb2;
    return const_cast<uint8_t*>(bb2.data());
}

static LLVMBool mmApplyPermissions(void*, char**)
{
    return false;
}

static void mmDestroy(void*)
{
}

int main()
{
    initLLVM();
    LLVMMCJITCompilerOptions options;
    llvmAPI->InitializeMCJITCompilerOptions(&options, sizeof(options));
    options.OptLevel = 2;
    State state;

    options.MCJMM = llvmAPI->CreateSimpleMCJITMemoryManager(
        &state, mmAllocateCodeSection, mmAllocateDataSection, mmApplyPermissions, mmDestroy);
}

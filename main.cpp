#include <assert.h>
#include <string.h>
#include "LLVMAPI.h"
#include "InitializeLLVM.h"
#include "CompilerState.h"
#include "log.h"
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

static void compile(State& state)
{
    LLVMMCJITCompilerOptions options;
    llvmAPI->InitializeMCJITCompilerOptions(&options, sizeof(options));
    options.OptLevel = 2;
    LLVMExecutionEngineRef engine;
    char* error = 0;

    options.MCJMM = llvmAPI->CreateSimpleMCJITMemoryManager(
        &state, mmAllocateCodeSection, mmAllocateDataSection, mmApplyPermissions, mmDestroy);
    if (llvmAPI->CreateMCJITCompilerForModule(&engine, state.m_module, &options, sizeof(options), &error)) {
        LOGE("FATAL: Could not create LLVM execution engine: %s", error);
        assert(false);
    }
    LLVMModuleRef module = state.m_module;
    LLVMPassManagerRef functionPasses = 0;
    LLVMPassManagerRef modulePasses;
    LLVMTargetDataRef targetData = llvmAPI->GetExecutionEngineTargetData(engine);
    char* stringRepOfTargetData = llvmAPI->CopyStringRepOfTargetData(targetData);
    llvmAPI->SetDataLayout(module, stringRepOfTargetData);
    free(stringRepOfTargetData);

    LLVMPassManagerBuilderRef passBuilder = llvmAPI->PassManagerBuilderCreate();
    llvmAPI->PassManagerBuilderSetOptLevel(passBuilder, 2);
    llvmAPI->PassManagerBuilderUseInlinerWithThreshold(passBuilder, 275);
    llvmAPI->PassManagerBuilderSetSizeLevel(passBuilder, 0);

    functionPasses = llvmAPI->CreateFunctionPassManagerForModule(module);
    modulePasses = llvmAPI->CreatePassManager();

    llvmAPI->AddTargetData(llvmAPI->GetExecutionEngineTargetData(engine), modulePasses);

    llvmAPI->PassManagerBuilderPopulateFunctionPassManager(passBuilder, functionPasses);
    llvmAPI->PassManagerBuilderPopulateModulePassManager(passBuilder, modulePasses);

    llvmAPI->PassManagerBuilderDispose(passBuilder);

    llvmAPI->InitializeFunctionPassManager(functionPasses);
    for (LLVMValueRef function = llvmAPI->GetFirstFunction(module); function; function = llvmAPI->GetNextFunction(function))
        llvmAPI->RunFunctionPassManager(functionPasses, function);
    llvmAPI->FinalizeFunctionPassManager(functionPasses);

    llvmAPI->RunPassManager(modulePasses, module);
    state.m_entryPoint = reinterpret_cast<void*>(llvmAPI->GetPointerToGlobal(engine, state.m_function));

    if (functionPasses)
        llvmAPI->DisposePassManager(functionPasses);
    llvmAPI->DisposePassManager(modulePasses);
    llvmAPI->DisposeExecutionEngine(engine);
}

int main()
{
    initLLVM();
    State state("test");
    LLVMTypeRef int32Type = llvmAPI->Int32TypeInContext(state.m_context);
    LLVMTypeRef structElements[] = { int32Type };
    LLVMTypeRef argumentType = llvmAPI->PointerType(llvmAPI->StructTypeInContext(state.m_context, structElements, sizeof(structElements) / sizeof(structElements[0]), false), 0);
    state.m_function = llvmAPI->AddFunction(
        state.m_module, "test", llvmAPI->FunctionType(int32Type, &argumentType, 1, false));
    LLVMValueRef arg0 = llvmAPI->GetParam(state.m_function, 0);
    LLVMBasicBlockRef entry = llvmAPI->AppendBasicBlockInContext(state.m_context, state.m_function, "Prologue");
    LLVMBuilderRef builder = llvmAPI->CreateBuilderInContext(state.m_context);
    llvmAPI->PositionBuilderAtEnd(builder, entry);
    LLVMValueRef one = llvmAPI->ConstInt(llvmAPI->Int32TypeInContext(state.m_context), 1, false);
    LLVMValueRef gep = llvmAPI->BuildStructGEP(builder, arg0, 0, "");
    LLVMValueRef loaded = llvmAPI->BuildLoad(builder, gep, "");
    LLVMValueRef add = llvmAPI->BuildAdd(builder, loaded, one, "");
    llvmAPI->BuildRet(builder, add);
    llvmAPI->DisposeBuilder(builder);

    compile(state);
    llvmAPI->DumpModule(state.m_module);
    assert(state.m_entryPoint == state.m_codeSectionList.front().data());
    return 0;
}

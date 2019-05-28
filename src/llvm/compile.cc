#include "src/llvm/compile.h"
#include <assert.h>
#include <string.h>
#include <memory>
#include "src/llvm/compiler-state.h"
#include "src/llvm/log.h"

#define SECTION_NAME_PREFIX "."
#define SECTION_NAME(NAME) (SECTION_NAME_PREFIX NAME)

namespace v8 {
namespace internal {
namespace tf_llvm {
typedef CompilerState State;

static inline size_t round_up(size_t s, unsigned alignment) {
  return (s + alignment - 1) & ~(alignment - 1);
}

static uint8_t* mmAllocateCodeSection(void* opaqueState, uintptr_t size,
                                      unsigned alignment, unsigned,
                                      const char* sectionName) {
  State& state = *static_cast<State*>(opaqueState);

  state.codeSectionList_.push_back(tf_llvm::ByteBuffer());
  state.codeSectionNames_.push_back(sectionName);

  tf_llvm::ByteBuffer& bb(state.codeSectionList_.back());
  bb.resize(size);
  EMASSERT((reinterpret_cast<uintptr_t>(bb.data()) & (alignment - 1)) == 0);

  return const_cast<uint8_t*>(bb.data());
}

static uint8_t* mmAllocateDataSection(void* opaqueState, uintptr_t size,
                                      unsigned alignment, unsigned,
                                      const char* sectionName, LLVMBool) {
  State& state = *static_cast<State*>(opaqueState);

  state.dataSectionList_.push_back(tf_llvm::ByteBuffer());
  state.dataSectionNames_.push_back(sectionName);

  tf_llvm::ByteBuffer& bb(state.dataSectionList_.back());
  bb.resize(size);
  EMASSERT((reinterpret_cast<uintptr_t>(bb.data()) & (alignment - 1)) == 0);
  if (!strcmp(sectionName, SECTION_NAME("llvm_stackmaps"))) {
    state.stackMapsSection_ = &bb;
  } else if (!strcmp(sectionName, SECTION_NAME("ARM.extab"))) {
    state.exception_table_ = &bb;
  }

  return const_cast<uint8_t*>(bb.data());
}

static LLVMBool mmApplyPermissions(void*, char**) { return false; }

static void mmDestroy(void*) {}

void compile(State& state) {
  LLVMMCJITCompilerOptions options;
  LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
  options.OptLevel = 3;
  LLVMExecutionEngineRef engine;
  char* error = 0;
  options.MCJMM = LLVMCreateSimpleMCJITMemoryManager(
      &state, mmAllocateCodeSection, mmAllocateDataSection, mmApplyPermissions,
      mmDestroy);
  if (LLVMCreateMCJITCompilerForModule(&engine, state.module_, &options,
                                       sizeof(options), &error)) {
    LOGE("FATAL: Could not create LLVM execution engine: %s", error);
    EMASSERT(false);
  }
  LLVMModuleRef module = state.module_;
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

  LLVMPassManagerBuilderPopulateFunctionPassManager(passBuilder,
                                                    functionPasses);
  LLVMPassManagerBuilderPopulateModulePassManager(passBuilder, modulePasses);

  LLVMPassManagerBuilderDispose(passBuilder);

  LLVMInitializeFunctionPassManager(functionPasses);
  for (LLVMValueRef function = LLVMGetFirstFunction(module); function;
       function = LLVMGetNextFunction(function))
    LLVMRunFunctionPassManager(functionPasses, function);
  LLVMFinalizeFunctionPassManager(functionPasses);

  LLVMRunPassManager(modulePasses, module);
  state.entryPoint_ =
      reinterpret_cast<void*>(LLVMGetPointerToGlobal(engine, state.function_));

  if (functionPasses) LLVMDisposePassManager(functionPasses);
  LLVMDisposePassManager(modulePasses);
  LLVMDisposeExecutionEngine(engine);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

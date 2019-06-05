// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/compile.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

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
#if defined(FEATURE_SAMPLE_PGO) && !defined(FEATURE_USE_SAMPLE_PGO)
// This must be kept in sync with gdb/gdb/jit.h .
typedef enum {
  JIT_NOACTION = 0,
  JIT_REGISTER_FN,
  JIT_UNREGISTER_FN
} jit_actions_t;

struct jit_code_entry {
  struct jit_code_entry* next_entry;
  struct jit_code_entry* prev_entry;
  const char* symfile_addr;
  uint64_t symfile_size;
};

struct jit_descriptor {
  uint32_t version;
  // This should be jit_actions_t, but we want to be specific about the
  // bit-width.
  uint32_t action_flag;
  struct jit_code_entry* relevant_entry;
  struct jit_code_entry* first_entry;
};

extern "C" struct jit_descriptor __jit_debug_descriptor;
void SaveObjectFile(const State& state) {
  if (__jit_debug_descriptor.action_flag != JIT_REGISTER_FN) {
    return;
  }
  char file_name_buf[256];
  snprintf(file_name_buf, 256, "%s.o", state.function_name_);
  int fd = open(file_name_buf, O_WRONLY | O_CREAT | O_EXCL,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (fd == -1) return;
  write(fd, __jit_debug_descriptor.first_entry->symfile_addr,
        __jit_debug_descriptor.first_entry->symfile_size);
  close(fd);
}
#endif  // FEATURE_SAMPLE_PGO && !FEATURE_SAMPLE_PGO

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
#if defined(FEATURE_SAMPLE_PGO) && !defined(FEATURE_USE_SAMPLE_PGO)
  SaveObjectFile(state);
#endif  // FEATURE_SAMPLE_PGO && !FEATURE_SAMPLE_PGO
  LLVMDisposeExecutionEngine(engine);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

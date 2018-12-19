#include "src/llvm/compile.h"
#include <assert.h>
#include <string.h>
#include <memory>
#include "src/llvm/compiler-state.h"
#include "src/llvm/log.h"

#define SECTION_NAME_PREFIX "."
#define SECTION_NAME(NAME) (SECTION_NAME_PREFIX NAME)

namespace llvm {
void linkCoreCLRGC();
}

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
  assert((reinterpret_cast<uintptr_t>(bb.data()) & (alignment - 1)) == 0);

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
  assert((reinterpret_cast<uintptr_t>(bb.data()) & (alignment - 1)) == 0);
  if (!strcmp(sectionName, SECTION_NAME("llvm_stackmaps"))) {
    state.stackMapsSection_ = &bb;
  }

  return const_cast<uint8_t*>(bb.data());
}

static LLVMBool mmApplyPermissions(void*, char**) { return false; }

static void mmDestroy(void*) {}

static const char* symbolLookupCallback(void* DisInfo, uint64_t ReferenceValue,
                                        uint64_t* ReferenceType,
                                        uint64_t ReferencePC,
                                        const char** ReferenceName) {
  *ReferenceType = LLVMDisassembler_ReferenceType_InOut_None;
  return nullptr;
}

static void disassemble(tf_llvm::ByteBuffer& code) {
  LLVMDisasmContextRef DCR = LLVMCreateDisasm("armv7-linux-android", nullptr, 0,
                                              nullptr, symbolLookupCallback);

  uint8_t* BytesP = code.data();

  unsigned NumBytes = code.size();
  unsigned PC = 0;
  const char OutStringSize = 100;
  char OutString[OutStringSize];
  printf(
      "========================================================================"
      "========\n");
  while (NumBytes != 0) {
    size_t InstSize = LLVMDisasmInstruction(DCR, BytesP, NumBytes, PC,
                                            OutString, OutStringSize);

    PC += InstSize;
    BytesP += InstSize;
    NumBytes -= InstSize;
    printf("%s\n", OutString);
  }
  printf(
      "========================================================================"
      "========\n");
}

static void disassemble(State& state) {
  for (auto& code : state.codeSectionList_) {
    disassemble(code);
  }
}

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
    assert(false);
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
  disassemble(state);
  llvm::linkCoreCLRGC();
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

// Copyright 2019 UCWeb Co., Ltd.

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "src/llvm/basic-block-manager.h"
#include "src/llvm/basic-block.h"
#include "src/llvm/compile.h"
#include "src/llvm/compiler-state.h"
#include "src/llvm/initialize-llvm.h"
#include "src/llvm/liveness-analysis-visitor.h"
#include "src/llvm/llvm-log.h"
#include "src/llvm/llvm-tf-builder.h"
#include "src/llvm/output.h"
#include "src/llvm/stack-map-info.h"
#include "src/llvm/tf/tf-parser.h"
namespace v8 {
namespace internal {
namespace tf_llvm {
typedef CompilerState State;

static void buildIR(State& state) {
  FILE* f = fopen("src/llvm/tf/tests/scheduled.txt", "r");
  if (!f) {
    fprintf(stderr, "fails to open src/llvm/tf/tests/scheduled.txt\n");
    exit(1);
  }
  BasicBlockManager BBM;
  {
    LivenessAnalysisVisitor lav(BBM);
    TFParser tfparser(&lav);
    tfparser.Parse(f);
    lav.CalculateLivesIns();
  }
  fseek(f, 0, SEEK_SET);
  {
    Output output(state);
    RegisterParameterDesc desc = {{0, output.taggedType()},
                                  {5, output.repo().intPtr},
                                  {6, output.taggedType()},
                                  {8, output.taggedType()},
                                  {7, output.taggedType()}};
    output.initializeBuild(desc, true);
    LLVMTFBuilder builder(output, BBM, state.stack_map_info_map_,
                          state.load_constant_recorder_);
    TFParser tfparser(&builder);
    tfparser.Parse(f);
    builder.End();
  }
  fclose(f);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

int main() {
  using namespace v8::internal::tf_llvm;
  initLLVM();
  State state("test");
  buildIR(state);
  dumpModule(state.module_);
  compile(state);
  return 0;
}

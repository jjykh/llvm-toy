#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "compile.h"
#include "compiler-state.h"
#include "initialize-llvm.h"
#include "log.h"
#include "output.h"
#include "registers.h"

#include "basic-block-manager.h"
#include "basic-block.h"
#include "liveness-analysis-visitor.h"
#include "llvm-tf-builder.h"
#include "tf-parser.h"
typedef jit::CompilerState State;

static void buildIR(State& state) {
  FILE* f = fopen("tf/tests/scheduled.txt", "r");
  if (!f) {
    fprintf(stderr, "fails to open tf/tests/scheduled.txt\n");
    exit(1);
  }
  using namespace jit;
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
    LLVMTFBuilder builder(output, BBM);
    TFParser tfparser(&builder);
    tfparser.Parse(f);
    builder.End();
  }
  fclose(f);
}

int main() {
  initLLVM();
  using namespace jit;
  State state("test");
  buildIR(state);
  dumpModule(state.module_);
  compile(state);
  return 0;
}

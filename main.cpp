#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "compile.h"
#include "compiler-state.h"
#include "initialize-llvm.h"
#include "output.h"
#include "registers.h"
#include "log.h"
typedef jit::CompilerState State;

static void buildIR(State& state) {
  using namespace jit;
  Output output(state);
  RegisterParameterDesc desc = {{"r0", output.taggedType()},
                                {"r5", output.repo().intPtr},
                                {"r6", output.taggedType()},
                                {"r8", output.taggedType()},
                                {"r7", output.taggedType()}};
  output.initializeBuild(desc);
  LBasicBlock body = output.appendBasicBlock("Body");
  output.buildBr(body);
  output.positionToBBEnd(body);
  output.buildRet(output.registerParameter(2));
}

int main() {
  initLLVM();
  using namespace jit;
  State state("test");
  buildIR(state);
  dumpModule(state.m_module);
  compile(state);
  return 0;
}

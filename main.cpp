#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "LLVMAPI.h"
#include "InitializeLLVM.h"
#include "CompilerState.h"
#include "Output.h"
#include "Compile.h"
#include "Link.h"
#include "log.h"
typedef jit::CompilerState State;

static void myexit(void)
{
    exit(1);
}

static void buildIR(State& state)
{
    using namespace jit;
    Output output(state);
    LValue arg = output.arg();
    LBasicBlock body = output.appendBasicBlock("Body");
    output.buildBr(body);
    output.positionToBBEnd(body);
    LValue one = output.constInt32(1);
    LValue gep = output.buildStructGEP(arg, 0);
    LValue loaded = output.buildLoad(gep);
    LValue add = output.buildAdd(loaded, one);
    output.buildStore(add, gep);

    LBasicBlock patch = output.appendBasicBlock("Patch");
    output.buildBr(patch);
    output.positionToBBEnd(patch);
    output.buildChainPatch(reinterpret_cast<void*>(myexit));
    output.buildRetVoid();
}

static void mydispChain(void);

void mydispChain(void)
{
    asm volatile("\n"
                 "movq %rdi, %r11\n"
                 "movq %rsi, %rbp\n"
                 "jmp *%r11\n");
}

int main()
{
    initLLVM();
    using namespace jit;
    State state("test");
    buildIR(state);
    dumpModule(state.m_module);
    compile(state);
    assert(state.m_entryPoint == state.m_codeSectionList.front().data());
    link(state, reinterpret_cast<void*>(mydispChain), reinterpret_cast<void*>(mydispChain));
    return 0;
}

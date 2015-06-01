#include <assert.h>
#include "CompilerState.h"
#include "Output.h"

namespace jit {
Output::Output(CompilerState& state)
    : m_state(state)
    , m_repo(state.m_context, state.m_module)
    , m_builder(nullptr)
    , m_stackMapsId(1)
{
    m_argType = pointerType(arrayType(repo().intPtr, state.m_platformDesc.m_contextSize / sizeof(intptr_t)));
    state.m_function = addFunction(
        state.m_module, "main", functionType(repo().int64, m_argType));
    m_builder = llvmAPI->CreateBuilderInContext(state.m_context);

    m_prologue = appendBasicBlock("Prologue");
    positionToBBEnd(m_prologue);
    buildGetArg();
}
Output::~Output()
{
    llvmAPI->DisposeBuilder(m_builder);
}

LBasicBlock Output::appendBasicBlock(const char* name)
{
    return jit::appendBasicBlock(m_state.m_context, m_state.m_function, name);
}

void Output::positionToBBEnd(LBasicBlock bb)
{
    llvmAPI->PositionBuilderAtEnd(m_builder, bb);
}

LValue Output::constInt32(int i)
{
    return jit::constInt(m_repo.int32, i);
}

LValue Output::constInt64(long long l)
{
    return jit::constInt(m_repo.int64, l);
}

LValue Output::buildStructGEP(LValue structVal, unsigned field)
{
    return jit::buildStructGEP(m_builder, structVal, field);
}

LValue Output::buildLoad(LValue toLoad)
{
    return jit::buildLoad(m_builder, toLoad);
}

LValue Output::buildStore(LValue val, LValue pointer)
{
    return jit::buildStore(m_builder, val, pointer);
}

LValue Output::buildAdd(LValue lhs, LValue rhs)
{
    return jit::buildAdd(m_builder, lhs, rhs);
}

LValue Output::buildBr(LBasicBlock bb)
{
    return jit::buildBr(m_builder, bb);
}

LValue Output::buildRet(LValue ret)
{
    return jit::buildRet(m_builder, ret);
}

LValue Output::buildRetVoid(void)
{
    return jit::buildRetVoid(m_builder);
}

LValue Output::buildCast(LLVMOpcode Op, LLVMValueRef Val, LLVMTypeRef DestTy)
{
    llvmAPI->BuildCast(m_builder, Op, Val, DestTy, "");
}

void Output::buildGetArg()
{
    m_arg = llvmAPI->GetParam(m_state.m_function, 0);
}

void Output::buildDirectPatch(uintptr_t where)
{
    LValue constAddr = constInt64(where);
    LValue constIndex[] = { constInt32(0), constInt32(m_state.m_platformDesc.m_pcFieldOffset / sizeof(intptr_t)) };
    buildStore(constAddr, llvmAPI->BuildInBoundsGEP(m_builder, m_arg, constIndex, 2, ""));
    LValue call = buildCall(repo().patchpointInt64Intrinsic(), constInt32(m_stackMapsId), constInt32(m_state.m_platformDesc.m_directSize), constNull(repo().ref8), constInt32(0));
    llvmAPI->SetInstructionCallConv(call, LLVMAnyRegCallConv);
    buildUnreachable(m_builder);
    // record the stack map info
    PatchDesc desc = { PatchType::Direct };
    m_state.m_patchMap.insert(std::make_pair(m_stackMapsId++, desc));
}

void Output::buildIndirectPatch(LValue where)
{
    // FIXME: need rip index in platform desc.
    LValue constIndex[] = { constInt32(0), constInt32(m_state.m_platformDesc.m_pcFieldOffset / sizeof(intptr_t)) };
    buildStore(where, llvmAPI->BuildInBoundsGEP(m_builder, m_arg, constIndex, 2, ""));
    LValue call = buildCall(repo().patchpointInt64Intrinsic(), constInt32(m_stackMapsId), constInt32(m_state.m_platformDesc.m_indirectSize), constNull(repo().ref8), constInt32(0));
    llvmAPI->SetInstructionCallConv(call, LLVMAnyRegCallConv);
    buildUnreachable(m_builder);
    // record the stack map info
    PatchDesc desc = { PatchType::Indirect };
    m_state.m_patchMap.insert(std::make_pair(m_stackMapsId++, desc));
}
}

#include "CompilerState.h"
#include "Output.h"

namespace jit {
Output::Output(CompilerState& state)
    : m_state(state)
    , m_repo(state.m_context, state.m_module)
    , m_builder(nullptr)
    , m_stackMapsId(otherPatchStartId())
{
    LType structElements[] = { repo().int32 };
    m_argType = pointerType(structType(state.m_context, structElements, sizeof(structElements) / sizeof(structElements[0])));
    state.m_function = addFunction(
        state.m_module, "body", functionType(repo().int64, repo().voidType));
    llvmAPI->AddFunctionAttr(state.m_function, LLVMNakedAttribute);
    m_builder = llvmAPI->CreateBuilderInContext(state.m_context);

    m_prologue = appendBasicBlock("Prologue");
    positionToBBEnd(m_prologue);
    m_arg = buildGetArgPatch();
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

LValue Output::buildGetArgPatch()
{
    LValue callRet = buildCall(repo().patchpointInt64Intrinsic(), constInt64(argPatchId()), constInt32(3), constNull(repo().ref8), constInt32(0));
    return buildCast(LLVMIntToPtr, callRet, m_argType);
}

void Output::buildChainPatch(void* where)
{
    buildCall(repo().patchpointInt64Intrinsic(), constInt64(chainPatchId()), constInt32(20), constInt64(0), constInt32(2), constInt(repo().intPtr, reinterpret_cast<uintptr_t>(where)), m_arg);
}

void Output::buildXIndirectPatch(LValue where)
{
    buildCall(repo().patchpointInt64Intrinsic(), constInt64(xIndirectPatchId()), constInt32(20), constNull(repo().ref8), constInt32(2), where, m_arg);
}
}

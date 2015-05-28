#include "CompilerState.h"
#include "Output.h"

namespace jit {
Output::Output(CompilerState& state)
    : m_state(state)
    , m_repo(state.m_context, state.m_module)
    , m_builder(nullptr)
{
    LType structElements[] = { repo().int32 };
    LType argumentType = pointerType(structType(state.m_context, structElements, sizeof(structElements) / sizeof(structElements[0])));
    state.m_function = addFunction(
        state.m_module, "body", functionType(repo().int64, argumentType));
    m_builder = llvmAPI->CreateBuilderInContext(state.m_context);
}
Output::~Output()
{
    llvmAPI->DisposeBuilder(m_builder);
}

LValue Output::getParam(unsigned index)
{
    jit::getParam(m_state.m_function, index);
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
}

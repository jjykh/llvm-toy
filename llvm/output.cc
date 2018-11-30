#include "output.h"
#include <assert.h>
#include "compiler-state.h"

namespace jit {
Output::Output(CompilerState& state)
    : m_state(state),
      m_repo(state.m_context, state.m_module),
      m_builder(nullptr),
      m_prologue(nullptr),
      m_taggedType(nullptr),
      m_root(nullptr),
      m_stackMapsId(1) {
  m_taggedType =
      pointerType(LLVMStructCreateNamed(state.m_context, "TaggedStruct"));
  state.m_function =
      addFunction(state.m_module, "main", functionType(taggedType()));
  setFunctionCallingConv(state.m_function, LLVMAnyRegCallConv);
}
Output::~Output() { LLVMDisposeBuilder(m_builder); }

void Output::initializeBuild(const RegisterParameterDesc& registerParameters) {
  assert(!m_builder);
  assert(!m_prologue);
  m_builder = LLVMCreateBuilderInContext(m_state.m_context);

  m_prologue = appendBasicBlock("Prologue");
  positionToBBEnd(m_prologue);
  // build parameters
  char empty[] = "\0";
  char constraint[256];
  for (auto& registerParameter : registerParameters) {
    int len =
        snprintf(constraint, 256, "={%s}", registerParameter.name.c_str());
    LValue rvalue = buildInlineAsm(functionType(registerParameter.type), empty,
                                   0, constraint, len, true);
    m_registerParameters.push_back(rvalue);
  }
  int len = snprintf(constraint, 256, "={%s}", "r10");
  m_root = buildInlineAsm(functionType(pointerType(taggedType())), empty, 0,
                          constraint, len, true);
  len = snprintf(constraint, 256, "={%s}", "r11");
  m_fp = buildInlineAsm(functionType(pointerType(pointerType(repo().voidType))),
                        empty, 0, constraint, len, true);
}

LBasicBlock Output::appendBasicBlock(const char* name) {
  return jit::appendBasicBlock(m_state.m_context, m_state.m_function, name);
}

void Output::positionToBBEnd(LBasicBlock bb) {
  LLVMPositionBuilderAtEnd(m_builder, bb);
}

LValue Output::constInt32(int i) { return jit::constInt(m_repo.int32, i); }

LValue Output::constInt64(long long l) {
  return jit::constInt(m_repo.int64, l);
}

LValue Output::constIntPtr(intptr_t i) {
  return jit::constInt(m_repo.intPtr, i);
}

LValue Output::buildStructGEP(LValue structVal, unsigned field) {
  return jit::buildStructGEP(m_builder, structVal, field);
}

LValue Output::buildLoad(LValue toLoad) {
  return jit::buildLoad(m_builder, toLoad);
}

LValue Output::buildStore(LValue val, LValue pointer) {
  return jit::buildStore(m_builder, val, pointer);
}

LValue Output::buildAdd(LValue lhs, LValue rhs) {
  return jit::buildAdd(m_builder, lhs, rhs);
}

LValue Output::buildNSWAdd(LValue lhs, LValue rhs) {
  return LLVMBuildNSWAdd(m_builder, lhs, rhs, "");
}

LValue Output::buildSub(LValue lhs, LValue rhs) {
  return jit::buildSub(m_builder, lhs, rhs);
}

LValue Output::buildNSWSub(LValue lhs, LValue rhs) {
  return LLVMBuildNSWSub(m_builder, lhs, rhs, "");
}

LValue Output::buildMul(LValue lhs, LValue rhs) {
  return jit::buildMul(m_builder, lhs, rhs);
}

LValue Output::buildNSWMul(LValue lhs, LValue rhs) {
  return LLVMBuildNSWMul(m_builder, lhs, rhs, "");
}

LValue Output::buildShl(LValue lhs, LValue rhs) {
  return jit::buildShl(m_builder, lhs, rhs);
}

LValue Output::buildShr(LValue lhs, LValue rhs) {
  return jit::buildLShr(m_builder, lhs, rhs);
}

LValue Output::buildSar(LValue lhs, LValue rhs) {
  return jit::buildAShr(m_builder, lhs, rhs);
}

LValue Output::buildAnd(LValue lhs, LValue rhs) {
  return jit::buildAnd(m_builder, lhs, rhs);
}

LValue Output::buildBr(LBasicBlock bb) { return jit::buildBr(m_builder, bb); }

LValue Output::buildCondBr(LValue condition, LBasicBlock taken,
                           LBasicBlock notTaken) {
  return jit::buildCondBr(m_builder, condition, taken, notTaken);
}

LValue Output::buildRet(LValue ret) { return jit::buildRet(m_builder, ret); }

LValue Output::buildRetVoid(void) { return jit::buildRetVoid(m_builder); }

LValue Output::buildCast(LLVMOpcode Op, LLVMValueRef Val, LLVMTypeRef DestTy) {
  return LLVMBuildCast(m_builder, Op, Val, DestTy, "");
}

void Output::buildDirectPatch(uintptr_t where) {
  PatchDesc desc = {PatchType::Direct};
  buildPatchCommon(constInt64(where), desc,
                   m_state.m_platformDesc.m_directSize);
}

void Output::buildIndirectPatch(LValue where) {
  PatchDesc desc = {PatchType::Indirect};
  buildPatchCommon(where, desc, m_state.m_platformDesc.m_indirectSize);
}

void Output::buildAssistPatch(LValue where) {
  PatchDesc desc = {PatchType::Assist};
  buildPatchCommon(where, desc, m_state.m_platformDesc.m_assistSize);
}

void Output::buildPatchCommon(LValue where, const PatchDesc& desc,
                              size_t patchSize) {
  LValue call = buildCall(repo().patchpointVoidIntrinsic(),
                          constIntPtr(m_stackMapsId), constInt32(patchSize),
                          constIntToPtr(constInt32(patchSize), repo().ref8),
                          constInt32(1), constIntToPtr(where, repo().ref8));
  LLVMSetInstructionCallConv(call, LLVMAnyRegCallConv);
  buildUnreachable(m_builder);
  // record the stack map info
  m_state.m_patchMap.insert(std::make_pair(m_stackMapsId++, desc));
}

LValue Output::buildSelect(LValue condition, LValue taken, LValue notTaken) {
  return jit::buildSelect(m_builder, condition, taken, notTaken);
}

LValue Output::buildICmp(LIntPredicate cond, LValue left, LValue right) {
  return jit::buildICmp(m_builder, cond, left, right);
}

LValue Output::buildInlineAsm(LType type, char* asmString, size_t asmStringSize,
                              char* constraintString,
                              size_t constraintStringSize, bool sideEffect) {
  LValue func = LLVMGetInlineAsm(type, asmString, asmStringSize,
                                 constraintString, constraintStringSize,
                                 sideEffect, false, LLVMInlineAsmDialectATT);
  return buildCall(func);
}

LValue Output::buildPhi(LType type) { return jit::buildPhi(m_builder, type); }

LValue Output::buildGEPWithByteOffset(LValue base, int offset, LType dstType) {
  LValue base_ref8 = buildBitCast(base, repo().ref8);
  LValue offset_value = constIntPtr(offset);
  LValue dst_ref8 = LLVMBuildGEP(m_builder, base_ref8, &offset_value, 1, "");
  return buildBitCast(dst_ref8, dstType);
}

LValue Output::buildBitCast(LValue val, LType type) {
  return jit::buildBitCast(m_builder, val, type);
}
}  // namespace jit

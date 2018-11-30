#include "output.h"
#include <assert.h>
#include "compiler-state.h"

namespace jit {
Output::Output(CompilerState& state)
    : state_(state),
      repo_(state.context_, state.module_),
      builder_(nullptr),
      prologue_(nullptr),
      taggedType_(nullptr),
      root_(nullptr),
      stackMapsId_(1) {
  taggedType_ =
      pointerType(LLVMStructCreateNamed(state.context_, "TaggedStruct"));
  state.function_ =
      addFunction(state.module_, "main", functionType(taggedType()));
  setFunctionCallingConv(state.function_, LLVMAnyRegCallConv);
}
Output::~Output() { LLVMDisposeBuilder(builder_); }

void Output::initializeBuild(const RegisterParameterDesc& registerParameters) {
  assert(!builder_);
  assert(!prologue_);
  builder_ = LLVMCreateBuilderInContext(state_.context_);

  prologue_ = appendBasicBlock("Prologue");
  positionToBBEnd(prologue_);
  // build parameters
  char empty[] = "\0";
  char constraint[256];
  for (auto& registerParameter : registerParameters) {
    int len =
        snprintf(constraint, 256, "={%s}", registerParameter.name.c_str());
    LValue rvalue = buildInlineAsm(functionType(registerParameter.type), empty,
                                   0, constraint, len, true);
    registerParameters_.push_back(rvalue);
  }
  int len = snprintf(constraint, 256, "={%s}", "r10");
  root_ = buildInlineAsm(functionType(pointerType(taggedType())), empty, 0,
                         constraint, len, true);
  len = snprintf(constraint, 256, "={%s}", "r11");
  fp_ = buildInlineAsm(functionType(pointerType(pointerType(repo().voidType))),
                       empty, 0, constraint, len, true);
}

LBasicBlock Output::appendBasicBlock(const char* name) {
  return jit::appendBasicBlock(state_.context_, state_.function_, name);
}

void Output::positionToBBEnd(LBasicBlock bb) {
  LLVMPositionBuilderAtEnd(builder_, bb);
}

LValue Output::constInt32(int i) { return jit::constInt(repo_.int32, i); }

LValue Output::constInt64(long long l) { return jit::constInt(repo_.int64, l); }

LValue Output::constIntPtr(intptr_t i) {
  return jit::constInt(repo_.intPtr, i);
}

LValue Output::buildStructGEP(LValue structVal, unsigned field) {
  return jit::buildStructGEP(builder_, structVal, field);
}

LValue Output::buildLoad(LValue toLoad) {
  return jit::buildLoad(builder_, toLoad);
}

LValue Output::buildStore(LValue val, LValue pointer) {
  return jit::buildStore(builder_, val, pointer);
}

LValue Output::buildAdd(LValue lhs, LValue rhs) {
  return jit::buildAdd(builder_, lhs, rhs);
}

LValue Output::buildNSWAdd(LValue lhs, LValue rhs) {
  return LLVMBuildNSWAdd(builder_, lhs, rhs, "");
}

LValue Output::buildSub(LValue lhs, LValue rhs) {
  return jit::buildSub(builder_, lhs, rhs);
}

LValue Output::buildNSWSub(LValue lhs, LValue rhs) {
  return LLVMBuildNSWSub(builder_, lhs, rhs, "");
}

LValue Output::buildMul(LValue lhs, LValue rhs) {
  return jit::buildMul(builder_, lhs, rhs);
}

LValue Output::buildNSWMul(LValue lhs, LValue rhs) {
  return LLVMBuildNSWMul(builder_, lhs, rhs, "");
}

LValue Output::buildShl(LValue lhs, LValue rhs) {
  return jit::buildShl(builder_, lhs, rhs);
}

LValue Output::buildShr(LValue lhs, LValue rhs) {
  return jit::buildLShr(builder_, lhs, rhs);
}

LValue Output::buildSar(LValue lhs, LValue rhs) {
  return jit::buildAShr(builder_, lhs, rhs);
}

LValue Output::buildAnd(LValue lhs, LValue rhs) {
  return jit::buildAnd(builder_, lhs, rhs);
}

LValue Output::buildBr(LBasicBlock bb) { return jit::buildBr(builder_, bb); }

LValue Output::buildCondBr(LValue condition, LBasicBlock taken,
                           LBasicBlock notTaken) {
  return jit::buildCondBr(builder_, condition, taken, notTaken);
}

LValue Output::buildRet(LValue ret) { return jit::buildRet(builder_, ret); }

LValue Output::buildRetVoid(void) { return jit::buildRetVoid(builder_); }

LValue Output::buildCast(LLVMOpcode Op, LLVMValueRef Val, LLVMTypeRef DestTy) {
  return LLVMBuildCast(builder_, Op, Val, DestTy, "");
}

void Output::buildDirectPatch(uintptr_t where) {
  PatchDesc desc = {PatchType::Direct};
  buildPatchCommon(constInt64(where), desc, state_.platformDesc_.directSize_);
}

void Output::buildIndirectPatch(LValue where) {
  PatchDesc desc = {PatchType::Indirect};
  buildPatchCommon(where, desc, state_.platformDesc_.indirectSize_);
}

void Output::buildAssistPatch(LValue where) {
  PatchDesc desc = {PatchType::Assist};
  buildPatchCommon(where, desc, state_.platformDesc_.assistSize_);
}

void Output::buildPatchCommon(LValue where, const PatchDesc& desc,
                              size_t patchSize) {
  LValue call = buildCall(repo().patchpointVoidIntrinsic(),
                          constIntPtr(stackMapsId_), constInt32(patchSize),
                          constIntToPtr(constInt32(patchSize), repo().ref8),
                          constInt32(1), constIntToPtr(where, repo().ref8));
  LLVMSetInstructionCallConv(call, LLVMAnyRegCallConv);
  buildUnreachable(builder_);
  // record the stack map info
  state_.patchMap_.insert(std::make_pair(stackMapsId_++, desc));
}

LValue Output::buildSelect(LValue condition, LValue taken, LValue notTaken) {
  return jit::buildSelect(builder_, condition, taken, notTaken);
}

LValue Output::buildICmp(LIntPredicate cond, LValue left, LValue right) {
  return jit::buildICmp(builder_, cond, left, right);
}

LValue Output::buildInlineAsm(LType type, char* asmString, size_t asmStringSize,
                              char* constraintString,
                              size_t constraintStringSize, bool sideEffect) {
  LValue func = LLVMGetInlineAsm(type, asmString, asmStringSize,
                                 constraintString, constraintStringSize,
                                 sideEffect, false, LLVMInlineAsmDialectATT);
  return buildCall(func);
}

LValue Output::buildPhi(LType type) { return jit::buildPhi(builder_, type); }

LValue Output::buildGEPWithByteOffset(LValue base, int offset, LType dstType) {
  LValue base_ref8 = buildBitCast(base, repo().ref8);
  LValue offset_value = constIntPtr(offset);
  LValue dst_ref8 = LLVMBuildGEP(builder_, base_ref8, &offset_value, 1, "");
  return buildBitCast(dst_ref8, dstType);
}

LValue Output::buildBitCast(LValue val, LType type) {
  return jit::buildBitCast(builder_, val, type);
}
}  // namespace jit
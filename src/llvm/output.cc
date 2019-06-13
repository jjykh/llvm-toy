// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/output.h"
#include <llvm-c/DebugInfo.h>
#include "src/frames.h"
#include "src/llvm/compiler-state.h"
#include "src/llvm/log.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
Output::Output(CompilerState& state)
    : state_(state),
      repo_(state.context_, state.module_),
      builder_(nullptr),
      di_builder_(nullptr),
      prologue_(nullptr),
      root_(nullptr),
      fp_(nullptr),
      parent_fp_(nullptr),
      bitcast_space_(nullptr),
      subprogram_(nullptr),
      stack_parameter_count_(0) {}

Output::~Output() {
  LLVMDisposeBuilder(builder_);
  LLVMDisposeDIBuilder(di_builder_);
}

void Output::initializeBuild(const RegisterParameterDesc& registerParameters,
                             bool v8cc) {
  EMASSERT(!builder_);
  EMASSERT(!prologue_);
  builder_ = LLVMCreateBuilderInContext(state_.context_);
  di_builder_ = LLVMCreateDIBuilderDisallowUnresolved(state_.module_);
  initializeFunction(registerParameters, v8cc);
  // FIXME: Add V8 to LLVM.
  LLVMSetGC(state_.function_, "coreclr");

  prologue_ = appendBasicBlock("Prologue");
  positionToBBEnd(prologue_);
  // build parameters
  if (v8cc) {
    root_ = LLVMGetParam(state_.function_, 10);
    fp_ = LLVMGetParam(state_.function_, 11);
    parent_fp_ = LLVMGetParam(state_.function_, 9);
  } else {
    root_ = LLVMGetParam(state_.function_, 5);
    fp_ = LLVMGetParam(state_.function_, 6);
  }
  std::vector<LValue> stack_parameters;
  for (auto& registerParameter : registerParameters) {
    if (registerParameter.name >= 0) {
      EMASSERT(registerParameter.name < 10);
      LValue rvalue = LLVMGetParam(state_.function_, registerParameter.name);
      registerParameters_.push_back(rvalue);
    } else {
      // callee frame
      LValue rvalue =
          LLVMGetParam(state_.function_,
                       kV8CCRegisterParameterCount + stack_parameter_count_);
      stack_parameters.push_back(rvalue);
      registerParameters_.push_back(nullptr);
      stack_parameter_count_++;
    }
  }
  auto j = stack_parameters.rbegin();
  for (auto i = registerParameters_.begin(); i != registerParameters_.end();
       ++i) {
    if (*i != nullptr) continue;
    *i = *j;
    ++j;
  }
  EMASSERT(j == stack_parameters.rend());
  bitcast_space_ = buildAlloca(arrayType(repo().int8, 16));
}

void Output::initializeFunction(const RegisterParameterDesc& registerParameters,
                                bool v8cc) {
  std::vector<LType> params_types = {taggedType(),
                                     taggedType(),
                                     taggedType(),
                                     taggedType(),
                                     taggedType(),
                                     taggedType(),
                                     taggedType(),
                                     taggedType(),
                                     taggedType(),
                                     repo().ref8,
                                     pointerType(taggedType()),
                                     pointerType(repo().ref8)};
  EMASSERT(params_types.size() == kV8CCRegisterParameterCount);

  for (auto& registerParameter : registerParameters) {
    if (registerParameter.name >= 0) {
      EMASSERT(registerParameter.name < 10);
      params_types[registerParameter.name] = registerParameter.type;
    } else {
      params_types.push_back(registerParameter.type);
    }
  }
  state_.function_ = addFunction(
      state_.function_name_, functionType(taggedType(), params_types.data(),
                                          params_types.size(), NotVariadic));
  if (v8cc)
    setFunctionCallingConv(state_.function_, LLVMV8CallConv);
  else
    setFunctionCallingConv(state_.function_, LLVMV8SBCallConv);

  if (state_.needs_frame_) {
    static const char kJSFunctionCall[] = "js-function-call";
    static const char kJSStubCall[] = "js-stub-call";
    switch (state_.prologue_kind_) {
      case PrologueKind::JSFunctionCall:
        LLVMAddTargetDependentFunctionAttr(state_.function_, kJSFunctionCall,
                                           nullptr);
        break;
      case PrologueKind::Stub: {
        char stub_marker[16];
        snprintf(stub_marker, sizeof(stub_marker), "%d",
                 StackFrame::TypeToMarker(StackFrame::STUB));
        LLVMAddTargetDependentFunctionAttr(state_.function_, kJSStubCall,
                                           stub_marker);
      } break;
      default:
        __builtin_trap();
    }
  }
  // arm jump tables are slow.
  static const char kNoJumpTables[] = "no-jump-tables";
  static const char kTrue[] = "true";
  LLVMAddTargetDependentFunctionAttr(state_.function_, kNoJumpTables, kTrue);
  char file_name[256];
  int file_name_count = snprintf(file_name, 256, "%s.c", state_.function_name_);
  LLVMMetadataRef file_name_meta = LLVMDIBuilderCreateFile(
      di_builder_, file_name, file_name_count, nullptr, 0);
  LLVMMetadataRef cu = LLVMDIBuilderCreateCompileUnit(
      di_builder_, LLVMDWARFSourceLanguageC, file_name_meta, nullptr, 0, true,
      nullptr, 0, 1, nullptr, 0, LLVMDWARFEmissionLineTablesOnly, 0, false,
      false);
  subprogram_ = LLVMDIBuilderCreateFunction(
      di_builder_, cu, state_.function_name_, strlen(state_.function_name_),
      nullptr, 0, file_name_meta, 1, nullptr, false, true, 1, LLVMDIFlagZero,
      true);
  LLVMSetSubprogram(state_.function_, subprogram_);
}

LBasicBlock Output::appendBasicBlock(const char* name) {
  return v8::internal::tf_llvm::appendBasicBlock(state_.context_,
                                                 state_.function_, name);
}

LBasicBlock Output::appendBasicBlock(LValue function, const char* name) {
  return v8::internal::tf_llvm::appendBasicBlock(state_.context_, function,
                                                 name);
}

void Output::positionToBBEnd(LBasicBlock bb) {
  LLVMPositionBuilderAtEnd(builder_, bb);
}

void Output::positionBefore(LValue value) {
  LLVMPositionBuilderBefore(builder_, value);
}

LValue Output::constInt32(int i) {
  return v8::internal::tf_llvm::constInt(repo_.int32, i);
}

LValue Output::constInt64(long long l) {
  return v8::internal::tf_llvm::constInt(repo_.int64, l);
}

LValue Output::constIntPtr(intptr_t i) {
  return v8::internal::tf_llvm::constInt(repo_.intPtr, i);
}

LValue Output::constTagged(void* magic) {
  LValue intptr = constIntPtr(reinterpret_cast<intptr_t>(magic));
  return buildCast(LLVMIntToPtr, intptr, taggedType());
}

LValue Output::buildStructGEP(LValue structVal, unsigned field) {
  return setInstrDebugLoc(
      v8::internal::tf_llvm::buildStructGEP(builder_, structVal, field));
}

LValue Output::buildLoad(LValue toLoad) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildLoad(builder_, toLoad));
}

LValue Output::buildStore(LValue val, LValue pointer) {
  return setInstrDebugLoc(
      v8::internal::tf_llvm::buildStore(builder_, val, pointer));
}

LValue Output::buildNeg(LValue val) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildNeg(builder_, val));
}

LValue Output::buildAdd(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildAdd(builder_, lhs, rhs));
}

LValue Output::buildFAdd(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildFAdd(builder_, lhs, rhs));
}

LValue Output::buildNSWAdd(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(LLVMBuildNSWAdd(builder_, lhs, rhs, ""));
}

LValue Output::buildSub(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildSub(builder_, lhs, rhs));
}

LValue Output::buildFSub(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildFSub(builder_, lhs, rhs));
}

LValue Output::buildNSWSub(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(LLVMBuildNSWSub(builder_, lhs, rhs, ""));
}

LValue Output::buildMul(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildMul(builder_, lhs, rhs));
}

LValue Output::buildSRem(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(LLVMBuildSRem(builder_, lhs, rhs, ""));
}

LValue Output::buildSDiv(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(LLVMBuildSDiv(builder_, lhs, rhs, ""));
}

LValue Output::buildFMul(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildFMul(builder_, lhs, rhs));
}

LValue Output::buildFDiv(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildFDiv(builder_, lhs, rhs));
}

LValue Output::buildFCmp(LRealPredicate cond, LValue lhs, LValue rhs) {
  return setInstrDebugLoc(
      v8::internal::tf_llvm::buildFCmp(builder_, cond, lhs, rhs));
}

LValue Output::buildFNeg(LValue value) {
  return setInstrDebugLoc(LLVMBuildFNeg(builder_, value, ""));
}

LValue Output::buildNSWMul(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(LLVMBuildNSWMul(builder_, lhs, rhs, ""));
}

LValue Output::buildShl(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildShl(builder_, lhs, rhs));
}

LValue Output::buildShr(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildLShr(builder_, lhs, rhs));
}

LValue Output::buildSar(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildAShr(builder_, lhs, rhs));
}

LValue Output::buildAnd(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildAnd(builder_, lhs, rhs));
}

LValue Output::buildOr(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildOr(builder_, lhs, rhs));
}

LValue Output::buildXor(LValue lhs, LValue rhs) {
  return setInstrDebugLoc(LLVMBuildXor(builder_, lhs, rhs, ""));
}

LValue Output::buildBr(LBasicBlock bb) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildBr(builder_, bb));
}

LValue Output::buildSwitch(LValue val, LBasicBlock defaultBlock,
                           unsigned cases) {
  return setInstrDebugLoc(LLVMBuildSwitch(builder_, val, defaultBlock, cases));
}

LValue Output::buildCondBr(LValue condition, LBasicBlock taken,
                           LBasicBlock notTaken) {
  return setInstrDebugLoc(
      v8::internal::tf_llvm::buildCondBr(builder_, condition, taken, notTaken));
}

LValue Output::buildRet(LValue ret) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildRet(builder_, ret));
}

LValue Output::buildRetVoid(void) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildRetVoid(builder_));
}

LValue Output::buildCast(LLVMOpcode Op, LLVMValueRef Val, LLVMTypeRef DestTy) {
  return setInstrDebugLoc(LLVMBuildCast(builder_, Op, Val, DestTy, ""));
}

LValue Output::buildPointerCast(LValue val, LType type) {
  return setInstrDebugLoc(LLVMBuildPointerCast(builder_, val, type, ""));
}

LValue Output::buildSelect(LValue condition, LValue taken, LValue notTaken) {
  return setInstrDebugLoc(
      v8::internal::tf_llvm::buildSelect(builder_, condition, taken, notTaken));
}

LValue Output::buildICmp(LIntPredicate cond, LValue left, LValue right) {
  return setInstrDebugLoc(
      v8::internal::tf_llvm::buildICmp(builder_, cond, left, right));
}

LValue Output::buildInlineAsm(LType type, char* asmString, size_t asmStringSize,
                              char* constraintString,
                              size_t constraintStringSize, bool sideEffect) {
  LValue func = LLVMGetInlineAsm(type, asmString, asmStringSize,
                                 constraintString, constraintStringSize,
                                 sideEffect, false, LLVMInlineAsmDialectATT);
  return buildCall(func);
}

LValue Output::buildLoadMagic(LType type, int64_t magic) {
  char kAsmString[] = "ldr $0, =${1:c}";
  char kConstraint[] = "=r,i";
  LValue func = LLVMGetInlineAsm(functionType(type, repo().intPtr), kAsmString,
                                 sizeof(kAsmString) - 1, kConstraint,
                                 sizeof(kConstraint) - 1, false, false,
                                 LLVMInlineAsmDialectATT);
  return buildCall(func, constIntPtr(magic));
}

LValue Output::buildPhi(LType type) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildPhi(builder_, type));
}

LValue Output::buildAlloca(LType type) {
  return setInstrDebugLoc(v8::internal::tf_llvm::buildAlloca(builder_, type));
}

LValue Output::buildGEPWithByteOffset(LValue base, LValue offset,
                                      LType dstType) {
  LType base_type = typeOf(base);
  unsigned base_type_address_space = LLVMGetPointerAddressSpace(base_type);
  unsigned dst_type_address_space = LLVMGetPointerAddressSpace(dstType);
  LValue base_ref8 =
      buildBitCast(base, LLVMPointerType(repo().int8, base_type_address_space));
  LValue offset_value = offset;
  LValue dst_ref8 = LLVMBuildGEP(builder_, base_ref8, &offset_value, 1, "");
  if (base_type_address_space != dst_type_address_space) {
    dst_ref8 = buildCast(LLVMAddrSpaceCast, dst_ref8,
                         LLVMPointerType(repo().int8, dst_type_address_space));
  }
  return buildBitCast(dst_ref8, dstType);
}

LValue Output::buildGEP(LValue base, LValue offset) {
  LValue dst = LLVMBuildGEP(builder_, base, &offset, 1, "");
  return dst;
}

LValue Output::buildBitCast(LValue val, LType type) {
  return v8::internal::tf_llvm::buildBitCast(builder_, val, type);
}

void Output::buildUnreachable() {
  setInstrDebugLoc(v8::internal::tf_llvm::buildUnreachable(builder_));
}

LValue Output::getStatePointFunction(LType callee_type) {
  auto found = statepoint_function_map_.find(callee_type);
  if (found != statepoint_function_map_.end()) return found->second;
  std::vector<LType> wrapped_argument_types;
  wrapped_argument_types.push_back(repo().int64);
  wrapped_argument_types.push_back(repo().int32);
  wrapped_argument_types.push_back(callee_type);
  wrapped_argument_types.push_back(repo().int32);
  wrapped_argument_types.push_back(repo().int32);
  LType function_type =
      functionType(repo().tokenType, wrapped_argument_types.data(),
                   wrapped_argument_types.size(), Variadic);
  char name[256];
  if (!LLVMGetStatepointName(function_type, name, 256)) {
    __builtin_trap();
  }
  LValue function = addExternFunction(state_.module_, name, function_type);
  statepoint_function_map_[callee_type] = function;
  return function;
}

LValue Output::buildExtractValue(LValue aggVal, unsigned index) {
  return setInstrDebugLoc(tf_llvm::buildExtractValue(builder_, aggVal, index));
}

LValue Output::buildCall(LValue function, const LValue* args,
                         unsigned numArgs) {
  return setInstrDebugLoc(LLVMBuildCall(
      builder_, function, const_cast<LValue*>(args), numArgs, ""));
}

LValue Output::buildInvoke(LValue function, const LValue* args,
                           unsigned numArgs, LBasicBlock then,
                           LBasicBlock exception) {
  return setInstrDebugLoc(LLVMBuildInvoke(builder_, function,
                                          const_cast<LValue*>(args), numArgs,
                                          then, exception, ""));
}

LValue Output::buildLandingPad() {
  LValue function = repo().fakePersonalityIntrinsic();
  LType landing_type = repo().tokenType;
  LValue landing_pad =
      LLVMBuildLandingPad(builder_, landing_type, function, 0, "");
  LLVMSetCleanup(landing_pad, true);
  return setInstrDebugLoc(landing_pad);
}

void Output::setLineNumber(int linenum) {
#if defined(FEATURE_SAMPLE_PGO)
  LLVMMetadataRef loc = LLVMDIBuilderCreateDebugLocation(
      state_.context_, linenum, 0, subprogram_, nullptr);
  LValue loc_value = LLVMMetadataAsValue(state_.context_, loc);
  LLVMSetCurrentDebugLocation(builder_, loc_value);
#endif
}

#if defined(FEATURE_SAMPLE_PGO)
static bool ValueKindIsFind(LValue v) {
  switch (LLVMGetValueKind(v)) {
    case LLVMConstantExprValueKind:
    case LLVMConstantIntValueKind:
    case LLVMConstantFPValueKind:
    case LLVMConstantPointerNullValueKind:
      return false;
    default:
      return true;
  }
}
#endif

LValue Output::setInstrDebugLoc(LValue v) {
#if defined(FEATURE_SAMPLE_PGO)
  if (ValueKindIsFind(v)) LLVMSetInstDebugLocation(builder_, v);
#endif  // FEATURE_SAMPLE_PGO
  return v;
}

void Output::finalizeDebugInfo() { LLVMDIBuilderFinalize(di_builder_); }

LValue Output::addFunction(const char* name, LType type) {
  return tf_llvm::addFunction(state_.module_, name, type);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

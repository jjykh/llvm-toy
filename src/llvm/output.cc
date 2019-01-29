#include "src/llvm/output.h"
#include "src/llvm/compiler-state.h"
#include "src/llvm/log.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
Output::Output(CompilerState& state)
    : state_(state),
      repo_(state.context_, state.module_),
      builder_(nullptr),
      prologue_(nullptr),
      root_(nullptr),
      fp_(nullptr),
      lr_(nullptr),
      stack_parameter_count_(0) {}

Output::~Output() { LLVMDisposeBuilder(builder_); }

void Output::initializeBuild(const RegisterParameterDesc& registerParameters,
                             bool v8cc) {
  int len;
  EMASSERT(!builder_);
  EMASSERT(!prologue_);
  builder_ = LLVMCreateBuilderInContext(state_.context_);
  initializeFunction(registerParameters, v8cc);
  // FIXME: Add V8 to LLVM.
  LLVMSetGC(state_.function_, "coreclr");

  prologue_ = appendBasicBlock("Prologue");
  positionToBBEnd(prologue_);
  // build parameters
  char empty[] = "\0";
  char constraint[256];
  if (v8cc) {
    root_ = LLVMGetParam(state_.function_, 10);
    fp_ = LLVMGetParam(state_.function_, 11);
  } else {
    root_ = LLVMGetParam(state_.function_, 5);
    char kEmpty[] = "";
    char kConstraint[] = "={r11}";
    fp_ = buildInlineAsm(functionType(pointerType(repo().ref8)), kEmpty, 0,
                         kConstraint, sizeof(kConstraint) - 1, true);
  }
  for (auto& registerParameter : registerParameters) {
    if (registerParameter.name >= 0) {
      EMASSERT(registerParameter.name < 10);
      LValue rvalue = LLVMGetParam(state_.function_, registerParameter.name);
      registerParameters_.push_back(rvalue);
    } else {
      // callee frame
      if (state_.needs_frame_) {
        LValue gep = buildGEPWithByteOffset(
            fp_, constInt32((1 - registerParameter.name) * sizeof(void*)),
            pointerType(taggedType()));
        registerParameters_.push_back(buildLoad(gep));
      } else {
        LValue rvalue =
            LLVMGetParam(state_.function_,
                         kV8CCRegisterParameterCount + stack_parameter_count_);
        registerParameters_.push_back(rvalue);
      }
      stack_parameter_count_++;
    }
  }
  len = snprintf(constraint, 256, "={%s}", "lr");
  lr_ = buildInlineAsm(functionType(pointerType(repo().ref8)), empty, 0,
                       constraint, len, true);
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
                                     taggedType(),
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
  state_.function_ =
      addFunction(state_.module_, "main",
                  functionType(taggedType(), params_types.data(),
                               params_types.size(), NotVariadic));
  if (v8cc)
    setFunctionCallingConv(state_.function_, LLVMV8CallConv);
  else
    setFunctionCallingConv(state_.function_, LLVMV8SBCallConv);

  if (state_.needs_frame_) {
    LLVMAttributeRef attribute;
    static const char kJSFunctionCall[] = "js-function-call";
    static const char kJSStubCall[] = "js-stub-call";
    switch (state_.prologue_kind_) {
      case PrologueKind::JSFunctionCall:
        LLVMAddTargetDependentFunctionAttr(state_.function_, kJSFunctionCall,
                                           nullptr);
        break;
      case PrologueKind::Stub:
        LLVMAddTargetDependentFunctionAttr(state_.function_, kJSStubCall,
                                           nullptr);
        break;
      default:
        __builtin_trap();
    }
  }
}

LBasicBlock Output::appendBasicBlock(const char* name) {
  return v8::internal::tf_llvm::appendBasicBlock(state_.context_,
                                                 state_.function_, name);
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
LValue Output::constInt1(int i) {
  return v8::internal::tf_llvm::constInt(repo_.int1, i);
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
  return v8::internal::tf_llvm::buildStructGEP(builder_, structVal, field);
}

LValue Output::buildLoad(LValue toLoad) {
  return v8::internal::tf_llvm::buildLoad(builder_, toLoad);
}

LValue Output::buildStore(LValue val, LValue pointer) {
  return v8::internal::tf_llvm::buildStore(builder_, val, pointer);
}

LValue Output::buildNeg(LValue val) {
  return v8::internal::tf_llvm::buildNeg(builder_, val);
}

LValue Output::buildAdd(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildAdd(builder_, lhs, rhs);
}

LValue Output::buildFAdd(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildFAdd(builder_, lhs, rhs);
}

LValue Output::buildNSWAdd(LValue lhs, LValue rhs) {
  return LLVMBuildNSWAdd(builder_, lhs, rhs, "");
}

LValue Output::buildSub(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildSub(builder_, lhs, rhs);
}

LValue Output::buildFSub(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildFSub(builder_, lhs, rhs);
}

LValue Output::buildNSWSub(LValue lhs, LValue rhs) {
  return LLVMBuildNSWSub(builder_, lhs, rhs, "");
}

LValue Output::buildMul(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildMul(builder_, lhs, rhs);
}

LValue Output::buildSRem(LValue lhs, LValue rhs) {
  return LLVMBuildSRem(builder_, lhs, rhs, "");
}

LValue Output::buildSDiv(LValue lhs, LValue rhs) {
  return LLVMBuildSDiv(builder_, lhs, rhs, "");
}

LValue Output::buildFMul(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildFMul(builder_, lhs, rhs);
}

LValue Output::buildFDiv(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildFDiv(builder_, lhs, rhs);
}

LValue Output::buildFCmp(LRealPredicate cond, LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildFCmp(builder_, cond, lhs, rhs);
}

LValue Output::buildFNeg(LValue value) {
  return LLVMBuildFNeg(builder_, value, "");
}

LValue Output::buildNSWMul(LValue lhs, LValue rhs) {
  return LLVMBuildNSWMul(builder_, lhs, rhs, "");
}

LValue Output::buildShl(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildShl(builder_, lhs, rhs);
}

LValue Output::buildShr(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildLShr(builder_, lhs, rhs);
}

LValue Output::buildSar(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildAShr(builder_, lhs, rhs);
}

LValue Output::buildAnd(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildAnd(builder_, lhs, rhs);
}

LValue Output::buildOr(LValue lhs, LValue rhs) {
  return v8::internal::tf_llvm::buildOr(builder_, lhs, rhs);
}

LValue Output::buildXor(LValue lhs, LValue rhs) {
  return LLVMBuildXor(builder_, lhs, rhs, "");
}

LValue Output::buildBr(LBasicBlock bb) {
  return v8::internal::tf_llvm::buildBr(builder_, bb);
}

LValue Output::buildSwitch(LValue val, LBasicBlock defaultBlock,
                           unsigned cases) {
  return LLVMBuildSwitch(builder_, val, defaultBlock, cases);
}

LValue Output::buildCondBr(LValue condition, LBasicBlock taken,
                           LBasicBlock notTaken) {
  return v8::internal::tf_llvm::buildCondBr(builder_, condition, taken,
                                            notTaken);
}

LValue Output::buildRet(LValue ret) {
  return v8::internal::tf_llvm::buildRet(builder_, ret);
}

LValue Output::buildRetVoid(void) {
  return v8::internal::tf_llvm::buildRetVoid(builder_);
}

LValue Output::buildCast(LLVMOpcode Op, LLVMValueRef Val, LLVMTypeRef DestTy) {
  return LLVMBuildCast(builder_, Op, Val, DestTy, "");
}

LValue Output::buildPointerCast(LValue val, LType type) {
  return LLVMBuildPointerCast(builder_, val, type, "");
}

LValue Output::buildSelect(LValue condition, LValue taken, LValue notTaken) {
  return v8::internal::tf_llvm::buildSelect(builder_, condition, taken,
                                            notTaken);
}

LValue Output::buildICmp(LIntPredicate cond, LValue left, LValue right) {
  return v8::internal::tf_llvm::buildICmp(builder_, cond, left, right);
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
  return v8::internal::tf_llvm::buildPhi(builder_, type);
}

LValue Output::buildAlloca(LType type) {
  return v8::internal::tf_llvm::buildAlloca(builder_, type);
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

LValue Output::buildBitCast(LValue val, LType type) {
  return v8::internal::tf_llvm::buildBitCast(builder_, val, type);
}

void Output::buildUnreachable() {
  v8::internal::tf_llvm::buildUnreachable(builder_);
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
  return tf_llvm::buildExtractValue(builder_, aggVal, index);
}

void Output::ensureLR() {
  char constraint[256];
  char empty[] = "\0";
  int len = snprintf(constraint, 256, "{%s}", "lr");
  LValue function = LLVMGetInlineAsm(
      functionType(repo().voidType, pointerType(repo().ref8)), empty, 0,
      constraint, len, true, false, LLVMInlineAsmDialectATT);
  buildCall(function, lr_);
}

void Output::buildReturn(LValue value, LValue pop_count) {
  // FIXME:(UC_linzj) this is ugly, needs to simplify.
  if (state_.needs_frame_) {
    if (!LLVMIsConstant(pop_count)) {
      char asm_content[] =
          "mov sp, $0\n"
          "ldmia sp!, {fp, lr}\n"
          "add sp, sp, r2, lsl #2\n"
          "bx lr\n";
      char constraint[] = "r, {r2}, {r0}";
      LValue func = LLVMGetInlineAsm(functionType(repo().voidType, typeOf(fp_),
                                                  repo().int32, typeOf(value)),
                                     asm_content, sizeof(asm_content) - 1,
                                     constraint, sizeof(constraint) - 1, true,
                                     false, LLVMInlineAsmDialectATT);
      buildCall(func, fp_, pop_count, value);
    } else {
      int pop_count_value = LLVMConstIntGetZExtValue(pop_count);
      int to_pop = pop_count_value + stack_parameter_count_;
      if (to_pop == 0) {
        char asm_content[] =
            "mov sp, $0\n"
            "ldmia sp!, {fp, lr}\n"
            "bx lr\n";
        char constraint[] = "r, {r0}";
        LValue func = LLVMGetInlineAsm(
            functionType(repo().voidType, typeOf(fp_), typeOf(value)),
            asm_content, sizeof(asm_content) - 1, constraint,
            sizeof(constraint) - 1, true, false, LLVMInlineAsmDialectATT);
        buildCall(func, fp_, value);
      } else {
        char asm_content[] =
            "mov sp, $0\n"
            "ldmia sp!, {fp, lr}\n"
            "add sp, sp, $1\n"
            "bx lr\n";
        char constraint[] = "r, i, {r0}";
        LValue func = LLVMGetInlineAsm(
            functionType(repo().voidType, typeOf(fp_), repo().int32,
                         typeOf(value)),
            asm_content, sizeof(asm_content) - 1, constraint,
            sizeof(constraint) - 1, true, false, LLVMInlineAsmDialectATT);
        buildCall(func, fp_, constInt32(to_pop * sizeof(void*)), value);
      }
    }
  } else {
    if (!LLVMIsConstant(pop_count)) {
      char asm_content[] =
          "add sp, sp, r2, lsl #2\n"
          "bx lr\n";
      char constraint[] = "{r2}, {r0}";
      LValue func = LLVMGetInlineAsm(
          functionType(repo().voidType, repo().int32, typeOf(value)),
          asm_content, sizeof(asm_content) - 1, constraint,
          sizeof(constraint) - 1, true, false, LLVMInlineAsmDialectATT);
      buildCall(func, pop_count, value);
    } else {
      // FIXME:(UC_linzj): Should I pass fp_ so that fp stays intact?
      int pop_count_value = LLVMConstIntGetZExtValue(pop_count);
      int to_pop = pop_count_value + stack_parameter_count_;
      if (to_pop == 0) {
        char asm_content[] = "bx lr\n";
        char constraint[] = "{r0}";
        LValue func = LLVMGetInlineAsm(
            functionType(repo().voidType, typeOf(value)), asm_content,
            sizeof(asm_content) - 1, constraint, sizeof(constraint) - 1, true,
            false, LLVMInlineAsmDialectATT);
        buildCall(func, value);
      } else {
        char asm_content[] =
            "add sp, sp, $0\n"
            "bx lr\n";
        char constraint[] = "i, {r0}";
        LValue func = LLVMGetInlineAsm(
            functionType(repo().voidType, repo().int32, typeOf(value)),
            asm_content, sizeof(asm_content) - 1, constraint,
            sizeof(constraint) - 1, true, false, LLVMInlineAsmDialectATT);
        buildCall(func, constInt32(to_pop * sizeof(void*)), value);
      }
    }
  }
  buildUnreachable();
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

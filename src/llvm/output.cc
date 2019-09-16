// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/output.h"
#include <llvm-c/DebugInfo.h>
#include "src/frames.h"
#include "src/llvm/compiler-state.h"
#include "src/llvm/log.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
namespace {
StackFrame::Type GetOutputStackFrameType(Code::Kind kind) {
  switch (kind) {
    case Code::STUB:
    case Code::BYTECODE_HANDLER:
    case Code::BUILTIN:
      return StackFrame::STUB;
    case Code::WASM_FUNCTION:
      return StackFrame::WASM_COMPILED;
    case Code::JS_TO_WASM_FUNCTION:
      return StackFrame::JS_TO_WASM;
    case Code::WASM_TO_JS_FUNCTION:
      return StackFrame::WASM_TO_JS;
    case Code::WASM_INTERPRETER_ENTRY:
      return StackFrame::WASM_INTERPRETER_ENTRY;
    default:
      UNIMPLEMENTED();
      return StackFrame::NONE;
  }
}
}  // namespace
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

// parameter layout: register parameter, stack parameter, float point parameter
void Output::initializeBuild(const RegisterParameterDesc& registerParameters,
                             bool v8cc, bool is_wasm) {
  EMASSERT(!builder_);
  EMASSERT(!prologue_);
  builder_ = LLVMCreateBuilderInContext(state_.context_);
  di_builder_ = LLVMCreateDIBuilderDisallowUnresolved(state_.module_);
  initializeFunction(registerParameters, v8cc, is_wasm);
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
  enum class LateParameterType { Stack, FloatPoint };
  // type, position in stack/double registers, position in parameters;
  std::vector<std::tuple<LateParameterType, int, int>> late_parameters;
  for (auto& registerParameter : registerParameters) {
    if ((registerParameter.type == repo().doubleType) ||
        (registerParameter.type == repo().floatType)) {
      parameters_.emplace_back(nullptr);
      late_parameters.emplace_back(LateParameterType::FloatPoint,
                                   registerParameter.name,
                                   parameters_.size() - 1);
    } else if ((registerParameter.name >= 0)) {
      EMASSERT(registerParameter.name < 10);
      LValue rvalue = LLVMGetParam(state_.function_, registerParameter.name);
      parameters_.emplace_back(rvalue);
    } else {
      // callee frame
      stack_parameter_count_++;
      parameters_.emplace_back(nullptr);
      late_parameters.emplace_back(LateParameterType::Stack,
                                   -registerParameter.name - 1,
                                   parameters_.size() - 1);
    }
  }
  for (auto i = late_parameters.begin(); i != late_parameters.end(); ++i) {
    LValue v;
    auto& tuple = *i;
    switch (std::get<0>(tuple)) {
      case LateParameterType::Stack:
        v = LLVMGetParam(state_.function_,
                         kV8CCRegisterParameterCount + std::get<1>(tuple));
        break;
      case LateParameterType::FloatPoint:
        v = LLVMGetParam(state_.function_, kV8CCRegisterParameterCount +
                                               stack_parameter_count_ +
                                               std::get<1>(tuple));
        break;
    }
    parameters_[std::get<2>(tuple)] = v;
  }
  int v_null_index = 0;
  for (LValue v : parameters_) {
    EMASSERT(v != nullptr);
    v_null_index++;
  }
  bitcast_space_ = buildAlloca(arrayType(repo().int8, 16));
}

void Output::initializeFunction(const RegisterParameterDesc& registerParameters,
                                bool v8cc, bool is_wasm) {
  LType tagged_type = taggedType();
  std::vector<LType> params_types = {tagged_type,
                                     tagged_type,
                                     tagged_type,
                                     tagged_type,
                                     tagged_type,
                                     tagged_type,
                                     tagged_type,
                                     tagged_type,
                                     tagged_type,
                                     repo().ref8,
                                     pointerType(tagged_type),
                                     pointerType(repo().ref8)};
  EMASSERT(params_types.size() == kV8CCRegisterParameterCount);
  std::vector<LType> float_point_parameter_types;
  LType double_type = repo().doubleType;
  LType float_type = repo().floatType;
  for (auto& registerParameter : registerParameters) {
    if ((registerParameter.type == double_type) ||
        (registerParameter.type == float_type)) {
      EMASSERT(float_point_parameter_types.size() <=
               static_cast<size_t>(registerParameter.name));
      // FIXME: could be architecture dependent.
      if (float_point_parameter_types.size() <
          static_cast<size_t>(registerParameter.name)) {
        float_point_parameter_types.resize(
            static_cast<size_t>(registerParameter.name), float_type);
      }
      float_point_parameter_types.emplace_back(registerParameter.type);
    } else if (registerParameter.name >= 0) {
      EMASSERT(registerParameter.name < 10);
      params_types[registerParameter.name] = registerParameter.type;
    } else {
      int slot = -registerParameter.name;
      if (params_types.size() <
          static_cast<size_t>(slot + kV8CCRegisterParameterCount))
        params_types.resize(slot + kV8CCRegisterParameterCount, tagged_type);
      params_types[slot - 1 + kV8CCRegisterParameterCount] =
          registerParameter.type;
    }
  }
  EMASSERT(float_point_parameter_types.size() <= 8);
  params_types.insert(
      params_types.end(),
      std::make_move_iterator(float_point_parameter_types.begin()),
      std::make_move_iterator(float_point_parameter_types.end()));
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
    static const char kJSWASMCall[] = "js-wasm-call";
    switch (state_.prologue_kind_) {
      case PrologueKind::JSFunctionCall:
        LLVMAddTargetDependentFunctionAttr(state_.function_, kJSFunctionCall,
                                           nullptr);
        break;
      case PrologueKind::Stub: {
        char stub_marker[16];
        snprintf(stub_marker, sizeof(stub_marker), "%d",
                 StackFrame::TypeToMarker(GetOutputStackFrameType(
                     static_cast<Code::Kind>(state_.code_kind_))));
        LLVMAddTargetDependentFunctionAttr(state_.function_, kJSStubCall,
                                           stub_marker);
        if (is_wasm) {
          LLVMAddTargetDependentFunctionAttr(state_.function_, kJSWASMCall,
                                             nullptr);
        }
      } break;
      default:
        __builtin_trap();
    }
  }

  char file_name[256];
  int file_name_count = snprintf(file_name, 256, "%s.c", state_.function_name_);
  LLVMMetadataRef file_name_meta = LLVMDIBuilderCreateFile(
      di_builder_, file_name, file_name_count, nullptr, 0);
  LLVMMetadataRef cu = LLVMDIBuilderCreateCompileUnit(
      di_builder_, LLVMDWARFSourceLanguageC, file_name_meta, nullptr, 0, true,
      nullptr, 0, 1, nullptr, 0, LLVMDWARFEmissionLineTablesOnly, 0, false,
      false);
  LLVMMetadataRef subroutine_type = LLVMDIBuilderCreateSubroutineType(
      di_builder_, file_name_meta, nullptr, 0, LLVMDIFlagZero);
  subprogram_ = LLVMDIBuilderCreateFunction(
      di_builder_, cu, state_.function_name_, strlen(state_.function_name_),
      nullptr, 0, file_name_meta, 1, subroutine_type, false, true, 1,
      LLVMDIFlagZero, true);
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

static std::string GetMangledTypeStr(LType* types, size_t ntypes) {
  size_t nlength_used;
  // This shit use strdup.
  char* name = const_cast<char*>(
      LLVMIntrinsicCopyOverloadedName(1, types, ntypes, &nlength_used));
  char* last_dot = strrchr(name, '.');
  std::string r(last_dot);
  free(name);
  return r;
}

LValue Output::getStatePointFunction(LType callee_type) {
  auto found = gc_function_map_.find(callee_type);
  if (found != gc_function_map_.end()) return found->second;
  std::vector<LType> wrapped_argument_types;
  wrapped_argument_types.emplace_back(repo().int64);
  wrapped_argument_types.emplace_back(repo().int32);
  wrapped_argument_types.emplace_back(callee_type);
  wrapped_argument_types.emplace_back(repo().int32);
  wrapped_argument_types.emplace_back(repo().int32);
  LType function_type =
      functionType(repo().tokenType, wrapped_argument_types.data(),
                   wrapped_argument_types.size(), Variadic);
  std::string name("llvm.experimental.gc.statepoint");
  name.append(GetMangledTypeStr(&callee_type, 1));
  LValue function =
      addExternFunction(state_.module_, name.c_str(), function_type);
  gc_function_map_[callee_type] = function;
  return function;
}

LValue Output::getGCResultFunction(LType return_type) {
  auto found = gc_function_map_.find(return_type);
  if (found != gc_function_map_.end()) return found->second;
  std::string name("llvm.experimental.gc.result");
  name.append(GetMangledTypeStr(&return_type, 1));
  LType function_type = functionType(return_type, repo().tokenType);
  LValue function =
      addExternFunction(state_.module_, name.c_str(), function_type);
  gc_function_map_[return_type] = function;
  return function;
}

LValue Output::buildExtractValue(LValue aggVal, unsigned index) {
  return tf_llvm::buildExtractValue(builder_, aggVal, index);
}

LValue Output::buildInsertValue(LValue aggVal, unsigned index, LValue value) {
  return LLVMBuildInsertValue(builder_, aggVal, value, index, "");
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
  LValue function = tf_llvm::addFunction(state_.module_, name, type);
  AddFunctionCommonAttr(function);
  return function;
}

LType Output::getLLVMTypeFromMachineType(const MachineType& mt) {
  switch (mt.representation()) {
    case MachineRepresentation::kWord8:
      return repo().int8;
    case MachineRepresentation::kWord16:
      return repo().int16;
    case MachineRepresentation::kWord32:
      return repo().int32;
    case MachineRepresentation::kWord64:
      return repo().int64;
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
      return repo().taggedType;
    case MachineRepresentation::kFloat64:
      return repo().doubleType;
    case MachineRepresentation::kFloat32:
      return repo().floatType;
    default:
      UNREACHABLE();
  }
}

void Output::AddFunctionCommonAttr(LValue function) {
  // arm jump tables are slow.
  static const char kNoJumpTables[] = "no-jump-tables";
  static const char kTrue[] = "true";
  LLVMAddTargetDependentFunctionAttr(function, kNoJumpTables, kTrue);

  static const char kFS[] = "target-features";
  static const char kFSValue[] =
      "+armv7-a,+dsp,+neon,+vfp3,-crypto,-d16,-fp-armv8,-fp-only-sp,-fp16,"
      "-thumb-mode,-vfp4";
  LLVMAddTargetDependentFunctionAttr(function, kFS, kFSValue);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

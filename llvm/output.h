#ifndef OUTPUT_H
#define OUTPUT_H
#include <string>
#include <unordered_map>
#include <vector>
#include "intrinsic-repository.h"
namespace jit {
struct CompilerState;
struct PatchDesc;
struct RegisterParameter {
  std::string name;
  LType type;
};

using RegisterParameterDesc = std::vector<RegisterParameter>;

class Output {
 public:
  Output(CompilerState& state);
  ~Output();
  void initializeBuild(const RegisterParameterDesc&);
  LBasicBlock appendBasicBlock(const char* name = nullptr);
  void positionToBBEnd(LBasicBlock);
  LValue constInt1(int);
  LValue constInt32(int);
  LValue constIntPtr(intptr_t);
  LValue constInt64(long long);
  LValue buildStructGEP(LValue structVal, unsigned field);
  LValue buildGEPWithByteOffset(LValue base, LValue offset, LType dstType);
  LValue buildLoad(LValue toLoad);
  LValue buildStore(LValue val, LValue pointer);
  LValue buildAdd(LValue lhs, LValue rhs);
  LValue buildNSWAdd(LValue lhs, LValue rhs);
  LValue buildSub(LValue lhs, LValue rhs);
  LValue buildNSWSub(LValue lhs, LValue rhs);
  LValue buildMul(LValue lhs, LValue rhs);
  LValue buildNSWMul(LValue lhs, LValue rhs);
  LValue buildShl(LValue lhs, LValue rhs);
  LValue buildShr(LValue lhs, LValue rhs);
  LValue buildSar(LValue lhs, LValue rhs);
  LValue buildAnd(LValue lhs, LValue rhs);
  LValue buildBr(LBasicBlock bb);
  LValue buildCondBr(LValue condition, LBasicBlock taken, LBasicBlock notTaken);
  LValue buildRet(LValue ret);
  LValue buildRetVoid(void);
  LValue buildSelect(LValue condition, LValue taken, LValue notTaken);
  LValue buildICmp(LIntPredicate cond, LValue left, LValue right);
  LValue buildPhi(LType type);

  inline LValue buildCall(LValue function, const LValue* args,
                          unsigned numArgs) {
    return LLVMBuildCall(builder_, function, const_cast<LValue*>(args), numArgs,
                         "");
  }

  template <typename VectorType>
  inline LValue buildCall(LValue function, const VectorType& vector) {
    return buildCall(function, vector.begin(), vector.size());
  }
  inline LValue buildCall(LValue function) {
    return buildCall(function, nullptr, 0U);
  }
  inline LValue buildCall(LValue function, LValue arg1) {
    return buildCall(function, &arg1, 1);
  }
  template <typename... Args>
  LValue buildCall(LValue function, LValue arg1, Args... args) {
    LValue argsArray[] = {arg1, args...};
    return buildCall(function, argsArray, sizeof(argsArray) / sizeof(LValue));
  }

  LValue buildCast(LLVMOpcode Op, LLVMValueRef Val, LLVMTypeRef DestTy);
  LValue buildBitCast(LValue val, LType type);
  LValue buildPointerCast(LValue val, LType type);
  LValue getStatePointFunction(LType callee_type);

  void buildDirectPatch(uintptr_t where);
  void buildIndirectPatch(LValue where);
  void buildAssistPatch(LValue where);
  LValue buildInlineAsm(LType, char*, size_t, char*, size_t, bool);
  void buildUnreachable();
  void buildClobberRegister();

  inline IntrinsicRepository& repo() { return repo_; }
  inline LBasicBlock prologue() const { return prologue_; }
  inline LType taggedType() const { return repo_.taggedType; }
  inline LValue registerParameter(int i) { return registerParameters_[i]; }
  inline LValue context() {
    return registerParameters_[registerParameters_.size() - 1];
  }
  inline LValue root() { return root_; }
  inline LValue fp() { return fp_; }

 private:
  void buildGetArg();
  void buildPatchCommon(LValue where, const PatchDesc& desc, size_t patchSize);

  CompilerState& state_;
  IntrinsicRepository repo_;
  LBuilder builder_;
  LBasicBlock prologue_;
  LValue root_;
  LValue fp_;
  LValue clobber_func_;
  std::vector<LValue> registerParameters_;
  std::unordered_map<LType, LValue> statepoint_function_map_;
  uint32_t stackMapsId_;
};
}  // namespace jit
#endif /* OUTPUT_H */

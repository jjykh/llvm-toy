#ifndef OUTPUT_H
#define OUTPUT_H
#include <string>
#include <vector>
#include "IntrinsicRepository.h"
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
  LValue constInt32(int);
  LValue constIntPtr(intptr_t);
  LValue constInt64(long long);
  LValue buildStructGEP(LValue structVal, unsigned field);
  LValue buildGEPWithByteOffset(LValue base, int offset, LType dstType);
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
    return LLVMBuildCall(m_builder, function, const_cast<LValue*>(args),
                         numArgs, "");
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

  void buildDirectPatch(uintptr_t where);
  void buildIndirectPatch(LValue where);
  void buildAssistPatch(LValue where);
  LValue buildInlineAsm(LType, char*, size_t, char*, size_t, bool);

  inline IntrinsicRepository& repo() { return m_repo; }
  inline LBasicBlock prologue() const { return m_prologue; }
  inline LType taggedType() const { return m_taggedType; }
  inline LValue registerParameter(int i) { return m_registerParameters[i]; }
  inline LValue root() { return m_root; }
  inline LValue fp() { return m_fp; }

 private:
  void buildGetArg();
  void buildPatchCommon(LValue where, const PatchDesc& desc, size_t patchSize);

  CompilerState& m_state;
  IntrinsicRepository m_repo;
  LBuilder m_builder;
  LBasicBlock m_prologue;
  LType m_taggedType;
  LValue m_root;
  LValue m_fp;
  std::vector<LValue> m_registerParameters;
  uint32_t m_stackMapsId;
};
}  // namespace jit
#endif /* OUTPUT_H */

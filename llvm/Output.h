#ifndef OUTPUT_H
#define OUTPUT_H
#include "IntrinsicRepository.h"
namespace jit {
class CompilerState;
class Output {
public:
    Output(CompilerState& state);
    ~Output();
    LBasicBlock appendBasicBlock(const char* name = nullptr);
    void positionToBBEnd(LBasicBlock);
    LValue constInt32(int);
    LValue constInt64(long long);
    LValue buildStructGEP(LValue structVal, unsigned field);
    LValue buildLoad(LValue toLoad);
    LValue buildStore(LValue val, LValue pointer);
    LValue buildAdd(LValue lhs, LValue rhs);
    LValue buildBr(LBasicBlock bb);
    LValue buildRet(LValue ret);
    LValue buildRetVoid(void);

    inline LValue buildCall(LValue function, const LValue* args, unsigned numArgs)
    {
        return llvmAPI->BuildCall(m_builder, function, const_cast<LValue*>(args), numArgs, "");
    }

    template <typename VectorType>
    inline LValue buildCall(LValue function, const VectorType& vector)
    {
        return buildCall(function, vector.begin(), vector.size());
    }
    inline LValue buildCall(LValue function)
    {
        return buildCall(function, nullptr, 0U);
    }
    inline LValue buildCall(LValue function, LValue arg1)
    {
        return buildCall(function, &arg1, 1);
    }
    template <typename... Args>
    LValue buildCall(LValue function, LValue arg1, Args... args)
    {
        LValue argsArray[] = { arg1, args... };
        return buildCall(function, argsArray, sizeof(argsArray) / sizeof(LValue));
    }

    LValue buildCast(LLVMOpcode Op, LLVMValueRef Val, LLVMTypeRef DestTy);

    void buildChainPatch(void* where);
    void buildXIndirectPatch(LValue where);

    inline IntrinsicRepository& repo() { return m_repo; }
    inline LType argType() const { return m_argType; }
    inline LBasicBlock prologue() const { return m_prologue; }
    inline LValue arg() const { return m_arg; }
private:
    LValue buildGetArgPatch();

    CompilerState& m_state;
    IntrinsicRepository m_repo;
    LBuilder m_builder;
    LType m_argType;
    LBasicBlock m_prologue;
    LValue m_arg;
    uint32_t m_stackMapsId;
};
}
#endif /* OUTPUT_H */
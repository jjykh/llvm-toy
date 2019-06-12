// Copyright 2019 UCWeb Co., Ltd.

#ifndef TFVISITOR_H
#define TFVISITOR_H
#include <stdint.h>
#include <string>
#include <vector>
#include "src/machine-type.h"
namespace v8 {
namespace internal {
namespace tf_llvm {
using OperandsVector = std::vector<int>;
using RegistersForOperands = std::vector<int>;
struct CallDescriptor {
  RegistersForOperands registers_for_operands;
  size_t return_count;
};

#define INSTRUCTIONS(V)                                                       \
  V(Parameter, (int id, int pid))                                             \
  V(Return, (int id, int pop_count, const OperandsVector& operands))          \
  V(LoadParentFramePointer, (int id))                                         \
  V(LoadFramePointer, (int id))                                               \
  V(LoadStackPointer, (int id))                                               \
  V(DebugBreak, (int id))                                                     \
  V(Int32Constant, (int id, int32_t value))                                   \
  V(Float64SilenceNaN, (int id, int value))                                   \
  V(Identity, (int id, int value))                                            \
  V(Load, (int id, MachineRepresentation rep, MachineSemantic semantic,       \
           int base, int offset))                                             \
  V(Store, (int id, MachineRepresentation rep, WriteBarrierKind barrier,      \
            int base, int offset, int value))                                 \
  V(BitcastWordToTagged, (int id, int e))                                     \
  V(ChangeInt32ToFloat64, (int id, int e))                                    \
  V(ChangeFloat32ToFloat64, (int id, int e))                                  \
  V(ChangeUint32ToFloat64, (int id, int e))                                   \
  V(ChangeFloat64ToInt32, (int id, int e))                                    \
  V(ChangeFloat64ToUint32, (int id, int e))                                   \
  V(ChangeFloat64ToUint64, (int id, int e))                                   \
  V(BitcastInt32ToFloat32, (int id, int e))                                   \
  V(BitcastFloat32ToInt32, (int id, int e))                                   \
  V(TruncateFloat64ToWord32, (int id, int e))                                 \
  V(TruncateFloat64ToFloat32, (int id, int e))                                \
  V(RoundFloat64ToInt32, (int id, int e))                                     \
  V(Float64ExtractHighWord32, (int id, int e))                                \
  V(Float64ExtractLowWord32, (int id, int e))                                 \
  V(RoundInt32ToFloat32, (int id, int e))                                     \
  V(Projection, (int id, int e, int index))                                   \
  V(Int32Add, (int id, int e1, int e2))                                       \
  V(Int32AddWithOverflow, (int id, int e1, int e2))                           \
  V(Int32Sub, (int id, int e1, int e2))                                       \
  V(Int32SubWithOverflow, (int id, int e1, int e2))                           \
  V(Int32Mul, (int id, int e1, int e2))                                       \
  V(Int32MulWithOverflow, (int id, int e1, int e2))                           \
  V(Int32Div, (int id, int e1, int e2))                                       \
  V(Int32Mod, (int id, int e1, int e2))                                       \
  V(Float64InsertLowWord32, (int id, int e1, int e2))                         \
  V(Float64InsertHighWord32, (int id, int e1, int e2))                        \
  V(Int32LessThanOrEqual, (int id, int e1, int e2))                           \
  V(Int32LessThan, (int id, int e1, int e2))                                  \
  V(Uint32LessThanOrEqual, (int id, int e1, int e2))                          \
  V(Uint32LessThan, (int id, int e1, int e2))                                 \
  V(Word32Shl, (int id, int e1, int e2))                                      \
  V(Word32Shr, (int id, int e1, int e2))                                      \
  V(Word32Sar, (int id, int e1, int e2))                                      \
  V(Word32Mul, (int id, int e1, int e2))                                      \
  V(Word32And, (int id, int e1, int e2))                                      \
  V(Word32Or, (int id, int e1, int e2))                                       \
  V(Word32Xor, (int id, int e1, int e2))                                      \
  V(Word32Equal, (int id, int e1, int e2))                                    \
  V(Word32Clz, (int id, int e))                                               \
  V(Float64Add, (int id, int e1, int e2))                                     \
  V(Float64Sub, (int id, int e1, int e2))                                     \
  V(Float64Mul, (int id, int e1, int e2))                                     \
  V(Float64Div, (int id, int e1, int e2))                                     \
  V(Float64Mod, (int id, int e1, int e2))                                     \
  V(Float64LessThan, (int id, int e1, int e2))                                \
  V(Float64LessThanOrEqual, (int id, int e1, int e2))                         \
  V(Float64Equal, (int id, int e1, int e2))                                   \
  V(Float64Neg, (int id, int e))                                              \
  V(Float64Abs, (int id, int e))                                              \
  V(Branch, (int id, int cmp, int btrue, int bfalse))                         \
  V(Switch, (int id, int val, const OperandsVector& blocks))                  \
  V(IfValue, (int id, int val))                                               \
  V(IfDefault, (int id))                                                      \
  V(IfException, (int id))                                                    \
  V(HeapConstant, (int id, int64_t magic))                                    \
  V(SmiConstant, (int id, void* smi_value))                                   \
  V(Float64Constant, (int id, double value))                                  \
  V(Root, (int id, int index))                                                \
  V(CodeForCall, (int id, int64_t magic))                                     \
  V(ExternalConstant, (int id, int64_t magic))                                \
  V(Phi, (int id, MachineRepresentation rep, const OperandsVector& operands)) \
  V(Call, (int id, bool code, const CallDescriptor& call_desc,                \
           const OperandsVector& operands))                                   \
  V(Invoke, (int id, bool code, const CallDescriptor& call_desc,              \
             const OperandsVector& operands, int then, int exception))        \
  V(CallWithCallerSavedRegisters, (int id, const OperandsVector& operands))   \
  V(TailCall, (int id, bool code, const CallDescriptor& call_desc,            \
               const OperandsVector& operands))

class TFVisitor {
 public:
  virtual ~TFVisitor() = default;
  virtual void VisitBlock(int id, bool is_deferred,
                          const OperandsVector& predecessors) = 0;
  virtual void VisitGoto(int bid) = 0;
#define DECL_METHOD(name, signature) virtual void Visit##name signature = 0;

  INSTRUCTIONS(DECL_METHOD)
#undef DECL_METHOD
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // TFVISITOR_H

// Copyright 2019 UCWeb Co., Ltd.

#ifndef TFVISITOR_H
#define TFVISITOR_H
#include <stdint.h>
#include <string>
#include <vector>
#include "src/codegen/machine-type.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/roots/roots.h"
namespace v8 {
namespace internal {
namespace tf_llvm {
using OperandsVector = std::vector<int>;
using RegistersForOperands = std::vector<int>;
using ReturnTypes = std::vector<MachineType>;
struct CallDescriptor {
  RegistersForOperands registers_for_operands;
  ReturnTypes return_types;
};

enum class CallMode { kCode, kAddress, kBuiltin };
enum class BranchHint { kNone, kTrue, kFalse };

#define INSTRUCTIONS(V)                                                       \
  V(Parameter, (int id, int pid))                                             \
  V(Return, (int id, int pop_count, const OperandsVector& operands))          \
  V(LoadParentFramePointer, (int id))                                         \
  V(LoadFramePointer, (int id))                                               \
  V(DebugBreak, (int id))                                                     \
  V(StackPointerGreaterThan, (int id, int value))                             \
  V(TrapIf, (int id, int value))                                              \
  V(TrapUnless, (int id, int value))                                          \
  V(Int32Constant, (int id, int32_t value))                                   \
  V(Int64Constant, (int id, int64_t value))                                   \
  V(RelocatableInt32Constant, (int id, int32_t value, int rmode))             \
  V(Float64SilenceNaN, (int id, int value))                                   \
  V(Identity, (int id, int value))                                            \
  V(Load, (int id, MachineRepresentation rep, MachineSemantic semantic,       \
           int base, int offset))                                             \
  V(Store,                                                                    \
    (int id, MachineRepresentation rep, compiler::WriteBarrierKind barrier,   \
     int base, int offset, int value))                                        \
  V(UnalignedLoad, (int id, MachineRepresentation rep, int base, int offset)) \
  V(UnalignedStore,                                                           \
    (int id, MachineRepresentation rep, int base, int offset, int value))     \
  V(StackSlot, (int id, int size, int alignment))                             \
  V(BitcastTaggedToWord, (int id, int e))                                     \
  V(BitcastWordToTagged, (int id, int e))                                     \
  V(ChangeInt32ToFloat64, (int id, int e))                                    \
  V(ChangeFloat32ToFloat64, (int id, int e))                                  \
  V(ChangeUint32ToFloat64, (int id, int e))                                   \
  V(ChangeFloat64ToInt32, (int id, int e))                                    \
  V(ChangeFloat64ToUint32, (int id, int e))                                   \
  V(ChangeFloat64ToUint64, (int id, int e))                                   \
  V(ChangeUint32ToUint64, (int id, int e))                                    \
  V(ChangeInt32ToInt64, (int id, int e))                                      \
  V(BitcastInt32ToFloat32, (int id, int e))                                   \
  V(BitcastInt64ToFloat64, (int id, int e))                                   \
  V(BitcastFloat32ToInt32, (int id, int e))                                   \
  V(BitcastFloat64ToInt64, (int id, int e))                                   \
  V(TruncateFloat32ToInt32, (int id, int e))                                  \
  V(TruncateFloat32ToUint32, (int id, int e))                                 \
  V(TruncateFloat64ToWord32, (int id, int e))                                 \
  V(TruncateInt64ToWord32, (int id, int e))                                   \
  V(TruncateFloat64ToFloat32, (int id, int e))                                \
  V(TruncateFloat64ToUint32, (int id, int e))                                 \
  V(RoundFloat64ToInt32, (int id, int e))                                     \
  V(Float64ExtractHighWord32, (int id, int e))                                \
  V(Float64ExtractLowWord32, (int id, int e))                                 \
  V(RoundInt32ToFloat32, (int id, int e))                                     \
  V(RoundUint32ToFloat32, (int id, int e))                                    \
  V(Projection, (int id, int e, int index))                                   \
  V(Int32Add, (int id, int e1, int e2))                                       \
  V(Int64Add, (int id, int e1, int e2))                                       \
  V(Int32AddWithOverflow, (int id, int e1, int e2))                           \
  V(Int32Sub, (int id, int e1, int e2))                                       \
  V(Int64Sub, (int id, int e1, int e2))                                       \
  V(Int32SubWithOverflow, (int id, int e1, int e2))                           \
  V(Int32Mul, (int id, int e1, int e2))                                       \
  V(Int32MulWithOverflow, (int id, int e1, int e2))                           \
  V(Int32Div, (int id, int e1, int e2))                                       \
  V(Int32Mod, (int id, int e1, int e2))                                       \
  V(Uint32Div, (int id, int e1, int e2))                                      \
  V(Uint32Mod, (int id, int e1, int e2))                                      \
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
  V(Word32Ror, (int id, int e1, int e2))                                      \
  V(Word32Equal, (int id, int e1, int e2))                                    \
  V(Word32Clz, (int id, int e))                                               \
  V(Word64Clz, (int id, int e))                                               \
  V(Word64Shl, (int id, int e1, int e2))                                      \
  V(Word64Shr, (int id, int e1, int e2))                                      \
  V(Word64Sar, (int id, int e1, int e2))                                      \
  V(Word64And, (int id, int e1, int e2))                                      \
  V(Word64Or, (int id, int e1, int e2))                                       \
  V(Word64Xor, (int id, int e1, int e2))                                      \
  V(Word64Equal, (int id, int e1, int e2))                                    \
  V(Int64Mul, (int id, int e1, int e2))                                       \
  V(Int64LessThanOrEqual, (int id, int e1, int e2))                           \
  V(Int64LessThan, (int id, int e1, int e2))                                  \
  V(Uint64LessThanOrEqual, (int id, int e1, int e2))                          \
  V(Uint64LessThan, (int id, int e1, int e2))                                 \
  V(Float64Add, (int id, int e1, int e2))                                     \
  V(Float64Sub, (int id, int e1, int e2))                                     \
  V(Float64Mul, (int id, int e1, int e2))                                     \
  V(Float64Div, (int id, int e1, int e2))                                     \
  V(Float64Mod, (int id, int e1, int e2))                                     \
  V(Float32Sqrt, (int id, int e))                                             \
  V(Float64Sqrt, (int id, int e))                                             \
  V(Float64LessThan, (int id, int e1, int e2))                                \
  V(Float64LessThanOrEqual, (int id, int e1, int e2))                         \
  V(Float64Equal, (int id, int e1, int e2))                                   \
  V(Float64Neg, (int id, int e))                                              \
  V(Float64Abs, (int id, int e))                                              \
  V(Float32Abs, (int id, int e))                                              \
  V(Float32Equal, (int id, int e1, int e2))                                   \
  V(Float32LessThan, (int id, int e1, int e2))                                \
  V(Float32LessThanOrEqual, (int id, int e1, int e2))                         \
  V(Float32Add, (int id, int e1, int e2))                                     \
  V(Float32Sub, (int id, int e1, int e2))                                     \
  V(Float32Mul, (int id, int e1, int e2))                                     \
  V(Float32Div, (int id, int e1, int e2))                                     \
  V(Float32Neg, (int id, int e))                                              \
  V(Float32Max, (int id, int e1, int e2))                                     \
  V(Float32Min, (int id, int e1, int e2))                                     \
  V(Int32PairAdd, (int id, int e0, int e1, int e2, int e3))                   \
  V(Int32PairSub, (int id, int e0, int e1, int e2, int e3))                   \
  V(Int32PairMul, (int id, int e0, int e1, int e2, int e3))                   \
  V(Word32PairShl, (int id, int e0, int e1, int e2))                          \
  V(Word32PairShr, (int id, int e0, int e1, int e2))                          \
  V(Word32PairSar, (int id, int e0, int e1, int e2))                          \
  V(Branch, (int id, int cmp, int btrue, int bfalse, BranchHint))             \
  V(Switch, (int id, int val, const OperandsVector& blocks))                  \
  V(IfValue, (int id, int val))                                               \
  V(IfDefault, (int id))                                                      \
  V(IfException, (int id))                                                    \
  V(AbortCSAAssert, (int id))                                                 \
  V(HeapConstant, (int id, uintptr_t magic))                                  \
  V(SmiConstant, (int id, uintptr_t smi_value))                               \
  V(Float64Constant, (int id, double value))                                  \
  V(Float32Constant, (int id, double value))                                  \
  V(Root, (int id, RootIndex index))                                          \
  V(RootRelative, (int id, int offset, bool tagged))                          \
  V(RootOffset, (int id, int offset))                                         \
  V(LoadFromConstantTable, (int id, int constant_index))                      \
  V(CodeForCall, (int id, uintptr_t magic, bool relative))                    \
  V(ExternalConstant, (int id, uintptr_t magic))                              \
  V(Phi, (int id, MachineRepresentation rep, const OperandsVector& operands)) \
  V(Call, (int id, CallMode mode, const CallDescriptor& call_desc,            \
           const OperandsVector& operands))                                   \
  V(Invoke, (int id, CallMode mode, const CallDescriptor& call_desc,          \
             const OperandsVector& operands, int then, int exception))        \
  V(CallWithCallerSavedRegisters,                                             \
    (int id, const OperandsVector& operands, bool save_fp))                   \
  V(TailCall, (int id, CallMode mode, const CallDescriptor& call_desc,        \
               const OperandsVector& operands))

class TFVisitor {
 public:
  virtual ~TFVisitor() = default;
  virtual void VisitBlock(int id, bool is_deferred,
                          const OperandsVector& predecessors) = 0;
  virtual void VisitGoto(int bid) = 0;
  virtual void SetSourcePosition(int line, const char* filename) {}

#define DECL_METHOD(name, signature) virtual void Visit##name signature = 0;

  INSTRUCTIONS(DECL_METHOD)
#undef DECL_METHOD
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // TFVISITOR_H

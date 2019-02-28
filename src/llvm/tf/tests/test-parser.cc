#include <stdlib.h>
#include <iostream>
#include "src/llvm/tf/tf-parser.h"
#define UNREACHABLE() __builtin_unreachable()

namespace v8 {
namespace internal {
namespace tf_llvm {
class TestParserVisitor : public TFVisitor {
  void VisitBlock(int id, bool is_deferred,
                  const OperandsVector& predecessors) override;
  void VisitGoto(int bid) override;
  void VisitParameter(int id, int pid) override;
  void VisitLoadParentFramePointer(int id) override;
  void VisitLoadFramePointer(int id) override;
  void VisitLoadStackPointer(int id) override;
  void VisitDebugBreak(int id) override;
  void VisitInt32Constant(int id, int32_t value) override;
  void VisitFloat64SilenceNaN(int id, int value) override;
  void VisitIdentity(int id, int value) override;
  void VisitSmiConstant(int id, void* value) override;
  void VisitLoad(int id, MachineRepresentation rep, MachineSemantic semantic,
                 int base, int offset) override;
  void VisitStore(int id, MachineRepresentation rep, WriteBarrierKind barrier,
                  int base, int offset, int value) override;
  void VisitBitcastWordToTagged(int id, int e) override;
  void VisitChangeInt32ToFloat64(int id, int e) override;
  void VisitChangeFloat32ToFloat64(int id, int e) override;
  void VisitChangeUint32ToFloat64(int id, int e) override;
  void VisitTruncateFloat64ToWord32(int id, int e) override;
  void VisitTruncateFloat64ToFloat32(int id, int e) override;
  void VisitRoundFloat64ToInt32(int id, int e) override;
  void VisitFloat64ExtractHighWord32(int id, int e) override;
  void VisitRoundInt32ToFloat32(int id, int e) override;
  void VisitInt32Add(int id, int e1, int e2) override;
  void VisitInt32AddWithOverflow(int id, int e1, int e2) override;
  void VisitInt32SubWithOverflow(int id, int e1, int e2) override;
  void VisitInt32MulWithOverflow(int id, int e1, int e2) override;
  void VisitInt32Sub(int id, int e1, int e2) override;
  void VisitInt32Mul(int id, int e1, int e2) override;
  void VisitInt32Div(int id, int e1, int e2) override;
  void VisitInt32Mod(int id, int e1, int e2) override;
  void VisitWord32Shl(int id, int e1, int e2) override;
  void VisitWord32Xor(int id, int e1, int e2) override;
  void VisitWord32Shr(int id, int e1, int e2) override;
  void VisitWord32Sar(int id, int e1, int e2) override;
  void VisitWord32Mul(int id, int e1, int e2) override;
  void VisitWord32And(int id, int e1, int e2) override;
  void VisitWord32Or(int id, int e1, int e2) override;
  void VisitWord32Equal(int id, int e1, int e2) override;
  void VisitWord32Clz(int id, int e) override;
  void VisitFloat64Add(int id, int e1, int e2) override;
  void VisitFloat64Sub(int id, int e1, int e2) override;
  void VisitFloat64Mul(int id, int e1, int e2) override;
  void VisitFloat64Div(int id, int e1, int e2) override;
  void VisitFloat64LessThan(int id, int e1, int e2) override;
  void VisitFloat64LessThanOrEqual(int id, int e1, int e2) override;
  void VisitFloat64Equal(int id, int e1, int e2) override;
  void VisitFloat64Mod(int id, int e1, int e2) override;
  void VisitFloat64Neg(int id, int e) override;
  void VisitFloat64Abs(int id, int e) override;
  void VisitInt32LessThanOrEqual(int id, int e1, int e2) override;
  void VisitInt32LessThan(int id, int e1, int e2) override;
  void VisitUint32LessThanOrEqual(int id, int e1, int e2) override;
  void VisitUint32LessThan(int id, int e1, int e2) override;
  void VisitBranch(int id, int cmp, int btrue, int bfalse) override;
  void VisitSwitch(int id, int val, const OperandsVector& blocks) override;
  void VisitIfValue(int id, int val) override;
  void VisitIfDefault(int id) override;
  void VisitHeapConstant(int id, int64_t magic) override;
  void VisitFloat64Constant(int id, double) override;
  void VisitRoot(int id, int index) override;
  void VisitCodeForCall(int id, int64_t) override;
  void VisitProjection(int id, int e, int index) override;
  void VisitExternalConstant(int id, int64_t magic) override;
  void VisitPhi(int id, MachineRepresentation rep,
                const OperandsVector& operands) override;
  void VisitCall(int id, bool code, const CallDescriptor&,
                 const OperandsVector& operands) override;
  void VisitInvoke(int id, bool code, const CallDescriptor&,
                   const OperandsVector& operands, int, int) override;
  void VisitCallWithCallerSavedRegisters(
      int id, const OperandsVector& operands) override;
  void VisitTailCall(int id, bool code,
                     const CallDescriptor& registers_for_operands,
                     const OperandsVector& operands) override;
  void VisitReturn(int id, int pop_count,
                   const OperandsVector& operands) override;
};

inline std::ostream& operator<<(std::ostream& os, WriteBarrierKind kind) {
  switch (kind) {
    case kNoWriteBarrier:
      return os << "NoWriteBarrier";
    case kMapWriteBarrier:
      return os << "MapWriteBarrier";
    case kPointerWriteBarrier:
      return os << "PointerWriteBarrier";
    case kFullWriteBarrier:
      return os << "FullWriteBarrier";
  }
  UNREACHABLE();
}

const char* MachineReprToString(MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kNone:
      return "kMachNone";
    case MachineRepresentation::kBit:
      return "kRepBit";
    case MachineRepresentation::kWord8:
      return "kRepWord8";
    case MachineRepresentation::kWord16:
      return "kRepWord16";
    case MachineRepresentation::kWord32:
      return "kRepWord32";
    case MachineRepresentation::kWord64:
      return "kRepWord64";
    case MachineRepresentation::kFloat32:
      return "kRepFloat32";
    case MachineRepresentation::kFloat64:
      return "kRepFloat64";
    case MachineRepresentation::kSimd128:
      return "kRepSimd128";
    case MachineRepresentation::kTaggedSigned:
      return "kRepTaggedSigned";
    case MachineRepresentation::kTaggedPointer:
      return "kRepTaggedPointer";
    case MachineRepresentation::kTagged:
      return "kRepTagged";
  }
  UNREACHABLE();
}

std::ostream& operator<<(std::ostream& os, MachineSemantic type) {
  switch (type) {
    case MachineSemantic::kNone:
      return os << "kMachNone";
    case MachineSemantic::kBool:
      return os << "kTypeBool";
    case MachineSemantic::kInt32:
      return os << "kTypeInt32";
    case MachineSemantic::kUint32:
      return os << "kTypeUint32";
    case MachineSemantic::kInt64:
      return os << "kTypeInt64";
    case MachineSemantic::kUint64:
      return os << "kTypeUint64";
    case MachineSemantic::kNumber:
      return os << "kTypeNumber";
    case MachineSemantic::kAny:
      return os << "kTypeAny";
  }
  UNREACHABLE();
}

inline std::ostream& operator<<(std::ostream& os,
                                const OperandsVector& operands) {
  os << "(";
  for (int operand : operands) {
    os << operand << ", ";
  }
  os << ")";
  return os;
}

using namespace std;
void TestParserVisitor::VisitBlock(int id, bool is_deferred,
                                   const OperandsVector& predecessors) {
  cout << id << ":"
       << "VisitBlock: " << id << "; is_deferred" << is_deferred
       << "; predecessors: " << predecessors << endl;
}

void TestParserVisitor::VisitGoto(int bid) {
  cout << "VisitGoto " << bid << endl;
}

void TestParserVisitor::VisitParameter(int id, int pid) {
  cout << id << ":"
       << "VisitParameter " << pid << endl;
}

void TestParserVisitor::VisitLoadParentFramePointer(int id) {
  cout << id << ":"
       << "LoadParentFramePointer " << id << endl;
}

void TestParserVisitor::VisitLoadFramePointer(int id) {
  cout << id << ":"
       << "LoadFramePointer " << id << endl;
}

void TestParserVisitor::VisitLoadStackPointer(int id) {
  cout << id << ":"
       << "LoadStackPointer " << id << endl;
}

void TestParserVisitor::VisitDebugBreak(int id) {
  cout << id << ":"
       << "VisitDebugBreak " << id << endl;
}

void TestParserVisitor::VisitInt32Constant(int id, int32_t value) {
  cout << id << ":"
       << "VisitInt32Constant " << value << endl;
}

void TestParserVisitor::VisitFloat64SilenceNaN(int id, int value) {
  cout << id << ":"
       << "VisitFloat64SilenceNaN " << value << endl;
}

void TestParserVisitor::VisitIdentity(int id, int value) {
  cout << id << ":"
       << "VisitIdentity " << value << endl;
}

void TestParserVisitor::VisitLoad(int id, MachineRepresentation rep,
                                  MachineSemantic semantic, int base,
                                  int offset) {
  cout << id << ":"
       << "VisitLoad " << MachineReprToString(rep) << " " << semantic
       << " base: " << base << "; offset:" << offset << endl;
}

void TestParserVisitor::VisitStore(int id, MachineRepresentation rep,
                                   WriteBarrierKind barrier, int base,
                                   int offset, int value) {
  cout << id << ":"
       << "VisitStore " << MachineReprToString(rep) << " " << barrier
       << " base: " << base << "; offset:" << offset << "; value: " << value
       << endl;
}

void TestParserVisitor::VisitBitcastWordToTagged(int id, int e) {
  cout << id << ":"
       << "VisitBitcastWordToTagged"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitChangeInt32ToFloat64(int id, int e) {
  cout << id << ":"
       << "ChangeFloat32ToFloat64"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitChangeFloat32ToFloat64(int id, int e) {
  cout << id << ":"
       << "ChangeFloat32ToFloat64"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitChangeUint32ToFloat64(int id, int e) {
  cout << id << ":"
       << "ChangeUint32ToFloat64"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitTruncateFloat64ToWord32(int id, int e) {
  cout << id << ":"
       << "TruncateFloat64ToWord32"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitTruncateFloat64ToFloat32(int id, int e) {
  cout << id << ":"
       << "TruncateFloat64ToFloat32"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitRoundFloat64ToInt32(int id, int e) {
  cout << id << ":"
       << "RoundFloat64ToInt32"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitFloat64ExtractHighWord32(int id, int e) {
  cout << id << ":"
       << "Float64ExtractHighWord32"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitRoundInt32ToFloat32(int id, int e) {
  cout << id << ":"
       << "RoundInt32ToFloat32"
       << "  e " << e << endl;
}

void TestParserVisitor::VisitInt32Add(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32Add"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitInt32AddWithOverflow(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32AddWithOverflow"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitInt32SubWithOverflow(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32SubWithOverflow"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitInt32MulWithOverflow(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32MulWithOverflow"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitInt32Sub(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32Sub"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitInt32Mul(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32Mul"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitInt32Div(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32Div"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitInt32Mod(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32Mod"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32Shl(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Shl"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32Xor(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Xor"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32Shr(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Shr"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32Sar(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Sar"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32Mul(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Mul"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32And(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32And"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32Or(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Or"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32Equal(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Equal"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitWord32Clz(int id, int e) {
  cout << id << ":"
       << "Word32Clz"
       << "  e: " << e << endl;
}

void TestParserVisitor::VisitFloat64Add(int id, int e1, int e2) {
  cout << id << ":"
       << "Float64Add"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitFloat64Sub(int id, int e1, int e2) {
  cout << id << ":"
       << "Float64Sub"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitFloat64Mul(int id, int e1, int e2) {
  cout << id << ":"
       << "Float64Mul"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitFloat64Div(int id, int e1, int e2) {
  cout << id << ":"
       << "Float64Div"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitFloat64LessThan(int id, int e1, int e2) {
  cout << id << ":"
       << "Float64LessThan"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitFloat64LessThanOrEqual(int id, int e1, int e2) {
  cout << id << ":"
       << "Float64LessThanOrEqual"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitFloat64Equal(int id, int e1, int e2) {
  cout << id << ":"
       << "Float64Equal"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitFloat64Mod(int id, int e1, int e2) {
  cout << id << ":"
       << "Float64Mod"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitFloat64Neg(int id, int e) {
  cout << id << ":"
       << "Float64Neg"
       << "  e: " << e << endl;
}

void TestParserVisitor::VisitFloat64Abs(int id, int e) {
  cout << id << ":"
       << "Float64Abs"
       << "  e: " << e << endl;
}

void TestParserVisitor::VisitInt32LessThanOrEqual(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32LessThanOrEqual"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitUint32LessThanOrEqual(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitUint32LessThanOrEqual"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitUint32LessThan(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitUint32LessThan"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitInt32LessThan(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32LessThan"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
}

void TestParserVisitor::VisitBranch(int id, int cmp, int btrue, int bfalse) {
  cout << id << ":"
       << "VisitBranch "
       << " cmp: " << cmp << " btrue: " << btrue << " bfalse: " << bfalse
       << endl;
}

void TestParserVisitor::VisitSwitch(int id, int val,
                                    const OperandsVector& blocks) {
  cout << id << ":"
       << "VisitBranch"
       << " cmp: " << val << endl;
}

void TestParserVisitor::VisitIfValue(int id, int val) {
  cout << id << ":"
       << "VisitIfValue"
       << " : " << val << endl;
}

void TestParserVisitor::VisitIfDefault(int id) {
  cout << id << ":"
       << "VisitIfDefault" << endl;
}

void TestParserVisitor::VisitHeapConstant(int id, int64_t magic) {
  cout << id << ":"
       << "VisitHeapConstant"
       << " magic:" << magic << endl;
}

void TestParserVisitor::VisitFloat64Constant(int id, double value) {
  cout << id << ":"
       << "VisitFloat64Constant"
       << " value:" << value << endl;
}

void TestParserVisitor::VisitRoot(int id, int index) {
  cout << id << ":"
       << "VisitRoot"
       << " index:" << index << endl;
}

void TestParserVisitor::VisitCodeForCall(int id, int64_t magic) {
  cout << id << ":"
       << "VisitCodeForCall"
       << " magic:" << magic << endl;
}

void TestParserVisitor::VisitSmiConstant(int id, void* magic) {
  cout << id << ":"
       << "VisitSmiConstant"
       << " magic:" << magic << endl;
}

void TestParserVisitor::VisitExternalConstant(int id, int64_t magic) {
  cout << id << ":"
       << "VisitExternalConstant"
       << " magic:" << magic << endl;
}

void TestParserVisitor::VisitPhi(int id, MachineRepresentation rep,
                                 const OperandsVector& operands) {
  cout << id << ":"
       << "VisitPhi"
       << " rep:" << MachineReprToString(rep) << operands << endl;
}

void TestParserVisitor::VisitCall(int id, bool code, const CallDescriptor&,
                                  const OperandsVector& operands) {
  cout << id << ":"
       << "VisitCall"
       << " is_code:" << code << operands << endl;
}

void TestParserVisitor::VisitInvoke(int id, bool code, const CallDescriptor&,
                                    const OperandsVector& operands, int, int) {
  cout << id << ":"
       << "VisitInvoke"
       << " is_code:" << code << operands << endl;
}

void TestParserVisitor::VisitCallWithCallerSavedRegisters(
    int id, const OperandsVector& operands) {
  cout << id << ":"
       << "VisitCallWithCallerSavedRegisters" << endl;
}

void TestParserVisitor::VisitTailCall(
    int id, bool code, const CallDescriptor& registers_for_operands,
    const OperandsVector& operands) {
  cout << id << ":"
       << "VisitTailCall"
       << " is_code:" << code << operands << endl;
}

void TestParserVisitor::VisitProjection(int id, int e, int index) {}
void TestParserVisitor::VisitReturn(int id, int pop_count,
                                    const OperandsVector& operands) {}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

int main() {
  using namespace v8::internal::tf_llvm;
  FILE* f = fopen("src/llvm/tf/tests/scheduled.txt", "r");
  if (!f) {
    fprintf(stderr, "fails to open src/llvm/tf/tests/scheduled.txt\n");
    exit(1);
  }
  TestParserVisitor tpv;
  TFParser tfparser(&tpv);
  tfparser.Parse(f);
  fclose(f);
}

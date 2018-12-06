#include <stdlib.h>
#include <iostream>
#include "tf-parser.h"
#define UNREACHABLE() __builtin_unreachable()

class TestParserVisitor : public TFVisitor {
  void VisitBlock(int id, bool is_deferred,
                  const OperandsVector& predecessors) override;
  void VisitGoto(int bid) override;
  void VisitParameter(int id, int pid) override;
  void VisitLoadParentFramePointer(int id) override;
  void VisitInt32Constant(int id, int32_t value) override;
  void VisitLoad(int id, MachineRepresentation rep, MachineSemantic semantic,
                 int base, int offset) override;
  void VisitStore(int id, MachineRepresentation rep, WriteBarrierKind barrier,
                  int base, int offset, int value) override;
  void VisitBitcastWordToTagged(int id, int e) override;
  void VisitInt32Add(int id, int e1, int e2) override;
  void VisitInt32Sub(int id, int e1, int e2) override;
  void VisitInt32Mul(int id, int e1, int e2) override;
  void VisitWord32Shl(int id, int e1, int e2) override;
  void VisitWord32Shr(int id, int e1, int e2) override;
  void VisitWord32Sar(int id, int e1, int e2) override;
  void VisitWord32Mul(int id, int e1, int e2) override;
  void VisitWord32And(int id, int e1, int e2) override;
  void VisitWord32Equal(int id, int e1, int e2) override;
  void VisitInt32LessThanOrEqual(int id, int e1, int e2) override;
  void VisitInt32LessThan(int id, int e1, int e2) override;
  void VisitUint32LessThanOrEqual(int id, int e1, int e2) override;
  void VisitBranch(int id, int cmp, int btrue, int bfalse) override;
  void VisitHeapConstant(int id, int64_t magic) override;
  void VisitExternalConstant(int id, int64_t magic) override;
  void VisitPhi(int id, MachineRepresentation rep,
                const OperandsVector& operands) override;
  void VisitCall(int id, bool code,
                 const RegistersForOperands& registers_for_operands,
                 const OperandsVector& operands) override;
  void VisitTailCall(int id, bool code,
                     const RegistersForOperands& registers_for_operands,
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
       << "VisitParameter " << id << endl;
}

void TestParserVisitor::VisitInt32Constant(int id, int32_t value) {
  cout << id << ":"
       << "VisitInt32Constant " << value << endl;
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

void TestParserVisitor::VisitInt32Add(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitInt32Add"
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

void TestParserVisitor::VisitWord32Shl(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Shl"
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

void TestParserVisitor::VisitWord32Equal(int id, int e1, int e2) {
  cout << id << ":"
       << "VisitWord32Equal"
       << "  e1: " << e1 << "  e2:" << e2 << endl;
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

void TestParserVisitor::VisitHeapConstant(int id, int64_t magic) {
  cout << id << ":"
       << "VisitHeapConstant"
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

void TestParserVisitor::VisitCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  cout << id << ":"
       << "VisitCall"
       << " is_code:" << code << operands << endl;
}

void TestParserVisitor::VisitTailCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  cout << id << ":"
       << "VisitTailCall"
       << " is_code:" << code << operands << endl;
}

int main() {
  FILE* f = fopen("tf/tests/scheduled.txt", "r");
  if (!f) {
    fprintf(stderr, "fails to open tf/tests/scheduled.txt\n");
    exit(1);
  }
  TestParserVisitor tpv;
  TFParser tfparser(&tpv);
  tfparser.Parse(f);
  fclose(f);
}

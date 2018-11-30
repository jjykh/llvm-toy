#ifndef LIVENESS_ANAYLSIS_VISITOR_H
#define LIVENESS_ANAYLSIS_VISITOR_H
#include "tf-visitor.h"
namespace jit {
class BasicBlockManager;
class LivenessAnalysisVisitor final: public TFVisitor {
public:
  explicit LivenessAnalysisVisitor(BasicBlockManager* bbm);
private:
  void VisitBlock(int id, const OperandsVector& predecessors) override;
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

  inline BasicBlockManager& basicBlockManager() { return *basicBlockManager_; }
};
}
#endif  // LIVENESS_ANAYLSIS_VISITOR_H

#ifndef LIVENESS_ANALYSIS_VISITOR_H
#define LIVENESS_ANALYSIS_VISITOR_H
#include <set>
#include "src/llvm/tf/tf-visitor.h"
namespace v8 {
namespace internal {
namespace tf_llvm {
class BasicBlockManager;
class BasicBlock;
class LivenessAnalysisVisitor final : public TFVisitor {
 public:
  explicit LivenessAnalysisVisitor(BasicBlockManager& bbm);
  ~LivenessAnalysisVisitor() override = default;
  void CalculateLivesIns();

 private:
  void VisitBlock(int id, bool, const OperandsVector& predecessors) override;
  void VisitGoto(int bid) override;
#define DECL_METHOD(name, signature) void Visit##name signature override;
  INSTRUCTIONS(DECL_METHOD)
#undef DECL_METHOD

  inline BasicBlockManager& basicBlockManager() {
    return *basic_block_manager_;
  }
  void AddIfNotInDefines(int id);
  void Define(int id);
  void EndBlock();

 private:
  BasicBlockManager* basic_block_manager_;
  std::set<int> current_references_;
  std::set<int> current_defines_;
  BasicBlock* current_basic_block_;
};
}
}
}
#endif  // LIVENESS_ANALYSIS_VISITOR_H

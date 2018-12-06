#ifndef LLVM_TF_BUILDER_H
#define LLVM_TF_BUILDER_H
#include <unordered_map>
#include <vector>
#include "abbreviated-types.h"
#include "tf-visitor.h"
namespace jit {
class Output;
class BasicBlock;
class BasicBlockManager;

class LLVMTFBuilder final : public TFVisitor {
 public:
  explicit LLVMTFBuilder(Output&, BasicBlockManager&);
  void End();

 private:
  void VisitBlock(int id, bool, const OperandsVector& predecessors) override;
  void VisitGoto(int bid) override;
#define DECL_METHOD(name, signature) void Visit##name signature override;
  INSTRUCTIONS(DECL_METHOD)
#undef DECL_METHOD
  inline Output& output() { return *output_; }
  inline BasicBlockManager& basic_block_manager() {
    return *basic_block_manager_;
  }
  void MergePredecessors(BasicBlock* bb);
  bool AllPredecessorStarted(BasicBlock* bb, BasicBlock** ref_pred);
  void BuildPhiAndPushToWorkList(BasicBlock* bb, BasicBlock* ref_pred);
  void ProcessPhiWorkList();
  void DoCall(int id, bool code,
              const RegistersForOperands& registers_for_operands,
              const OperandsVector& operands);
  void DoTailCall(int id, bool code,
                  const RegistersForOperands& registers_for_operands,
                  const OperandsVector& operands);
  void EndCurrentBlock();
  LValue EnsureWord32(LValue);
  Output* output_;
  BasicBlockManager* basic_block_manager_;
  BasicBlock* current_bb_;
  std::vector<BasicBlock*> phi_rebuild_worklist_;
  std::vector<BasicBlock*> tf_phi_rebuild_worklist_;
  int64_t state_point_id_next_;
};
}  // namespace jit
#endif  // LLVM_TF_BUILDER_H

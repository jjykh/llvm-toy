#ifndef LLVM_TF_BUILDER_H
#define LLVM_TF_BUILDER_H
#include <unordered_map>
#include "tf-visitor.h"
namespace jit {
class Output;
class BasicBlock;
class BasicBlockManager;

class LLVMTFBuilder final : public TFVisitor {
 public:
  explicit LLVMTFBuilder(Output& output, BasicBlockManager& _basicBlockManager);
  void end();

 private:
  void VisitBlock(int id, const OperandsVector& predecessors) override;
  void VisitGoto(int bid) override;
#define DECL_METHOD(name, signature) void Visit##name signature override;
  INSTRUCTIONS(DECL_METHOD)
#undef DECL_METHOD
  inline Output& output() { return *output_; }
  inline BasicBlockManager& basicBlockManager() { return *basicBlockManager_; }
  Output* output_;
  BasicBlockManager* basicBlockManager_;
  BasicBlock* currentBB_;
};
}  // namespace jit
#endif  // LLVM_TF_BUILDER_H

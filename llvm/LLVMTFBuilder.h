#ifndef LLVMTFBUILDER_H
#define LLVMTFBUILDER_H
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
  inline Output& output() { return *m_output; }
  inline BasicBlockManager& basicBlockManager() { return *m_basicBlockManager; }
  Output* m_output;
  BasicBlockManager* m_basicBlockManager;
  BasicBlock* m_currentBB;
};
}  // namespace jit
#endif  // LLVMTFBUILDER_H

#ifndef LLVMTFBUILDER_H
#define LLVMTFBUILDER_H
#include <unordered_map>
#include "tf-visitor.h"
namespace jit {
class Output;
class BasicBlock;

class LLVMTFBuilder final : public TFVisitor {
 public:
  explicit LLVMTFBuilder(Output& output);
  void end();

 private:
  BasicBlock* createBB(int);
  BasicBlock* findBB(int);
  BasicBlock* ensureBB(int);
  void VisitBlock(int id, const OperandsVector& predecessors) override;
  void VisitGoto(int bid) override;
#define DECL_METHOD(name, signature) void Visit##name signature override;
  INSTRUCTIONS(DECL_METHOD)
#undef DECL_METHOD
  inline Output& output() { return *m_output; }
  std::unordered_map<int, std::unique_ptr<BasicBlock>> m_bbs;
  Output* m_output;
  BasicBlock* m_currentBB;
};
}  // namespace jit
#endif  // LLVMTFBUILDER_H

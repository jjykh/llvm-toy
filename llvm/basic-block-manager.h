#ifndef BASIC_BLOCK_MANAGER_H
#define BASIC_BLOCK_MANAGER_H
#include <unordered_map>
#include <vector>
#include <memory>
namespace jit {
class Output;
class BasicBlock;
class BasicBlockManager {
public:
  explicit BasicBlockManager(Output& output);
  ~BasicBlockManager();
  BasicBlock* createBB(int);
  BasicBlock* findBB(int);
  BasicBlock* ensureBB(int);
  inline std::vector<int>& rpo() { return rpo_;}
private:
  inline Output& output() {return *output_;}
  Output* output_;
  std::vector<int> rpo_;
  std::unordered_map<int, std::unique_ptr<BasicBlock>> bbs_;
};
}
#endif  // BASIC_BLOCK_MANAGER_H

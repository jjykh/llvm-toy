#ifndef BASIC_BLOCK_MANAGER_H
#define BASIC_BLOCK_MANAGER_H
#include <map>
#include <memory>
#include <vector>
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
  inline std::vector<int>& rpo() { return rpo_; }
  typedef std::map<int, std::unique_ptr<BasicBlock>> BasicBlockMap;
  inline BasicBlockMap::iterator begin() { return bbs_.begin(); }
  inline BasicBlockMap::iterator end() { return bbs_.end(); }

 private:
  inline Output& output() { return *output_; }
  Output* output_;
  std::vector<int> rpo_;
  BasicBlockMap bbs_;
};

template <class T, class Y>
static inline void ResetImpls(Y& bbm) {
  for (auto& bb : bbm) {
    bb.second->template ResetImpl<T>();
  }
}
}  // namespace jit
#endif  // BASIC_BLOCK_MANAGER_H

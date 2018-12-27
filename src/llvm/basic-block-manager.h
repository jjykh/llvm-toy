#ifndef BASIC_BLOCK_MANAGER_H
#define BASIC_BLOCK_MANAGER_H
#include <map>
#include <memory>
#include <vector>
namespace v8 {
namespace internal {
namespace tf_llvm {
class Output;
class BasicBlock;

class BasicBlockManager {
 public:
  BasicBlockManager();
  ~BasicBlockManager();
  BasicBlock* createBB(int);
  BasicBlock* findBB(int);
  BasicBlock* ensureBB(int);
  inline std::vector<int>& rpo() { return rpo_; }
  typedef std::map<int, std::unique_ptr<BasicBlock>> BasicBlockMap;
  inline BasicBlockMap::iterator begin() { return bbs_.begin(); }
  inline BasicBlockMap::iterator end() { return bbs_.end(); }
  inline bool needs_frame() const { return needs_frame_; }
  inline void set_needs_frame(bool n) { needs_frame_ = n; }

 private:
  std::vector<int> rpo_;
  BasicBlockMap bbs_;
  bool needs_frame_;
};

template <class T, class Y>
static inline void ResetImpls(Y& bbm) {
  for (auto& bb : bbm) {
    bb.second->template ResetImpl<T>();
  }
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // BASIC_BLOCK_MANAGER_H

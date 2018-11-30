#ifndef BASIC_BLOCK_H
#define BASIC_BLOCK_H
#include <unordered_map>
#include <vector>
#include "output.h"
namespace jit {
class BasicBlock {
 public:
  explicit BasicBlock(int id, Output& output);
  ~BasicBlock();
  void startBuild(Output& output);
  void endBuild();
  void addPredecessor(BasicBlock* pred);
  inline bool started() const { return started_; }
  inline bool ended() const { return ended_; }
  inline LBasicBlock nativeBB() { return bb_; }
  inline void setValue(int nid, LValue value) { values_[nid] = value; }
  inline LValue value(int nid) {
    auto found = values_.find(nid);
    assert(found != values_.end());
    return found->second;
  }
  inline const std::vector<BasicBlock*>& predecessors() const {
    return predecessors_;
  }
  inline std::vector<BasicBlock*>& predecessors() { return predecessors_; }

  inline std::vector<int>& rpo() { return rpo_;}

 private:
  void mergePredecessors(Output* output);
  std::vector<BasicBlock*> predecessors_;
  std::unordered_map<int, LValue> values_;
  std::vector<int> liveins_;
  std::vector<int> rpo_;
  LBasicBlock bb_;
  int id_;
  bool started_;
  bool ended_;
};
}  // namespace jit
#endif  // BASIC_BLOCK_H

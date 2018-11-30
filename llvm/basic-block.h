#ifndef BASIC_BLOCK_H
#define BASIC_BLOCK_H
#include <assert.h>
#include <set>
#include <unordered_map>
#include <vector>
#include "output.h"
namespace jit {
struct PhiDesc {
  int from;
  int value;
};
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

  inline const std::vector<BasicBlock*>& successors() const {
    return successors_;
  }

  inline std::vector<BasicBlock*>& successors() { return successors_; }

  inline std::vector<int>& rpo() { return rpo_; }
  inline std::vector<int>& liveins() { return liveins_; }
  inline std::set<int>& defines() { return defines_; }
  inline int id() const { return id_; }
  inline std::vector<PhiDesc>& phis() { return phis_; }

 private:
  void mergePredecessors(Output& output);
  std::vector<BasicBlock*> predecessors_;
  std::vector<BasicBlock*> successors_;
  std::unordered_map<int, LValue> values_;
  std::vector<int> liveins_;
  std::vector<PhiDesc> phis_;
  std::set<int> defines_;
  std::vector<int> rpo_;
  LBasicBlock bb_;
  int id_;
  bool started_;
  bool ended_;
};
}  // namespace jit
#endif  // BASIC_BLOCK_H

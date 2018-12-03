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
  explicit BasicBlock(int id);
  ~BasicBlock();
  void StartBuild();
  void EndBuild();
  void AddPredecessor(BasicBlock* pred);
  inline bool started() const { return started_; }
  inline bool ended() const { return ended_; }
  inline LBasicBlock native_bb() { return bb_; }
  // FIXME: move to tf builder
  inline void set_value(int nid, LValue value) { values_[nid] = value; }
  inline LValue value(int nid) {
    auto found = values_.find(nid);
    assert(found != values_.end());
    return found->second;
  }
  inline std::unordered_map<int, LValue>& values() { return values_; }

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

  inline int id() const { return id_; }

  template <class T>
  inline T* GetImpl() {
    return static_cast<T*>(impl_);
  }

  inline void SetImpl(void* impl) { impl_ = impl; }

  template <class T>
  inline void ResetImpl() {
    if (impl_) {
      delete GetImpl<T>();
      impl_ = nullptr;
    }
  }

  void AssignNativeBB(LBasicBlock native) { bb_ = native; }

 private:
  std::vector<BasicBlock*> predecessors_;
  std::vector<BasicBlock*> successors_;
  std::unordered_map<int, LValue> values_;
  std::vector<int> liveins_;
  std::vector<int> rpo_;

  void* impl_;
  LBasicBlock bb_;
  int id_;
  bool started_;
  bool ended_;
};
}  // namespace jit
#endif  // BASIC_BLOCK_H

// Copyright 2019 UCWeb Co., Ltd.

#ifndef BASIC_BLOCK_H
#define BASIC_BLOCK_H
#include <assert.h>

#include <set>
#include <vector>

#include "src/llvm/common-values.h"
#include "src/llvm/llvm-log.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
struct PhiDesc {
  int from;
  int value;
};
class BasicBlock {
 public:
  explicit BasicBlock(int id);
  ~BasicBlock();
  void AddPredecessor(BasicBlock* pred);

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

  bool is_deferred() const { return deferred_; }
  void set_deffered(bool is_deferred) { deferred_ = is_deferred; }

 private:
  std::vector<BasicBlock*> predecessors_;
  std::vector<BasicBlock*> successors_;
  std::vector<int> liveins_;
  std::vector<int> rpo_;

  void* impl_;
  int id_;
  bool deferred_;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // BASIC_BLOCK_H

// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/basic-block.h"
#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace v8 {
namespace internal {
namespace tf_llvm {
BasicBlock::BasicBlock(int id) : id_(id), deferred_(false) {}

BasicBlock::~BasicBlock() {}

void BasicBlock::AddPredecessor(BasicBlock* pred) {
  predecessors().push_back(pred);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#include "src/llvm/basic-block.h"
#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace v8 {
namespace internal {
namespace tf_llvm {
BasicBlock::BasicBlock(int id)
    : id_(id), started_(false), ended_(false), deferred_(false) {}

BasicBlock::~BasicBlock() {}

void BasicBlock::StartBuild() {
  assert(!started());
  assert(!ended());
  started_ = true;
}

void BasicBlock::EndBuild() {
  assert(started());
  assert(!ended());
  ended_ = true;
}

void BasicBlock::AddPredecessor(BasicBlock* pred) {
  assert(!pred->started());
  predecessors().push_back(pred);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

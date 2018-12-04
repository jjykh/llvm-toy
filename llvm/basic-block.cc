#include "basic-block.h"
#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace jit {
BasicBlock::BasicBlock(int id)
    : bb_(nullptr), id_(id), started_(false), ended_(false) {}

BasicBlock::~BasicBlock() {}

void BasicBlock::StartBuild() {
  assert(!started());
  assert(!!native_bb());
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
}  // namespace jit

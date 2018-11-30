#include "basic-block.h"
#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace jit {
BasicBlock::BasicBlock(int id, Output& output)
    : bb_(nullptr), id_(id), started_(false), ended_(false) {
  char buf[256];
  snprintf(buf, 256, "B%d\n", id_);
  bb_ = output.appendBasicBlock(buf);
}

BasicBlock::~BasicBlock() {}

void BasicBlock::startBuild(Output& output) {
  assert(!started());
  assert(!nativeBB());
  assert(!ended());
  started_ = true;
  output.positionToBBEnd(bb_);
  mergePredecessors(output);
}

void BasicBlock::endBuild() {
  assert(!started());
  assert(!ended());
  ended_ = true;
}

void BasicBlock::addPredecessor(BasicBlock* pred) {
  assert(!pred->started());
  predecessors().push_back(pred);
}

void BasicBlock::mergePredecessors(Output& output) {
  if (predecessors().empty()) return;
  // FIXME: implement merge.
}
}  // namespace jit

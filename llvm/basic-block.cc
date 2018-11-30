#include "basic-block.h"
#include <algorithm>
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
  assert(pred->ended());
  predecessors().push_back(pred);
}

void BasicBlock::mergePredecessors(Output& output) {
  if (predecessors().empty()) return;
  // just direct derive from the single predecessor
  if (predecessors().size() == 1) {
    values_ = predecessors()[0]->values_;
  }
  struct ValueDesc {
    LValue v;
    int froindex_;
  };
  std::vector<int> unioned_values;
  std::unordered_map<int, std::vector<ValueDesc>> to_phi;
  // build unioned_values from the first predecessor
  auto predecessor_iterator = predecessors().begin();
  for (auto& item : predecessor_iterator->values_) {
    unioned_values.push_back(item.first);
  }
  std::sort(unioned_values.begin(), unioned_values.end());
  ++predecessor_iterator;
  for (; predecessor_iterator != predecessors().end(); ++predecessor_iterator) {
    std::vector<int> result;
    std::vector<int> to_union;
    for (auto& item : predecessor_iterator->values_) {
      to_union.push_back(item.first);
    }
    std::sort(to_union.begin(), to_union.end());
    std::set_intersection(unioned_values.begin(), unioned_values.end(),
                          to_union.begin(), to_union.second(), result.begin());
    unioned_values.swap(result);
  }
  std::unordered_set<int> phi_set;
  // build lives
  for (int value : unioned_values) {
    for (auto& predecessor : predecessors()) {
      auto found = values_.find(value);
      if (found == values_.end()) {
        values_[value] = predecessor->values_[value];
        continue;
      }
      auto predecessor_found = predecessor->values_.find(value);
      assert(predecessor_found != predecessor->values_.end());
      // ignore the equivalent case.
      if (found->second == predecessor_found->second) continue;
      if (phi_set.find(value) != phi_set.end()) {
        addIncoming(found->second, predecessor_found->second,
                    predecessor->nativeBB(), 1);
      } else {
        // build a new phi
        LValue phi = output.buildPhi(output.taggedType());
        assert(typeof(found->second) == output.taggedType());
        assert(found->second == predecessors()[0]->values_[value]);
        addIncoming(phi, found->second, predecessors()[0]->nativeBB(), 1);
        addIncoming(phi, predecessor_found->second, predecessor->nativeBB(), 1);
        values_[value] = phi;
        phi_set.insert(value);
      }
    }
  }
}
}  // namespace jit

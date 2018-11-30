#include "BasicBlock.h"
#include <algorithm>
#include <unordered_set>

namespace jit {
BasicBlock::BasicBlock(int id, Output& output)
    : m_bb(nullptr), m_id(id), m_started(false), m_ended(false) {
  char buf[256];
  snprintf(buf, 256, "B%d\n", m_id);
  m_bb = output.appendBasicBlock(buf);
}

BasicBlock::~BasicBlock() {}

void BasicBlock::startBuild(Output& output) {
  assert(!started());
  assert(!nativeBB());
  assert(!ended());
  m_started = true;
  output.positionToBBEnd(m_bb);
  mergePredecessors(output);
}

void BasicBlock::endBuild() {
  assert(!started());
  assert(!ended());
  m_ended = true;
}

void BasicBlock::addPredecessor(BasicBlock* pred) {
  assert(pred->ended());
  predecessors().push_back(pred);
}

void BasicBlock::mergePredecessors(Output& output) {
  if (predecessors().empty()) return;
  // just direct derive from the single predecessor
  if (predecessors().size() == 1) {
    m_values = predecessors()[0]->m_values;
  }
  struct ValueDesc {
    LValue v;
    int from_index;
  };
  std::vector<int> unioned_values;
  std::unordered_map<int, std::vector<ValueDesc>> to_phi;
  // build unioned_values from the first predecessor
  auto predecessor_iterator = predecessors().begin();
  for (auto& item : predecessor_iterator->m_values) {
    unioned_values.push_back(item.first);
  }
  std::sort(unioned_values.begin(), unioned_values.end());
  ++predecessor_iterator;
  for (; predecessor_iterator != predecessors().end(); ++predecessor_iterator) {
    std::vector<int> result;
    std::vector<int> to_union;
    for (auto& item : predecessor_iterator->m_values) {
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
      auto found = m_values.find(value);
      if (found == m_values.end()) {
        m_values[value] = predecessor->m_values[value];
        continue;
      }
      auto predecessor_found = predecessor->m_values.find(value);
      assert(predecessor_found != predecessor->m_values.end());
      // ignore the equivalent case.
      if (found->second == predecessor_found->second) continue;
      if (phi_set.find(value) != phi_set.end()) {
        addIncoming(found->second, predecessor_found->second,
                    predecessor->nativeBB(), 1);
      } else {
        // build a new phi
        LValue phi = output.buildPhi(output.taggedType());
        assert(typeof(found->second) == output.taggedType());
        assert(found->second == predecessors()[0]->m_values[value]);
        addIncoming(phi, found->second, predecessors()[0]->nativeBB(), 1);
        addIncoming(phi, predecessor_found->second, predecessor->nativeBB(), 1);
        m_values[value] = phi;
        phi_set.insert(value);
      }
    }
  }
}
}  // namespace jit

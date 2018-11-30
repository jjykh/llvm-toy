#include "basic-block-manager.h"
#include "basic-block.h"
#include "output.h"

namespace jit {

BasicBlockManager::BasicBlockManager(Output& output) :output_(&output){
}

BasicBlockManager::~BasicBlockManager() {
}
BasicBlock* BasicBlockManager::createBB(int bid) {
  BasicBlock* bb;
  auto newBB = std::make_unique<BasicBlock>(bid, output());
  bb = newBB.get();
  auto inserted = bbs_.insert(bid, std::move(newBB));
  assert(insert.second);
  return bb;
}

BasicBlock* BasicBlockManager::findBB(int bid) {
  auto found = bbs_.find(id);
  if (found != bbs_.end()) {
    return found->second.get();
  }
  return nullptr;
}

BasicBlock* BasicBlockManager::ensureBB(int bid) {
  BasicBlock* bb = findBB(bid);
  if (!!bb) return bb;
  bb = createBB(bid);
  return bb;
}

}

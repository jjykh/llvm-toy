#include <memory>
#include "src/llvm/basic-block-manager.h"
#include "src/llvm/basic-block.h"
#include "src/llvm/output.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
BasicBlockManager::BasicBlockManager() {}

BasicBlockManager::~BasicBlockManager() {}

BasicBlock* BasicBlockManager::createBB(int bid) {
  BasicBlock* bb;
  auto newBB = std::make_unique<BasicBlock>(bid);
  bb = newBB.get();
  auto inserted = bbs_.emplace(bid, std::move(newBB));
  assert(inserted.second);
  return bb;
}

BasicBlock* BasicBlockManager::findBB(int bid) {
  auto found = bbs_.find(bid);
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
}
}

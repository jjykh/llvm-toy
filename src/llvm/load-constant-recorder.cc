#include "src/llvm/load-constant-recorder.h"
#include "src/llvm/log.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
void LoadConstantRecorder::Register(int64_t magic,
                                    LoadConstantRecorder::Type type) {
  map_.insert(std::make_pair(magic, type));
}

LoadConstantRecorder::Type LoadConstantRecorder::Query(int64_t magic) const {
  auto found = map_.find(magic);
  EMASSERT(found != map_.end());
  return found->second;
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

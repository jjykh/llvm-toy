// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/load-constant-recorder.h"
#include "src/llvm/log.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
void LoadConstantRecorder::Register(int64_t magic,
                                    LoadConstantRecorder::Type type,
                                    int rmode) {
  map_.emplace(magic, std::make_tuple(type, rmode));
}

std::tuple<LoadConstantRecorder::Type, int> LoadConstantRecorder::Query(
    int64_t magic) const {
  auto found = map_.find(magic);
  EMASSERT(found != map_.end());
  return found->second;
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

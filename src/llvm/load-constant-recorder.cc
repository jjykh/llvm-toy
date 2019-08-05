// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/load-constant-recorder.h"
#include "src/llvm/log.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
int64_t LoadConstantRecorder::Register(int64_t magic,
                                       LoadConstantRecorder::Type type,
                                       int rmode) {
  int result_magic = magic;
  switch (type) {
    case kRelocatableInt32Constant:
      result_magic = magic | (static_cast<int64_t>(type) << 16) | (rmode << 24);
      break;
    default:
      break;
  }
  map_.emplace(result_magic, MagicInfo(type, rmode, magic));
  return result_magic;
}

const LoadConstantRecorder::MagicInfo& LoadConstantRecorder::Query(
    int64_t magic) const {
  auto found = map_.find(magic);
  EMASSERT(found != map_.end());
  return found->second;
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

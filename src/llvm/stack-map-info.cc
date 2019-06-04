// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
StackMapInfo::StackMapInfo(StackMapInfoType type) : type_(type) {}
StackMapInfo::~StackMapInfo() {}

CallInfo::CallInfo(LocationVector&& locations)
    : StackMapInfo(StackMapInfoType::kCallInfo),
      locations_(std::move(locations)),
      tailcall_return_count_(0),
      is_tailcall_(false) {}

ReturnInfo::ReturnInfo()
    : StackMapInfo(StackMapInfoType::kReturn),
      pop_count_is_constant_(false),
      constant_(0) {}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

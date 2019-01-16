#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
StackMapInfo::StackMapInfo(StackMapInfoType type) : type_(type) {}
StackMapInfo::~StackMapInfo() {}

CallInfo::CallInfo(LocationVector&& locations)
    : StackMapInfo(StackMapInfoType::kCallInfo),
      locations_(std::move(locations)),
      tailcall_(false) {}

StoreBarrierInfo::StoreBarrierInfo()
    : StackMapInfo(StackMapInfoType::kStoreBarrier) {}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

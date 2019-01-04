#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
StackMapInfo::StackMapInfo(StackMapInfoType type) : type_(type) {}

HeapConstantInfo::HeapConstantInfo(int64_t magic)
    : StackMapInfo(StackMapInfoType::kHeapConstant), magic_(magic) {}

ExternalReferenceInfo::ExternalReferenceInfo(int64_t magic)
    : StackMapInfo(StackMapInfoType::kExternalReference), magic_(magic) {}

CallInfo::CallInfo(LocationVector&& locations)
    : StackMapInfo(StackMapInfoType::kCallInfo),
      locations_(std::move(locations)),
      code_magic_(0),
      tailcall_(false) {}

StoreBarrierInfo::StoreBarrierInfo()
    : StackMapInfo(StackMapInfoType::kStoreBarrier) {}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

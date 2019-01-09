#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
StackMapInfo::StackMapInfo(StackMapInfoType type) : type_(type) {}

HeapConstantInfo::HeapConstantInfo(const void* pc)
    : StackMapInfo(StackMapInfoType::kHeapConstant), pc_(pc) {}

HeapConstantLocationInfo::HeapConstantLocationInfo(const void* pc)
    : StackMapInfo(StackMapInfoType::kHeapConstantLocation), pc_(pc) {}

ExternalReferenceInfo::ExternalReferenceInfo(const void* pc)
    : StackMapInfo(StackMapInfoType::kExternalReference), pc_(pc) {}

ExternalReferenceLocationInfo::ExternalReferenceLocationInfo(const void* pc)
    : StackMapInfo(StackMapInfoType::kExternalReferenceLocation), pc_(pc) {}

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

#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
StackMapInfo::StackMapInfo(StackMapInfoType type) : type_(type) {}

HeapConstantInfo::HeapConstantInfo()
    : StackMapInfo(StackMapInfoType::kHeapConstant) {}

HeapConstantLocationInfo::HeapConstantLocationInfo()
    : StackMapInfo(StackMapInfoType::kHeapConstantLocation) {}

ExternalReferenceInfo::ExternalReferenceInfo()
    : StackMapInfo(StackMapInfoType::kExternalReference) {}

ExternalReferenceLocationInfo::ExternalReferenceLocationInfo()
    : StackMapInfo(StackMapInfoType::kExternalReferenceLocation) {}

CodeConstantInfo::CodeConstantInfo()
    : StackMapInfo(StackMapInfoType::kCodeConstant) {}

CodeConstantLocationInfo::CodeConstantLocationInfo()
    : StackMapInfo(StackMapInfoType::kCodeConstantLocation) {}

CallInfo::CallInfo(LocationVector&& locations)
    : StackMapInfo(StackMapInfoType::kCallInfo),
      locations_(std::move(locations)),
      tailcall_(false) {}

StoreBarrierInfo::StoreBarrierInfo()
    : StackMapInfo(StackMapInfoType::kStoreBarrier) {}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
StackMapInfo::StackMapInfo(StackMapInfoType type) : type_(type) {}
StackMapInfo::~StackMapInfo() {}

CallInfo::CallInfo(LocationVector&& locations)
    : StackMapInfo(StackMapInfoType::kCallInfo),
      locations_(std::move(locations)),
      is_tailcall_(false),
      is_invoke_(false) {}

ReturnInfo::ReturnInfo()
    : StackMapInfo(StackMapInfoType::kReturn),
      pop_count_is_constant_(false),
      constant_(0) {}

ExceptionInfo::ExceptionInfo(int target_patch_id)
    : StackMapInfo(StackMapInfoType::kException),
      target_patch_id_(target_patch_id) {}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

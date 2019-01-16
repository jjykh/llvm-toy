#ifndef STACK_MAP_INFO_H
#define STACK_MAP_INFO_H
#include <stdint.h>
#include <memory>
#include <unordered_map>
#include <vector>
namespace v8 {
namespace internal {
namespace tf_llvm {

enum class StackMapInfoType {
  kHeapConstant,
  kHeapConstantLocation,
  kExternalReference,
  kExternalReferenceLocation,
  kCodeConstant,
  kCodeConstantLocation,
  kCallInfo,
  kStoreBarrier
};

class StackMapInfo {
 public:
  explicit StackMapInfo(StackMapInfoType);
  virtual ~StackMapInfo() = default;

  StackMapInfoType GetType() const { return type_; }

 private:
  const StackMapInfoType type_;
};

class HeapConstantInfo final : public StackMapInfo {
 public:
  explicit HeapConstantInfo();
  ~HeapConstantInfo() override = default;
};

class HeapConstantLocationInfo final : public StackMapInfo {
 public:
  explicit HeapConstantLocationInfo();
  ~HeapConstantLocationInfo() override = default;
};

class ExternalReferenceInfo final : public StackMapInfo {
 public:
  explicit ExternalReferenceInfo();
  ~ExternalReferenceInfo() override = default;
};

class ExternalReferenceLocationInfo final : public StackMapInfo {
 public:
  explicit ExternalReferenceLocationInfo();
  ~ExternalReferenceLocationInfo() override = default;
};

class CodeConstantInfo final : public StackMapInfo {
 public:
  explicit CodeConstantInfo();
  ~CodeConstantInfo() override = default;
};

class CodeConstantLocationInfo final : public StackMapInfo {
 public:
  explicit CodeConstantLocationInfo();
  ~CodeConstantLocationInfo() override = default;
};

class CallInfo final : public StackMapInfo {
 public:
  typedef std::vector<int> LocationVector;
  CallInfo(LocationVector&& locations);
  ~CallInfo() override = default;
  const LocationVector& locations() const { return locations_; }
  bool tailcall() const { return tailcall_; }
  void set_tailcall(bool _tailcall) { tailcall_ = _tailcall; }

 private:
  LocationVector locations_;
  bool tailcall_;
};

class StoreBarrierInfo final : public StackMapInfo {
 public:
  StoreBarrierInfo();
  ~StoreBarrierInfo() override = default;
};

// By zuojian.lzj, should be int64_t. But I believe there will not be any number
// greater.
typedef std::unordered_map<int, std::unique_ptr<StackMapInfo>> StackMapInfoMap;
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // STACK_MAP_INFO_H

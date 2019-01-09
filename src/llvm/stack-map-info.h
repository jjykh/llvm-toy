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
  explicit HeapConstantInfo(const void* pc);
  ~HeapConstantInfo() override = default;
  const void* pc() const { return pc_; }

 private:
  const void* pc_;
};

class HeapConstantLocationInfo final : public StackMapInfo {
 public:
  explicit HeapConstantLocationInfo(const void* pc);
  ~HeapConstantLocationInfo() override = default;
  const void* pc() const { return pc_; }

 private:
  const void* pc_;
};

class ExternalReferenceInfo final : public StackMapInfo {
 public:
  explicit ExternalReferenceInfo(const void* pc);
  ~ExternalReferenceInfo() override = default;
  const void* pc() const { return pc_; }

 private:
  const void* pc_;
};

class ExternalReferenceLocationInfo final : public StackMapInfo {
 public:
  explicit ExternalReferenceLocationInfo(const void* pc);
  ~ExternalReferenceLocationInfo() override = default;
  const void* pc() const { return pc_; }

 private:
  const void* pc_;
};

class CallInfo final : public StackMapInfo {
 public:
  typedef std::vector<int> LocationVector;
  CallInfo(LocationVector&& locations);
  ~CallInfo() override = default;
  const LocationVector& locations() const { return locations_; }
  bool tailcall() const { return tailcall_; }
  void set_tailcall(bool _tailcall) { tailcall_ = _tailcall; }
  int64_t code_magic() const { return code_magic_; }
  void set_code_magic(int64_t _code_magic) { code_magic_ = _code_magic; }

 private:
  LocationVector locations_;
  int64_t code_magic_;
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

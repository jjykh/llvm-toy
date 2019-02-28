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
  kExternalReference,
  kCodeConstant,
  kIsolateExternalReferenceLocation,
  kModuloExternalReferenceLocation,
  kRecordStubCodeLocation,
  kCallInfo,
  kStoreBarrier,
  kReturn,
  kException
};

class StackMapInfo {
 public:
  explicit StackMapInfo(StackMapInfoType);
  virtual ~StackMapInfo();

  StackMapInfoType GetType() const { return type_; }

 private:
  const StackMapInfoType type_;
};

class CallInfo final : public StackMapInfo {
 public:
  typedef std::vector<int> LocationVector;
  CallInfo(LocationVector&& locations);
  ~CallInfo() override = default;
  const LocationVector& locations() const { return locations_; }
  bool is_tailcall() const { return is_tailcall_; }
  void set_is_tailcall(bool _tailcall) { is_tailcall_ = _tailcall; }
  bool is_invoke() const { return is_invoke_; }
  void set_is_invoke(bool i) { is_invoke_ = i; }

 private:
  LocationVector locations_;
  bool is_tailcall_;
  bool is_invoke_;
};

class ReturnInfo final : public StackMapInfo {
 public:
  ReturnInfo();
  ~ReturnInfo() override = default;
  inline bool pop_count_is_constant() const { return pop_count_is_constant_; }
  inline void set_pop_count_is_constant(bool b) { pop_count_is_constant_ = b; }
  inline int constant() const { return constant_; }
  inline void set_constant(int constant) { constant_ = constant; }

 private:
  bool pop_count_is_constant_;
  int constant_;
};

class ExceptionInfo final : public StackMapInfo {
 public:
  explicit ExceptionInfo(int target_patch_id);
  ~ExceptionInfo() override = default;
  int target_patch_id() const { return target_patch_id_; }

 private:
  int target_patch_id_;
};

// By zuojian.lzj, should be int64_t. But I believe there will not be any number
// greater.
typedef std::unordered_map<int, std::unique_ptr<StackMapInfo>> StackMapInfoMap;
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // STACK_MAP_INFO_H

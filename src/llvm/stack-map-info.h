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
  kCallInfo,
  kStoreBarrier,
  kReturn
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
  unsigned tailcall_return_count() const { return tailcall_return_count_; }
  void set_tailcall_return_count(unsigned c) { tailcall_return_count_ = c; }
  int restore_slot_count() const { return restore_slot_count_; }
  void set_restore_slot_count(int c) { restore_slot_count_ = c; }

 private:
  LocationVector locations_;
  unsigned tailcall_return_count_;
  int restore_slot_count_;
  bool is_tailcall_;
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

// By zuojian.lzj, should be int64_t. But I believe there will not be any number
// greater.
typedef std::unordered_map<int, std::unique_ptr<StackMapInfo>> StackMapInfoMap;
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // STACK_MAP_INFO_H

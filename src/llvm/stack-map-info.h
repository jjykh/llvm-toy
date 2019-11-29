// Copyright 2019 UCWeb Co., Ltd.

#ifndef STACK_MAP_INFO_H
#define STACK_MAP_INFO_H
#include <stdint.h>
#include <memory>
#include <unordered_map>
#include <vector>
namespace v8 {
namespace internal {
namespace tf_llvm {

enum class StackMapInfoType { kCallInfo, kStoreBarrier, kReturn };

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
  int64_t relative_target() const { return relative_target_; }
  void set_relative_target(int64_t relative_target) {
    relative_target_ = relative_target;
  }
  int sp_adjust() const { return sp_adjust_; }
  void set_sp_adjust(int _sp_adjust) { sp_adjust_ = _sp_adjust; }

 private:
  LocationVector locations_;
  int64_t relative_target_;
  unsigned tailcall_return_count_;
  int sp_adjust_;
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

class StoreBarrierInfo final : public StackMapInfo {
 public:
  StoreBarrierInfo();
  ~StoreBarrierInfo() override = default;
  inline uint8_t write_barrier_kind() const { return write_barrier_kind_; }
  inline void set_write_barrier_kind(uint8_t wbk) { write_barrier_kind_ = wbk; }

 private:
  uint8_t write_barrier_kind_;
};

// By zuojian.lzj, should be int64_t. But I believe there will not be any number
// greater.
typedef std::unordered_map<int, std::unique_ptr<StackMapInfo>> StackMapInfoMap;
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // STACK_MAP_INFO_H

// Copyright 2019 UCWeb Co., Ltd.

#ifndef LOAD_CONSTANT_RECORDER_H
#define LOAD_CONSTANT_RECORDER_H
#include <stdint.h>
#include <unordered_map>
namespace v8 {
namespace internal {
namespace tf_llvm {

class LoadConstantRecorder {
 public:
  enum Type {
    kExternalReference,
    kHeapConstant,
    kCodeConstant,
    kRelativeCall,
    kRelocatableInt32Constant,
  };
  struct MagicInfo {
    MagicInfo(Type _type, int _rmode = 0, int _real_magic = 0)
        : type(_type), rmode(_rmode), real_magic(_real_magic) {}
    ~MagicInfo() = default;
    Type type;
    int rmode;
    int real_magic;
  };
  LoadConstantRecorder() = default;
  ~LoadConstantRecorder() = default;
  int64_t Register(int64_t magic, Type type, int rmode = 0);
  const MagicInfo& Query(int64_t magic) const;

 private:
  std::unordered_map<int64_t, MagicInfo> map_;
  int64_t next_magic_ = 0xefecdade;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#endif  // LOAD_CONSTANT_RECORDER_H

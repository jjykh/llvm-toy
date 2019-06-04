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
    kIsolateExternalReference,
    kRecordStubCodeConstant,
    kModuloExternalReference,
  };
  LoadConstantRecorder() = default;
  ~LoadConstantRecorder() = default;
  void Register(int64_t magic, Type type);
  Type Query(int64_t magic) const;
  static int64_t IsolateExternalReferenceMagic();
  static int64_t RecordStubCodeConstantMagic();
  static int64_t ModuloExternalReferenceMagic();

 private:
  std::unordered_map<int64_t, Type> map_;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#endif  // LOAD_CONSTANT_RECORDER_H

#ifndef LOAD_CONSTANT_RECORDER_H
#define LOAD_CONSTANT_RECORDER_H
#include <stdint.h>
#include <unordered_map>
namespace v8 {
namespace internal {
namespace tf_llvm {

class LoadConstantRecorder {
 public:
  enum Type { ExternalReference, HeapConstant, CodeConstant };
  LoadConstantRecorder() = default;
  ~LoadConstantRecorder() = default;
  void Register(int64_t magic, Type type);
  Type Query(int64_t magic) const;

 private:
  std::unordered_map<int64_t, Type> map_;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

#endif  // LOAD_CONSTANT_RECORDER_H

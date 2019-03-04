#ifndef EXCEPTION_TABLE_ARM_H
#define EXCEPTION_TABLE_ARM_H
#include <stddef.h>
#include <stdint.h>
#include <tuple>
#include <vector>

namespace v8 {
namespace internal {
namespace tf_llvm {
class ExceptionTableARM {
 public:
  ExceptionTableARM(const uint8_t*, size_t);
  ~ExceptionTableARM() = default;
  const std::vector<std::tuple<int, int>>& CallSiteHandlerPairs() const {
    return records_;
  }

 private:
  std::vector<std::tuple<int, int>> records_;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // EXCEPTION_TABLE_ARM_H

#ifndef OBJECTS_H
#define OBJECTS_H
#include "src/globals.h"
namespace v8 {
namespace internal {
class Code {
 public:
  static const size_t kHeaderSize = 64;
};
class HeapNumber {
 public:
  static const uint32_t kSignMask = 0x80000000u;
  static const uint32_t kExponentMask = 0x7ff00000u;
  static const uint32_t kMantissaMask = 0xfffffu;
  static const int kMantissaBits = 52;
  static const int kExponentBits = 11;
  static const int kExponentBias = 1023;
  static const int kExponentShift = 20;
  static const int kInfinityOrNanExponent =
      (kExponentMask >> kExponentShift) - kExponentBias;
  static const int kMantissaBitsInTopWord = 20;
  static const int kNonMantissaBitsInTopWord = 12;
};
}  // namespace internal
}  // namespace v8
#endif  // OBJECTS_H

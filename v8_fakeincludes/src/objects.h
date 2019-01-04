#ifndef OBJECTS_H
#define OBJECTS_H
#include "src/globals.h"
namespace v8 {
namespace internal {
class Code {
public:
  static const size_t kHeaderSize = 64;
};
}
}
#endif  // OBJECTS_H

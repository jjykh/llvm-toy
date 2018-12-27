#ifndef V8_CODEGEN_H
#define V8_CODEGEN_H
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
struct CompilerState;
Handle<Code> GenerateCode(Isolate* isolate, const CompilerState& state);
}
}
}

#endif  // V8_CODEGEN_H

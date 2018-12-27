#ifndef V8_PASS_MANAGER_H
#define V8_PASS_MANAGER_H
#include "src/objects.h"
namespace v8 {
namespace internal {
namespace compiler {
class Schedule;
class CallDescriptor;
}
namespace tf_llvm {
class V8PassManager {
 public:
  Handle<Code> Run(Isolate* isolate, compiler::Schedule*,
                   compiler::CallDescriptor*, const char* name);
};
}
}
}

#endif  // V8_PASS_MANAGER_:

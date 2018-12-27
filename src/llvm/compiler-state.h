#ifndef COMPILER_STATE_H
#define COMPILER_STATE_H
#include <stdint.h>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "src/llvm/llvm-headers.h"
#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
class StackMapInfo;
typedef std::vector<uint8_t> ByteBuffer;
typedef std::list<ByteBuffer> BufferList;
typedef std::list<std::string> StringList;

struct CompilerState {
  BufferList codeSectionList_;
  BufferList dataSectionList_;
  StringList codeSectionNames_;
  StringList dataSectionNames_;
  ByteBuffer* stackMapsSection_;
  StackMapInfoMap stack_map_info_map;
  LLVMModuleRef module_;
  LLVMValueRef function_;
  LLVMContextRef context_;
  void* entryPoint_;
  size_t spill_slot_count_;
  bool needs_frame_;
  CompilerState(const char* moduleName);
  ~CompilerState();
  CompilerState(const CompilerState&) = delete;
  const CompilerState& operator=(const CompilerState&) = delete;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // COMPILER_STATE_H

#ifndef COMPILER_STATE_H
#define COMPILER_STATE_H
#include <stdint.h>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "src/llvm/llvm-headers.h"
#include "src/llvm/load-constant-recorder.h"
#include "src/llvm/stack-map-info.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
class StackMapInfo;
typedef std::vector<uint8_t> ByteBuffer;
typedef std::list<ByteBuffer> BufferList;
typedef std::list<std::string> StringList;

enum class PrologueKind { Unset, CFunctionCall, JSFunctionCall, Stub };

struct CompilerState {
  BufferList codeSectionList_;
  BufferList dataSectionList_;
  StringList codeSectionNames_;
  StringList dataSectionNames_;
  StackMapInfoMap stack_map_info_map_;
  LoadConstantRecorder load_constant_recorder_;
  ByteBuffer* stackMapsSection_;
  LLVMModuleRef module_;
  LLVMValueRef function_;
  LLVMContextRef context_;
  void* entryPoint_;
  int code_kind_;
  PrologueKind prologue_kind_;
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

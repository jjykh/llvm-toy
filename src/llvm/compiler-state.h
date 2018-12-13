#ifndef COMPILER_STATE_H
#define COMPILER_STATE_H
#include <stdint.h>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include "src/llvm/llvm-headers.h"
namespace v8 {
namespace internal {
namespace tf_llvm {
typedef std::vector<uint8_t> ByteBuffer;
typedef std::list<ByteBuffer> BufferList;
typedef std::list<std::string> StringList;

struct CompilerState {
  BufferList codeSectionList_;
  BufferList dataSectionList_;
  StringList codeSectionNames_;
  StringList dataSectionNames_;
  ByteBuffer* stackMapsSection_;
  LLVMModuleRef module_;
  LLVMValueRef function_;
  LLVMContextRef context_;
  void* entryPoint_;
  CompilerState(const char* moduleName);
  ~CompilerState();
  CompilerState(const CompilerState&) = delete;
  const CompilerState& operator=(const CompilerState&) = delete;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // COMPILER_STATE_H

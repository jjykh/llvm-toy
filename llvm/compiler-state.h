#ifndef COMPILER_STATE_H
#define COMPILER_STATE_H
#include <stdint.h>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include "PlatformDesc.h"
#include "llvm-headers.h"
namespace jit {
enum class PatchType {
  Direct,
  Indirect,
  Assist,
};

struct PatchDesc {
  PatchType type_;
};

typedef std::vector<uint8_t> ByteBuffer;
typedef std::list<ByteBuffer> BufferList;
typedef std::list<std::string> StringList;
typedef std::unordered_map<unsigned /* stackmaps id */, PatchDesc> PatchMap;

struct CompilerState {
  BufferList codeSectionList_;
  BufferList dataSectionList_;
  StringList codeSectionNames_;
  StringList dataSectionNames_;
  ByteBuffer* stackMapsSection_;
  PatchMap patchMap_;
  LLVMModuleRef module_;
  LLVMValueRef function_;
  LLVMContextRef context_;
  void* entryPoint_;
  struct PlatformDesc platformDesc_;
  CompilerState(const char* moduleName);
  ~CompilerState();
  CompilerState(const CompilerState&) = delete;
  const CompilerState& operator=(const CompilerState&) = delete;
};
}  // namespace jit
#endif  // COMPILER_STATE_H

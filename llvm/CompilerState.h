#ifndef COMPILERSTATE_H
#define COMPILERSTATE_H
#include <vector>
#include <list>
#include <string>
#include <stdint.h>
#include "LLVMHeaders.h"
namespace jit {
typedef std::vector<uint8_t> ByteBuffer;
typedef std::list<ByteBuffer> BufferList;
typedef std::list<std::string> StringList;
struct CompilerState {
    BufferList m_codeSectionList;
    BufferList m_dataSectionList;
    StringList m_codeSectionNames;
    StringList m_dataSectionNames;
    ByteBuffer* m_stackMapsSection;
    LLVMModuleRef m_module;
    LLVMValueRef m_function;
    LLVMContextRef m_context;
    void* m_entryPoint;
    CompilerState(const char* moduleName);
    ~CompilerState();
    CompilerState(const CompilerState&) = delete;
    const CompilerState& operator=(const CompilerState&) = delete;
};
}

#endif /* COMPILERSTATE_H */

#ifndef COMPILERSTATE_H
#define COMPILERSTATE_H
#include <vector>
#include <list>
#include <string>
#include <stdint.h>
namespace jit {
typedef std::vector<uint8_t> ByteBuffer;
typedef std::list<ByteBuffer> BufferList;
typedef std::list<std::string> StringList;
struct CompilerState
{
    BufferList m_codeSectionList;
    BufferList m_dataSectionList;
    StringList m_codeSectionNames;
    StringList m_dataSectionNames;
    ByteBuffer* m_stackMapsSection;
};
}

#endif /* COMPILERSTATE_H */

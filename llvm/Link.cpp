#include <assert.h>
#include "StackMaps.h"
#include "CompilerState.h"
#include "Link.h"

namespace jit {

void link(CompilerState& state)
{
    StackMaps sm;
    DataView dv(state.m_stackMapsSection->data());
    sm.parse(&dv);
    auto rm = sm.computeRecordMap();
    assert(state.m_codeSectionList.size() == 1);
    uint8_t* code = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(state.m_codeSectionList.front().data()));
    auto recordPairs = rm.find(0);
    assert(recordPairs != rm.end());
    auto records = recordPairs->second;
}
}

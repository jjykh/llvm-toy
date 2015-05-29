#include <assert.h>
#include "StackMaps.h"
#include "CompilerState.h"
#include "Abbreviations.h"
#include "Link.h"

namespace jit {

inline static uint8_t rexAMode_R__wrk(unsigned gregEnc3210, unsigned eregEnc3210)
{
    uint8_t W = 1; /* we want 64-bit mode */
    uint8_t R = (gregEnc3210 >> 3) & 1;
    uint8_t X = 0; /* not relevant */
    uint8_t B = (eregEnc3210 >> 3) & 1;
    return 0x40 + ((W << 3) | (R << 2) | (X << 1) | (B << 0));
}

static inline unsigned iregEnc3210(unsigned in)
{
    return in;
}

static uint8_t rexAMode_R(unsigned greg, unsigned ereg)
{
    return rexAMode_R__wrk(iregEnc3210(greg), iregEnc3210(ereg));
}

inline static uint8_t mkModRegRM(unsigned mod, unsigned reg, unsigned regmem)
{
    return (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (regmem & 7));
}

inline static uint8_t* doAMode_R__wrk(uint8_t* p, unsigned gregEnc3210, unsigned eregEnc3210)
{
    *p++ = mkModRegRM(3, gregEnc3210 & 7, eregEnc3210 & 7);
    return p;
}

static uint8_t* doAMode_R(uint8_t* p, unsigned greg, unsigned ereg)
{
    return doAMode_R__wrk(p, iregEnc3210(greg), iregEnc3210(ereg));
}

static uint8_t* emit64(uint8_t* p, uint64_t w64)
{
    *reinterpret_cast<uint64_t*>(p) = w64;
    return p + sizeof(w64);
}

static void linkChain(CompilerState& state, const StackMaps::RecordMap& rm, uint8_t* code, void* dispChain)
{
    auto recordPairs = rm.find(chainPatchId());
    assert(recordPairs != rm.end());
    auto records = recordPairs->second;
    for (auto& record : records) {
        auto p = code + record.instructionOffset;
        // movabs xxx, %r11
        *p++ = 0x49;
        *p++ = 0xBB;
        p = emit64(p, reinterpret_cast<uint64_t>(dispChain));
        /* 3 bytes: jmp *%r11 */
        *p++ = 0x41;
        *p++ = 0xFF;
        *p++ = 0xE3;
    }
}

void link(CompilerState& state, void* dispChain, void* dispXind)
{
    StackMaps sm;
    DataView dv(state.m_stackMapsSection->data());
    sm.parse(&dv);
    auto rm = sm.computeRecordMap();
    assert(state.m_codeSectionList.size() == 1);
    uint8_t* code = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(state.m_codeSectionList.front().data()));
    linkChain(state, rm, code, dispChain);
}
}

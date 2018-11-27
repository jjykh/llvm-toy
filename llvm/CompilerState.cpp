#include "CompilerState.h"

namespace jit {

CompilerState::CompilerState(const char* moduleName, const PlatformDesc& desc)
    : m_stackMapsSection(nullptr)
    , m_module(nullptr)
    , m_function(nullptr)
    , m_context(nullptr)
    , m_entryPoint(nullptr)
    , m_platformDesc(desc)
{
    m_context = LLVMContextCreate();
    m_module = LLVMModuleCreateWithNameInContext("test", m_context);
#if 0
    LLVMSetTarget(m_module, "x86_64-unknown-linux-gnu");
#else
    LLVMSetTarget(m_module, "armv7-linux-android");
#endif
}

CompilerState::~CompilerState()
{
    LLVMContextDispose(m_context);
}
}

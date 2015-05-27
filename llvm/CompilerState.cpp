#include "LLVMAPI.h"
#include "CompilerState.h"

namespace jit {

CompilerState::CompilerState(const char* moduleName)
{
    m_context = llvmAPI->ContextCreate();
    m_module = llvmAPI->ModuleCreateWithNameInContext("test", m_context);
}

CompilerState::~CompilerState()
{
    llvmAPI->DisposeModule(m_module);
    llvmAPI->ContextDispose(m_context);
}

}

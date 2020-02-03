#ifndef TARGET_SPECIFIC_H
#define TARGET_SPECIFIC_H
#if V8_TARGET_ARCH_ARM
static const int kV8CCRegisterParameterCount = 12;
static const int kV8CCMaxStackParameterToReg = 6;
static const int kARMRegParameterNotAllocatable = (1 << 10 | 1 << 11);
static const int kDwarfGenernalRegEnd = 15;
static const int kRootReg = 10;
static const int kFPReg = 11;

static const int kTargetRegParameterNotAllocatable =
    kARMRegParameterNotAllocatable;
#else
#error Unsupported target architecture.
#endif

#endif  // TARGET_SPECIFIC_H

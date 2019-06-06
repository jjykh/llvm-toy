#ifndef GLOBALS_H
#define GLOBALS_H
const int kPageSizeBits = 19;
const int kHeapObjectTag = 1;
const int kHeapObjectTagSize = 2;
const int kCharSize = sizeof(char);
const int kShortSize = sizeof(short);  // NOLINT
const int kIntSize = sizeof(int);
const int kInt32Size = sizeof(int32_t);
const int kInt64Size = sizeof(int64_t);
const int kUInt32Size = sizeof(uint32_t);
const int kSizetSize = sizeof(size_t);
const int kFloatSize = sizeof(float);
const int kDoubleSize = sizeof(double);
const int kIntptrSize = sizeof(intptr_t);
const int kUIntptrSize = sizeof(uintptr_t);
const int kPointerSize = sizeof(void*);

#if defined(UC_BUILD_TF_LLVM_BACKEND) && UC_BUILD_TF_LLVM_BACKEND
#define FEATURE_USE_SAMPLE_PGO
#define FEATURE_SAMPLE_PGO
#endif

#endif  // GLOBALS_H

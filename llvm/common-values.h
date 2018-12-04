#ifndef COMMON_VALUES_H
#define COMMON_VALUES_H
#include "abbreviations.h"

namespace jit {

class CommonValues {
 public:
  CommonValues(LContext context);
  CommonValues(const CommonValues&) = delete;
  const CommonValues& operator=(const CommonValues&) = delete;

  void initialize(LModule module) { module_ = module; }

  const LType voidType;
  const LType boolean;
  const LType int1;
  const LType int8;
  const LType int16;
  const LType int32;
  const LType int64;
  const LType intPtr;
  const LType floatType;
  const LType doubleType;
  const LType tokenType;
  const LType ref8;
  const LType ref16;
  const LType ref32;
  const LType ref64;
  const LType refPtr;
  const LType refFloat;
  const LType refDouble;
  const LType taggedType;
  const LValue booleanTrue;
  const LValue booleanFalse;
  const LValue int8Zero;
  const LValue int32Zero;
  const LValue int32One;
  const LValue int64Zero;
  const LValue intPtrZero;
  const LValue intPtrOne;
  const LValue intPtrTwo;
  const LValue intPtrThree;
  const LValue intPtrFour;
  const LValue intPtrEight;
  const LValue intPtrPtr;
  const LValue doubleZero;

  const unsigned rangeKind;
  const unsigned profKind;
  const LValue branchWeights;

  LContext const context_;
  LModule module_;
};
}  // namespace jit
#endif  // COMMON_VALUES_H

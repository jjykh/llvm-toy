#ifndef MACHINE_TYPE_H
#define MACHINE_TYPE_H
namespace v8 {
namespace internal {

enum class MachineRepresentation {
  kNone,
  kBit,
  kWord8,
  kWord16,
  kWord32,
  kWord64,
  kTaggedSigned,
  kTaggedPointer,
  kTagged,
  // FP representations must be last, and in order of increasing size.
  kFloat32,
  kFloat64,
  kSimd128,
  kFirstFPRepresentation = kFloat32,
  kLastRepresentation = kSimd128
};

enum class MachineSemantic {
  kNone,
  kBool,
  kInt32,
  kUint32,
  kInt64,
  kUint64,
  kNumber,
  kAny
};

enum WriteBarrierKind : uint8_t {
  kNoWriteBarrier,
  kMapWriteBarrier,
  kPointerWriteBarrier,
  kFullWriteBarrier
};
}
}
#endif  // MACHINE_TYPE_H

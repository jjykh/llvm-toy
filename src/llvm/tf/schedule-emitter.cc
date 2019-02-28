#include "src/llvm/tf/schedule-emitter.h"

#include <unordered_map>
#include "src/compiler/common-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/schedule.h"
#include "src/llvm/log.h"
#include "src/llvm/tf/tf-visitor.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
ScheduleEmitter::ScheduleEmitter(Isolate* isolate, compiler::Schedule* schedule,
                                 compiler::CallDescriptor* incoming_descriptor)
    : isolate_(isolate),
      schedule_(schedule),
      incoming_descriptor_(incoming_descriptor),
      current_block_(nullptr) {}

ScheduleEmitter::~ScheduleEmitter() {}

void ScheduleEmitter::Visit(TFVisitor* visitor) { DoVisit(visitor); }

void ScheduleEmitter::DoVisit(TFVisitor* visitor) {
  compiler::BasicBlockVector* blocks = schedule()->rpo_order();
  for (auto const block : *blocks) {
    VisitBlock(block, visitor);
    for (auto i = block->begin(); i != block->end(); ++i) {
      VisitNode(*i, visitor);
    }
    VisitBlockControl(block, visitor);
  }
}

static bool CanProduceSignalingNaN(compiler::Node* node) {
  // TODO(jarin) Improve the heuristic here.
  if (node->opcode() == compiler::IrOpcode::kFloat64Add ||
      node->opcode() == compiler::IrOpcode::kFloat64Sub ||
      node->opcode() == compiler::IrOpcode::kFloat64Mul) {
    return false;
  }
  return true;
}

void ScheduleEmitter::VisitNode(compiler::Node* node, TFVisitor* visitor) {
  switch (node->opcode()) {
    case compiler::IrOpcode::kStart:
    case compiler::IrOpcode::kLoop:
    case compiler::IrOpcode::kEnd:
    case compiler::IrOpcode::kBranch:
    case compiler::IrOpcode::kIfTrue:
    case compiler::IrOpcode::kIfFalse:
    case compiler::IrOpcode::kIfSuccess:
    case compiler::IrOpcode::kSwitch:
    case compiler::IrOpcode::kEffectPhi:
    case compiler::IrOpcode::kMerge:
    case compiler::IrOpcode::kTerminate:
    case compiler::IrOpcode::kBeginRegion:
      // No code needed for these graph artifacts.
      return;
    case compiler::IrOpcode::kIfValue:
      visitor->VisitIfValue(node->id(),
                            compiler::OpParameter<int32_t>(node->op()));
      return;
    case compiler::IrOpcode::kIfDefault:
      visitor->VisitIfDefault(node->id());
      return;
    case compiler::IrOpcode::kIfException:
      visitor->VisitIfException(node->id());
      return;
    case compiler::IrOpcode::kFinishRegion:
      UNREACHABLE();
    case compiler::IrOpcode::kParameter: {
      int index = compiler::ParameterIndexOf(node->op());
      visitor->VisitParameter(node->id(), index);
      return;
    }
    case compiler::IrOpcode::kOsrValue:
      UNREACHABLE();
    case compiler::IrOpcode::kPhi: {
      MachineRepresentation rep = compiler::PhiRepresentationOf(node->op());
      if (rep == MachineRepresentation::kNone) return;
      const int input_count = node->op()->ValueInputCount();
      OperandsVector inputs;
      for (int i = 0; i < input_count; ++i) {
        compiler::Node* const input = node->InputAt(i);
        inputs.push_back(input->id());
      }
      visitor->VisitPhi(node->id(), rep, inputs);
      return;
    }
    case compiler::IrOpcode::kProjection:
      visitor->VisitProjection(node->id(), node->InputAt(0)->id(),
                               ProjectionIndexOf(node->op()));
      return;
    case compiler::IrOpcode::kInt32Constant:
      visitor->VisitInt32Constant(node->id(),
                                  compiler::OpParameter<int32_t>(node));
      return;
    case compiler::IrOpcode::kExternalConstant: {
      const ExternalReference& external_reference =
          compiler::OpParameter<ExternalReference>(node);
      int64_t magic = reinterpret_cast<int64_t>(external_reference.address());
      visitor->VisitExternalConstant(node->id(), magic);
    }
      return;
    case compiler::IrOpcode::kInt64Constant:
    case compiler::IrOpcode::kRelocatableInt32Constant:
    case compiler::IrOpcode::kRelocatableInt64Constant:
    case compiler::IrOpcode::kFloat32Constant:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Constant:
      visitor->VisitFloat64Constant(node->id(),
                                    compiler::OpParameter<double>(node));
      return;
    case compiler::IrOpcode::kHeapConstant: {
      Handle<HeapObject> object =
          compiler::OpParameter<Handle<HeapObject>>(node);
      Heap::RootListIndex index;
      if (IsMaterializableFromRoot(object, &index)) {
        visitor->VisitRoot(node->id(), static_cast<int>(index));
        return;
      } else if (object->IsCode()) {
        auto uses = node->uses();
        if (uses.empty()) return;  // WTF???
        auto iterator = uses.begin();
        auto expected_call = *iterator;
        ++iterator;
        if (((expected_call->opcode() == compiler::IrOpcode::kCall) ||
             (expected_call->opcode() == compiler::IrOpcode::kTailCall)) &&
            (iterator == uses.end())) {
          visitor->VisitCodeForCall(
              node->id(), reinterpret_cast<int64_t>(object.location()));
          return;
        }
      }
      visitor->VisitHeapConstant(node->id(),
                                 reinterpret_cast<int64_t>(object.location()));
    }
      return;
    case compiler::IrOpcode::kNumberConstant: {
      double value = compiler::OpParameter<double>(node);
      int smi;
      if (DoubleToSmiInteger(value, &smi)) {
        visitor->VisitSmiConstant(node->id(), Smi::FromInt(smi));
        return;
      }
      Handle<HeapNumber> num = isolate_->factory()->NewHeapNumber(value);
      visitor->VisitHeapConstant(node->id(),
                                 reinterpret_cast<int64_t>(num.location()));
    }
      return;
    case compiler::IrOpcode::kCall:
      // Only handle when node != invoke
      if (ShouldEmitCall(node)) VisitCall(node, visitor, false);
      return;
    case compiler::IrOpcode::kCallWithCallerSavedRegisters:
      VisitCCall(node, visitor);
      return;
    case compiler::IrOpcode::kDeoptimizeIf:
      UNREACHABLE();
    case compiler::IrOpcode::kDeoptimizeUnless:
      UNREACHABLE();
    case compiler::IrOpcode::kTrapIf:
      UNREACHABLE();
    case compiler::IrOpcode::kTrapUnless:
      UNREACHABLE();
    case compiler::IrOpcode::kFrameState:
    case compiler::IrOpcode::kStateValues:
    case compiler::IrOpcode::kObjectState:
      return;
    case compiler::IrOpcode::kDebugAbort:
    // FIXME(UC_linzj): Lazy implementation.
    case compiler::IrOpcode::kDebugBreak:
      visitor->VisitDebugBreak(node->id());
      return;
    case compiler::IrOpcode::kComment:
      UNREACHABLE();
      return;
    case compiler::IrOpcode::kRetain:
      UNREACHABLE();
      return;
    case compiler::IrOpcode::kLoad: {
      compiler::LoadRepresentation type =
          compiler::LoadRepresentationOf(node->op());
      compiler::Node* base = node->InputAt(0);
      compiler::Node* index = node->InputAt(1);
      visitor->VisitLoad(node->id(), type.representation(), type.semantic(),
                         base->id(), index->id());
    }
      return;
    case compiler::IrOpcode::kStore: {
      compiler::StoreRepresentation store_rep =
          StoreRepresentationOf(node->op());
      WriteBarrierKind write_barrier_kind = store_rep.write_barrier_kind();
      MachineRepresentation rep = store_rep.representation();
      compiler::Node* base = node->InputAt(0);
      compiler::Node* index = node->InputAt(1);
      compiler::Node* value = node->InputAt(2);
      visitor->VisitStore(node->id(), rep, write_barrier_kind, base->id(),
                          index->id(), value->id());
    }
      return;
    case compiler::IrOpcode::kProtectedStore:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32And: {
      visitor->VisitWord32And(node->id(), node->InputAt(0)->id(),
                              node->InputAt(1)->id());
    }
      return;
    case compiler::IrOpcode::kWord32Or: {
      visitor->VisitWord32Or(node->id(), node->InputAt(0)->id(),
                             node->InputAt(1)->id());
    }
      return;
    case compiler::IrOpcode::kWord32Xor:
      visitor->VisitWord32Xor(node->id(), node->InputAt(0)->id(),
                              node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kWord32Shl:
      visitor->VisitWord32Shl(node->id(), node->InputAt(0)->id(),
                              node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kWord32Shr:
      visitor->VisitWord32Shr(node->id(), node->InputAt(0)->id(),
                              node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kWord32Sar:
      visitor->VisitWord32Sar(node->id(), node->InputAt(0)->id(),
                              node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kWord32Ror:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32Equal:
      visitor->VisitWord32Equal(node->id(), node->InputAt(0)->id(),
                                node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kWord32Clz:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32Ctz:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32ReverseBits:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32ReverseBytes:
      UNREACHABLE();
    case compiler::IrOpcode::kInt32AbsWithOverflow:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32Popcnt:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Popcnt:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64And:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Or:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Xor:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Shl:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Shr:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Sar:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Ror:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Clz:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Ctz:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64ReverseBits:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64ReverseBytes:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64AbsWithOverflow:
      UNREACHABLE();
    case compiler::IrOpcode::kWord64Equal:
      UNREACHABLE();
    case compiler::IrOpcode::kInt32Add:
      visitor->VisitInt32Add(node->id(), node->InputAt(0)->id(),
                             node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32AddWithOverflow:
      visitor->VisitInt32AddWithOverflow(node->id(), node->InputAt(0)->id(),
                                         node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32Sub:
      visitor->VisitInt32Sub(node->id(), node->InputAt(0)->id(),
                             node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32SubWithOverflow:
      visitor->VisitInt32SubWithOverflow(node->id(), node->InputAt(0)->id(),
                                         node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32Mul:
      visitor->VisitInt32Mul(node->id(), node->InputAt(0)->id(),
                             node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32MulWithOverflow:
      visitor->VisitInt32MulWithOverflow(node->id(), node->InputAt(0)->id(),
                                         node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32MulHigh:
      UNREACHABLE();
    case compiler::IrOpcode::kInt32Div:
      visitor->VisitInt32Div(node->id(), node->InputAt(0)->id(),
                             node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32Mod:
      visitor->VisitInt32Mod(node->id(), node->InputAt(0)->id(),
                             node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32LessThan:
      visitor->VisitInt32LessThan(node->id(), node->InputAt(0)->id(),
                                  node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kInt32LessThanOrEqual:
      visitor->VisitInt32LessThanOrEqual(node->id(), node->InputAt(0)->id(),
                                         node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kUint32Div:
      UNREACHABLE();
    case compiler::IrOpcode::kUint32LessThan:
      visitor->VisitUint32LessThan(node->id(), node->InputAt(0)->id(),
                                   node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kUint32LessThanOrEqual:
      visitor->VisitUint32LessThanOrEqual(node->id(), node->InputAt(0)->id(),
                                          node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kUint32Mod:
      UNREACHABLE();
    case compiler::IrOpcode::kUint32MulHigh:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64Add:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64AddWithOverflow:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64Sub:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64SubWithOverflow:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64Mul:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64Div:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64Mod:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64LessThan:
      UNREACHABLE();
    case compiler::IrOpcode::kInt64LessThanOrEqual:
      UNREACHABLE();
    case compiler::IrOpcode::kUint64Div:
      UNREACHABLE();
    case compiler::IrOpcode::kUint64LessThan:
      UNREACHABLE();
    case compiler::IrOpcode::kUint64LessThanOrEqual:
      UNREACHABLE();
    case compiler::IrOpcode::kUint64Mod:
      UNREACHABLE();
    case compiler::IrOpcode::kBitcastTaggedToWord:
      UNREACHABLE();
    case compiler::IrOpcode::kBitcastWordToTagged:
      visitor->VisitBitcastWordToTagged(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kBitcastWordToTaggedSigned:
      UNREACHABLE();
    case compiler::IrOpcode::kChangeFloat32ToFloat64:
      visitor->VisitChangeFloat32ToFloat64(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kChangeInt32ToFloat64:
      visitor->VisitChangeInt32ToFloat64(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kChangeUint32ToFloat64:
      visitor->VisitChangeUint32ToFloat64(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kChangeFloat64ToInt32:
      UNREACHABLE();
    case compiler::IrOpcode::kChangeFloat64ToUint32:
      UNREACHABLE();
    case compiler::IrOpcode::kChangeFloat64ToUint64:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64SilenceNaN:
      if (CanProduceSignalingNaN(node->InputAt(0))) {
        visitor->VisitFloat64SilenceNaN(node->id(), node->InputAt(0)->id());
      } else {
        visitor->VisitIdentity(node->id(), node->InputAt(0)->id());
      }
      return;
    case compiler::IrOpcode::kTruncateFloat64ToUint32:
      UNREACHABLE();
    case compiler::IrOpcode::kTruncateFloat32ToInt32:
      UNREACHABLE();
    case compiler::IrOpcode::kTruncateFloat32ToUint32:
      UNREACHABLE();
    case compiler::IrOpcode::kTryTruncateFloat32ToInt64:
      UNREACHABLE();
    case compiler::IrOpcode::kTryTruncateFloat64ToInt64:
      UNREACHABLE();
    case compiler::IrOpcode::kTryTruncateFloat32ToUint64:
      UNREACHABLE();
    case compiler::IrOpcode::kTryTruncateFloat64ToUint64:
      UNREACHABLE();
    case compiler::IrOpcode::kChangeInt32ToInt64:
      UNREACHABLE();
    case compiler::IrOpcode::kChangeUint32ToUint64:
      UNREACHABLE();
    case compiler::IrOpcode::kTruncateFloat64ToFloat32:
      visitor->VisitTruncateFloat64ToFloat32(node->id(),
                                             node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kTruncateFloat64ToWord32:
      visitor->VisitTruncateFloat64ToWord32(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kTruncateInt64ToInt32:
      UNREACHABLE();
    case compiler::IrOpcode::kRoundFloat64ToInt32:
      visitor->VisitRoundFloat64ToInt32(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kRoundInt64ToFloat32:
      UNREACHABLE();
    case compiler::IrOpcode::kRoundInt32ToFloat32:
      visitor->VisitRoundInt32ToFloat32(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kRoundInt64ToFloat64:
      UNREACHABLE();
    case compiler::IrOpcode::kBitcastFloat32ToInt32:
      UNREACHABLE();
    case compiler::IrOpcode::kRoundUint32ToFloat32:
      UNREACHABLE();
    case compiler::IrOpcode::kRoundUint64ToFloat32:
      UNREACHABLE();
    case compiler::IrOpcode::kRoundUint64ToFloat64:
      UNREACHABLE();
    case compiler::IrOpcode::kBitcastFloat64ToInt64:
      UNREACHABLE();
    case compiler::IrOpcode::kBitcastInt32ToFloat32:
      UNREACHABLE();
    case compiler::IrOpcode::kBitcastInt64ToFloat64:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Add:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Sub:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Neg:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Mul:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Div:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Abs:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Sqrt:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Equal:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32LessThan:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32LessThanOrEqual:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Max:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32Min:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Add:
      visitor->VisitFloat64Add(node->id(), node->InputAt(0)->id(),
                               node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kFloat64Sub:
      visitor->VisitFloat64Sub(node->id(), node->InputAt(0)->id(),
                               node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kFloat64Neg:
      visitor->VisitFloat64Neg(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kFloat64Mul:
      visitor->VisitFloat64Mul(node->id(), node->InputAt(0)->id(),
                               node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kFloat64Div:
      visitor->VisitFloat64Div(node->id(), node->InputAt(0)->id(),
                               node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kFloat64Mod:
      visitor->VisitFloat64Mod(node->id(), node->InputAt(0)->id(),
                               node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kFloat64Min:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Max:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Abs:
      visitor->VisitFloat64Abs(node->id(), node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kFloat64Acos:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Acosh:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Asin:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Asinh:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Atan:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Atanh:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Atan2:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Cbrt:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Cos:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Cosh:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Exp:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Expm1:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Log:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Log1p:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Log10:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Log2:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Pow:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Sin:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Sinh:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Sqrt:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Tan:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Tanh:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64Equal:
      visitor->VisitFloat64Equal(node->id(), node->InputAt(0)->id(),
                                 node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kFloat64LessThan:
      visitor->VisitFloat64LessThan(node->id(), node->InputAt(0)->id(),
                                    node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kFloat64LessThanOrEqual:
      visitor->VisitFloat64LessThanOrEqual(node->id(), node->InputAt(0)->id(),
                                           node->InputAt(1)->id());
      return;
    case compiler::IrOpcode::kFloat32RoundDown:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64RoundDown:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32RoundUp:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64RoundUp:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32RoundTruncate:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64RoundTruncate:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64RoundTiesAway:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat32RoundTiesEven:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64RoundTiesEven:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64ExtractLowWord32:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64ExtractHighWord32:
      visitor->VisitFloat64ExtractHighWord32(node->id(),
                                             node->InputAt(0)->id());
      return;
    case compiler::IrOpcode::kFloat64InsertLowWord32:
      UNREACHABLE();
    case compiler::IrOpcode::kFloat64InsertHighWord32:
      UNREACHABLE();
    case compiler::IrOpcode::kStackSlot:
      UNREACHABLE();
    case compiler::IrOpcode::kLoadStackPointer:
      visitor->VisitLoadStackPointer(node->id());
      return;
    case compiler::IrOpcode::kLoadFramePointer:
      visitor->VisitLoadFramePointer(node->id());
      return;
    case compiler::IrOpcode::kLoadParentFramePointer:
      visitor->VisitLoadParentFramePointer(node->id());
      return;
    case compiler::IrOpcode::kUnalignedLoad:
      UNREACHABLE();
    case compiler::IrOpcode::kUnalignedStore:
      UNREACHABLE();
    case compiler::IrOpcode::kCheckedLoad:
      UNREACHABLE();
    case compiler::IrOpcode::kCheckedStore:
      UNREACHABLE();
    case compiler::IrOpcode::kInt32PairAdd:
      UNREACHABLE();
    case compiler::IrOpcode::kInt32PairSub:
      UNREACHABLE();
    case compiler::IrOpcode::kInt32PairMul:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32PairShl:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32PairShr:
      UNREACHABLE();
    case compiler::IrOpcode::kWord32PairSar:
      UNREACHABLE();
    case compiler::IrOpcode::kAtomicLoad:
      UNREACHABLE();
    case compiler::IrOpcode::kAtomicStore:
      UNREACHABLE();
#define ATOMIC_CASE(name)                   \
  case compiler::IrOpcode::kAtomic##name: { \
    UNREACHABLE();                          \
  }
      ATOMIC_CASE(Exchange)
      ATOMIC_CASE(CompareExchange)
      ATOMIC_CASE(Add)
      ATOMIC_CASE(Sub)
      ATOMIC_CASE(And)
      ATOMIC_CASE(Or)
      ATOMIC_CASE(Xor)
#undef ATOMIC_CASE
    case compiler::IrOpcode::kProtectedLoad:
      UNREACHABLE();
    case compiler::IrOpcode::kUnsafePointerAdd:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Splat:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4ExtractLane:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4ReplaceLane:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4SConvertI32x4:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4UConvertI32x4:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Abs:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Neg:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4RecipApprox:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4RecipSqrtApprox:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Add:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4AddHoriz:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Sub:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Mul:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Min:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Max:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Eq:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Ne:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Lt:
      UNREACHABLE();
    case compiler::IrOpcode::kF32x4Le:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4Splat:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4ExtractLane:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4ReplaceLane:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4SConvertF32x4:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4SConvertI16x8Low:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4SConvertI16x8High:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4Neg:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4Shl:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4ShrS:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4Add:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4AddHoriz:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4Sub:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4Mul:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4MinS:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4MaxS:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4Eq:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4Ne:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4GtS:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4GeS:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4UConvertF32x4:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4UConvertI16x8Low:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4UConvertI16x8High:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4ShrU:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4MinU:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4MaxU:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4GtU:
      UNREACHABLE();
    case compiler::IrOpcode::kI32x4GeU:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8Splat:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8ExtractLane:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8ReplaceLane:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8SConvertI8x16Low:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8SConvertI8x16High:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8Neg:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8Shl:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8ShrS:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8SConvertI32x4:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8Add:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8AddSaturateS:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8AddHoriz:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8Sub:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8SubSaturateS:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8Mul:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8MinS:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8MaxS:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8Eq:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8Ne:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8GtS:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8GeS:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8UConvertI8x16Low:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8UConvertI8x16High:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8ShrU:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8UConvertI32x4:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8AddSaturateU:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8SubSaturateU:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8MinU:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8MaxU:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8GtU:
      UNREACHABLE();
    case compiler::IrOpcode::kI16x8GeU:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16Splat:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16ExtractLane:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16ReplaceLane:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16Neg:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16Shl:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16ShrS:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16SConvertI16x8:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16Add:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16AddSaturateS:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16Sub:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16SubSaturateS:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16Mul:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16MinS:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16MaxS:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16Eq:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16Ne:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16GtS:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16GeS:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16ShrU:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16UConvertI16x8:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16AddSaturateU:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16SubSaturateU:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16MinU:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16MaxU:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16GtU:
      UNREACHABLE();
    case compiler::IrOpcode::kI8x16GeU:
      UNREACHABLE();
    case compiler::IrOpcode::kS128Zero:
      UNREACHABLE();
    case compiler::IrOpcode::kS128And:
      UNREACHABLE();
    case compiler::IrOpcode::kS128Or:
      UNREACHABLE();
    case compiler::IrOpcode::kS128Xor:
      UNREACHABLE();
    case compiler::IrOpcode::kS128Not:
      UNREACHABLE();
    case compiler::IrOpcode::kS128Select:
      UNREACHABLE();
    case compiler::IrOpcode::kS8x16Shuffle:
      UNREACHABLE();
    case compiler::IrOpcode::kS1x4AnyTrue:
      UNREACHABLE();
    case compiler::IrOpcode::kS1x4AllTrue:
      UNREACHABLE();
    case compiler::IrOpcode::kS1x8AnyTrue:
      UNREACHABLE();
    case compiler::IrOpcode::kS1x8AllTrue:
      UNREACHABLE();
    case compiler::IrOpcode::kS1x16AnyTrue:
      UNREACHABLE();
    case compiler::IrOpcode::kS1x16AllTrue:
      UNREACHABLE();
    default:
      V8_Fatal(__FILE__, __LINE__, "Unexpected operator #%d:%s @ node #%d",
               node->opcode(), node->op()->mnemonic(), node->id());
      break;
  }
}

void ScheduleEmitter::VisitBlock(compiler::BasicBlock* bb, TFVisitor* visitor) {
  current_block_ = bb;
  int id = bb->rpo_number();
  OperandsVector predecessors;
  for (auto pred : bb->predecessors()) {
    int rpo_number = pred->rpo_number();
    predecessors.push_back(rpo_number);
  }
  visitor->VisitBlock(id, bb->deferred(), predecessors);
}

bool ScheduleEmitter::IsMaterializableFromRoot(
    Handle<HeapObject> object, Heap::RootListIndex* index_return) {
  const compiler::CallDescriptor* my_incoming_descriptor =
      incoming_descriptor();
  if (my_incoming_descriptor->flags() &
      compiler::CallDescriptor::kCanUseRoots) {
    Heap* heap = isolate()->heap();
    return heap->IsRootHandle(object, index_return) &&
           !heap->RootCanBeWrittenAfterInitialization(*index_return);
  }
  return false;
}

void ScheduleEmitter::VisitBlockControl(compiler::BasicBlock* block,
                                        TFVisitor* visitor) {
  compiler::Node* input = block->control_input();
  switch (block->control()) {
    case compiler::BasicBlock::kThrow:
    case compiler::BasicBlock::kGoto:
      visitor->VisitGoto(block->SuccessorAt(0)->rpo_number());
      return;
    case compiler::BasicBlock::kCall: {
      compiler::BasicBlock* success = block->SuccessorAt(0);
      compiler::BasicBlock* exception = block->SuccessorAt(1);
      VisitCall(input, visitor, false, success->rpo_number(),
                exception->rpo_number());
    }
      return;
    case compiler::BasicBlock::kTailCall:
      VisitCall(input, visitor, true);
      return;
    case compiler::BasicBlock::kBranch: {
      DCHECK_EQ(IrOpcode::kBranch, input->opcode());
      compiler::BasicBlock* tbranch = block->SuccessorAt(0);
      compiler::BasicBlock* fbranch = block->SuccessorAt(1);
      if (tbranch == fbranch) {
        visitor->VisitGoto(tbranch->rpo_number());
      } else {
        visitor->VisitBranch(input->id(), input->InputAt(0)->id(),
                             tbranch->rpo_number(), fbranch->rpo_number());
      }
    }
      return;
    case compiler::BasicBlock::kSwitch: {
      OperandsVector successors;
      for (auto successor : block->successors()) {
        successors.emplace_back(successor->rpo_number());
      }
      visitor->VisitSwitch(input->id(), input->InputAt(0)->id(), successors);
    }
      return;
    case compiler::BasicBlock::kReturn: {
      OperandsVector return_operands;
      int pop_count = input->InputAt(0)->id();
      int return_operands_count = input->op()->ValueInputCount();
      for (int i = 1; i < return_operands_count; ++i)
        return_operands.emplace_back(input->InputAt(i)->id());
      visitor->VisitReturn(input->id(), pop_count, return_operands);
    }
      return;
    case compiler::BasicBlock::kDeoptimize:
      UNREACHABLE();
    case compiler::BasicBlock::kNone: {
      // Exit block doesn't have control.
      DCHECK_NULL(input);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

void ScheduleEmitter::VisitCall(compiler::Node* node, TFVisitor* visitor,
                                bool tail, int successor_bid,
                                int exception_bid) {
  EMASSERT(!tail || ((successor_bid == -1) && (exception_bid == -1)));
  const compiler::CallDescriptor* descriptor =
      compiler::CallDescriptorOf(node->op());
  if (!strcmp(descriptor->debug_name(), "c-call")) {
    VisitCCall(node, visitor);
    return;
  }
  bool code = false;
  switch (descriptor->kind()) {
    case compiler::CallDescriptor::kCallAddress:
      break;
    case compiler::CallDescriptor::kCallCodeObject:
      code = true;
      break;
    case compiler::CallDescriptor::kCallJSFunction:
      UNREACHABLE();
  }
  CHECK(descriptor->ReturnCount() <= 2);
  CallDescriptor call_desc;
  OperandsVector operands;
  bool met_stack = false;
  // push callee
  operands.push_back(node->InputAt(0)->id());
  for (int i = 1; i < descriptor->InputCount(); ++i) {
    compiler::LinkageLocation location = descriptor->GetInputLocation(i);
    if (location.IsRegister()) {
      CHECK(!met_stack);
      CHECK(!location.IsAnyRegister());
      call_desc.registers_for_operands.push_back(location.AsRegister());
    } else if (location.IsCallerFrameSlot()) {
      call_desc.registers_for_operands.push_back(-1);
    } else {
      UNREACHABLE();
    }
    operands.push_back(node->InputAt(i)->id());
  }
  call_desc.return_count = descriptor->ReturnCount();
  if (!tail) {
    if (successor_bid == -1)
      visitor->VisitCall(node->id(), code, call_desc, operands);
    else
      visitor->VisitInvoke(node->id(), code, call_desc, operands, successor_bid,
                           exception_bid);
  } else {
    visitor->VisitTailCall(node->id(), code, call_desc, operands);
  }
}

void ScheduleEmitter::VisitCCall(compiler::Node* node, TFVisitor* visitor) {
  int count = node->InputCount();
  OperandsVector operands;
  for (int i = 0; i < count; ++i) {
    operands.push_back(node->InputAt(i)->id());
  }
  visitor->VisitCallWithCallerSavedRegisters(node->id(), operands);
}

bool ScheduleEmitter::ShouldEmitCall(compiler::Node* node) {
  if (current_block_->SuccessorCount() != 1) return true;
  compiler::BasicBlock* next = current_block_->successors()[0];
  if (next->control_input() == node) return false;
  return true;
}

}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

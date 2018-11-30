#include "llvm-tf-builder.h"
#include "basic-block.h"
namespace jit {
LLVMTFBuilder::LLVMTFBuilder(Output& output,
                             BasicBlockManager& _basicBlockManager)
    : output_(&output),
      basicBlockManager_(&_basicBlockManager),
      currentBB_(nullptr) {}

void LLVMTFBuilder::end() {
  assert(!!currentBB_);
  currentBB_->end();
}

void LLVMTFBuilder::VisitBlock(int id, const OperandsVector& predecessors) {
  bb.startBuild(output());
  currentBB_ = bb;
}

void LLVMTFBuilder::VisitGoto(int bid) {
  BasicBlock* succ = basicBlockManager().ensureBB(bid);
  assert(!succ->started());
  output().buildBr(succ->nativeBB());
  currentBB_->end();
  currentBB_ = nullptr;
}

void LLVMTFBuilder::VisitParameter(int id, int pid) {
  LValue value = output.registerParameter(pid);
  currentBB_->setValue(id, value);
}

void LLVMTFBuilder::VisitLoadParentFramePointer(int id) {
  LValue value = output.fp();
  currentBB_->setValue(id, output.buildLoad(value));
}

void LLVMTFBuilder::VisitInt32Constant(int id, int32_t value) {
  currentBB_->setValue(id, output.constInt32(value));
}

static LType getMachineRepresentationType(Output& output,
                                          MachineRepresentation rep) {
  LType dstType;
  switch (rep) {
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kTaggedPointer:
      dstType = output.taggedType();
      break;
    case MachineRepresentation::kWord8:
      dstType = output.repo().int8;
      break;
    case MachineRepresentation::kWord16:
      dstType = output.repo().int16;
      break;
    case MachineRepresentation::kWord32:
      dstType = output.repo().int32;
      break;
    case MachineRepresentation::kWord64:
      dstType = output.repo().int64;
      break;
    case MachineRepresentation::kFloat32:
      dstType = output.repo().floatType;
      break;
    case MachineRepresentation::kFloat64:
      dstType = output.repo().doubleType;
      break;
    default:
      LLVM_BUILTIN_TRAP;
  }
  return dstType;
}

static LValue buildAccessPointer(Output& output, LValue value, int offset,
                                 MachineRepresentation rep) {
  LValue pointer = output.buildGEPWithByteOffset(
      value, offset, getMachineRepresentationType(output, rep));
  return pointer;
}

void LLVMTFBuilder::VisitLoad(int id, MachineRepresentation rep,
                              MachineSemantic semantic, int base, int offset) {
  LValue pointer =
      buildAccessPointer(output(), currentBB_->value(base), offset, rep);
  LValue value = output.buildLoad(pointer);
  LType castType = nullptr;
  LLVMOpcode opcode;
  switch (semantic) {
    case MachineSemantic::kUint32:
    case MachineSemantic::kInt32:
      switch (rep) {
        case MachineRepresentation::kWord8:
        case MachineRepresentation::kWord16:
          opcode =
              ((semantic == MachineSemantic::kInt32) ? LLVMSExt : LLVMZExt);
          castType = output().repo().int32;
          break;
        case MachineRepresentation::kWord32:
          break;
        default:
          LLVM_BUILTIN_TRAP;
      }
      break;
    case MachineRepresentation::kUint64:
    case MachineRepresentation::kInt64:
      switch (rep) {
        case MachineRepresentation::kWord8:
        case MachineRepresentation::kWord16:
        case MachineRepresentation::kWord32:
          opcode =
              ((semantic == MachineSemantic::kInt64) ? LLVMSExt : LLVMZExt);
          castType = output().repo().int64;
          break;
        case MachineRepresentation::kWord64:
          break;
        default:
          LLVM_BUILTIN_TRAP;
      }
  }
  if (castType) value = output().buildCast(opcode, value, castType);
  currentBB_->setValue(id, value);
}

void LLVMTFBuilder::VisitStore(int id, MachineRepresentation rep,
                               WriteBarrierKind barrier, int base, int offset,
                               int value) {
  LValue pointer =
      buildAccessPointer(output(), currentBB_->value(base), offset, rep);
  // FIXME: emit write barrier accordingly.
  assert(barrier == kNoWriteBarrier);
  LValue val = buildStore(currentBB_->value(value), pointer);
  // store should not be recorded, whatever.
  currentBB_->setValue(id, val);
}

void LLVMTFBuilder::VisitBitcastWordToTagged(int id, int e) {
  currentBB_->setValue(
      id, output().buildBitcast(currentBB_->value(e), output().taggedType()));
}

void LLVMTFBuilder::VisitInt32Add(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildNSWAdd(e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitInt32Sub(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildNSWSub(e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitInt32Mul(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildNSWMul(e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitWord32Shl(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildShl(e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitWord32Shr(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildShr(e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitWord32Sar(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildSar(e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitWord32Mul(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildMul(e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitWord32And(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildAnd(e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitWord32Equal(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildICmp(LLVMIntEQ, e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitInt32LessThanOrEqual(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildICmp(LLVMIntSLE, e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitUint32LessThanOrEqual(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildICmp(LLVMIntULE, e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitInt32LessThan(int id, int e1, int e2) {
  LValue e1_value = currentBB_->value(e1);
  LValue e2_value = currentBB_->value(e2);
  LValue result = output().buildICmp(LLVMIntSLT, e1_value, e2_value);
  currentBB_->setValue(id, result);
}

void LLVMTFBuilder::VisitBranch(int id, int cmp, int btrue, int bfalse) {
  LValue cmp_value = currentBB_->value(cmp);
  BasicBlock* bbTrue = basicBlockManager().ensureBB(btrue);
  BasicBlock* bbFalse = basicBlockManager().ensureBB(bfalse);
  output().buildCondBr(cmp, bbTrue->nativeBB(), bbFalse->nativeBB());
  currentBB_->end();
  currentBB_ = nullptr;
}

void LLVMTFBuilder::VisitHeapConstant(int id, int64_t magic) {
  char buf[256];
  int len = snprintf(buf, 256, "mov $0, #%lld", magic);
  char kConstraint[] = "=r";
  LValue value =
      output().buildInlineAsm(functionType(output().taggedType()), buf, len,
                              kConstraint, sizeof(kConstraint) - 1, false);
  currentBB_->setValue(id, value);
}

void LLVMTFBuilder::VisitExternalConstant(int id, int64_t magic) {
  char buf[256];
  int len = snprintf(buf, 256, "mov $0, #%lld", magic);
  char kConstraint[] = "=r";
  LValue value =
      output().buildInlineAsm(functionType(output().taggedType()), buf, len,
                              kConstraint, sizeof(kConstraint) - 1, false);
  currentBB_->setValue(id, value);
}

void LLVMTFBuilder::VisitPhi(int id, MachineRepresentation rep,
                             const OperandsVector& operands) {
  LValue phi = output().buildPhi(getMachineRepresentationType(output(), rep));
  auto operands_iterator = operands.cbegin();
  for (BasicBlock* pred : currentBB_->predecessor()) {
    LValue value = currentBB_->getValue(*operands_iterator);
    LBasicBlock llvbb_ = pred->nativeBB();
    addIncoming(phi, &value, &llvbb_, 1);
    ++operands_iterator;
  }
  currentBB_->setValue(id, phi);
}

void LLVMTFBuilder::VisitCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  DoCommonCall(id, code, registers_for_operands, operands);
}

void LLVMTFBuilder::VisitTailCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  cout << id << ":"
       << "VisitTailCall"
       << " is_code:" << code << operands << endl;
}
}  // namespace jit

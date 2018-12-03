#include "llvm-tf-builder.h"
#include <llvm/Support/Compiler.h>
#include <sstream>
#include "basic-block-manager.h"
#include "basic-block.h"

namespace jit {
namespace {
struct LLVMTFBuilderBasicBlockImpl {
  std::vector<BasicBlock*> not_merged_pred;
  std::vector<LValue> phis;
};

void EnsureNativeBB(BasicBlock* bb, Output& output) {
  if (bb->native_bb()) return;
  char buf[256];
  snprintf(buf, 256, "B%d\n", bb->id());
  LBasicBlock native_bb = output.appendBasicBlock(buf);
  bb->AssignNativeBB(native_bb);
}

void StartBuild(BasicBlock* bb, Output& output) {
  EnsureNativeBB(bb, output);
  bb->StartBuild();
  output.positionToBBEnd(bb->native_bb());
}
}  // namespace

LLVMTFBuilder::LLVMTFBuilder(Output& output,
                             BasicBlockManager& basic_block_manager)
    : output_(&output),
      basic_block_manager_(&basic_block_manager),
      current_bb_(nullptr),
      state_point_id_next_(0) {}

void LLVMTFBuilder::End() {
  assert(!!current_bb_);
  current_bb_->EndBuild();
  current_bb_ = nullptr;
  ProcessPhiWorkList();
  jit::ResetImpls<LLVMTFBuilderBasicBlockImpl>(basic_block_manager());
}

void LLVMTFBuilder::MergePredecessors(BasicBlock* bb) {
  if (bb->predecessors().empty()) return;
  if (bb->predecessors().size() == 1) {
    // Don't use phi if only one predecessor.
    BasicBlock* pred = bb->predecessors()[0];
    assert(pred->started());
    for (int live : bb->liveins()) {
      LValue value = pred->value(live);
      bb->set_value(live, value);
    }
    return;
  }
  BasicBlock* ref_pred = nullptr;
  if (!AllPredecessorStarted(bb, &ref_pred)) {
    assert(!!ref_pred);
    std::unique_ptr<LLVMTFBuilderBasicBlockImpl> impl(
        new LLVMTFBuilderBasicBlockImpl);
    bb->SetImpl(impl.release());
    BuildPhiAndPushToWorkList(bb, ref_pred);
  }
  // Use phi.
  for (int live : bb->liveins()) {
    LValue ref_value = ref_pred->value(live);
    LType ref_type = typeOf(ref_value);
    // FIXME: Should ignore those are not tagged type.
    if (ref_type != output().taggedType()) {
    }
    LValue phi = output().buildPhi(ref_type);
    for (BasicBlock* pred : bb->predecessors()) {
      LValue value = pred->value(live);
      LBasicBlock native = pred->native_bb();
      addIncoming(phi, &value, &native, 1);
    }
  }
}

bool LLVMTFBuilder::AllPredecessorStarted(BasicBlock* bb,
                                          BasicBlock** ref_pred) {
  bool ret_value = true;
  for (BasicBlock* pred : bb->predecessors()) {
    if (pred->started()) {
      if (!*ref_pred) *ref_pred = pred;
    } else {
      ret_value = false;
    }
  }
  return ret_value;
}

void LLVMTFBuilder::BuildPhiAndPushToWorkList(BasicBlock* bb,
                                              BasicBlock* ref_pred) {
  auto impl = bb->GetImpl<LLVMTFBuilderBasicBlockImpl>();
  for (int live : bb->liveins()) {
    LValue ref_value = ref_pred->value(live);
    LType ref_type = typeOf(ref_value);
    // FIXME: Should ignore those are not tagged type.
    if (ref_type != output().taggedType()) {
    }
    LValue phi = output().buildPhi(ref_type);
    bb->set_value(live, phi);
    impl->phis.push_back(phi);
    for (BasicBlock* pred : bb->predecessors()) {
      if (!pred->started()) {
        impl->not_merged_pred.push_back(pred);
        continue;
      }

      LValue value = pred->value(live);
      LBasicBlock native = pred->native_bb();
      addIncoming(phi, &value, &native, 1);
    }
  }
  phi_rebuild_worklist_.push_back(bb);
}

void LLVMTFBuilder::ProcessPhiWorkList() {
  for (BasicBlock* bb : phi_rebuild_worklist_) {
    auto impl = bb->GetImpl<LLVMTFBuilderBasicBlockImpl>();
    auto phi_iterator = impl->phis.begin();
    for (int live : bb->liveins()) {
      for (auto pred : impl->not_merged_pred) {
        assert(pred->started());
        LValue value = pred->value(live);
        LBasicBlock native = pred->native_bb();
        addIncoming(*phi_iterator, &value, &native, 1);
      }
      ++phi_iterator;
    }
  }
  phi_rebuild_worklist_.clear();
}

void LLVMTFBuilder::DoCommonCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands, bool tailcall) {
  // 1. generate call asm
  std::ostringstream constraints;
  if (!tailcall) constraints << "={r0},";
  for (auto& rname : registers_for_operands) {
    constraints << "{" << rname << "},";
  }
  auto operands_iterator = operands.begin();
  LValue ret;
  int operand_index_start = (tailcall ? 0 : 1);
  LValue target;
  if (code) {
    // layout
    // return value | register operands | stack operands | artifact operands
    int code_value = *(operands_iterator++);
    target = output().buildGEPWithByteOffset(current_bb_->value(code_value),
                                             63 /* Code::kHeaderSize */,
                                             output().repo().ref8);
  } else {
    int addr_value = *(operands_iterator++);
    target = current_bb_->value(addr_value);
  }

  std::ostringstream instructions;
  int stack_operands_index =
      operand_index_start + registers_for_operands.size();
  int artifact_operands_index_start = operand_index_start + operands.size();
  int artifact_target_operand_index = artifact_operands_index_start + 0;
  int artifact_fp_operand_index = artifact_operands_index_start + 3;
  size_t stack_parameter_size =
      operands.size() - 1 - registers_for_operands.size();
  for (size_t i = 0; i < stack_parameter_size; ++i) {
    constraints << "r";
    instructions << "push {$" << stack_operands_index++ << "}\n";
  }
  // artifact operands
  if (tailcall) {
    instructions << "add sp, $" << artifact_fp_operand_index << ", #8\n"
                 << "pop {r11, lr}\n";
  }
  instructions << (tailcall ? "bx" : "blx") << " $"
               << artifact_target_operand_index << "\n";
  constraints << ",r, {r7}, {r10}, {r11}";
  std::string instruction_string = instructions.str();
  std::string constraints_string = constraints.str();
  LValue func = LLVMGetInlineAsm(
      output().taggedType(), const_cast<char*>(instruction_string.data()),
      instruction_string.size(), const_cast<char*>(constraints_string.data()),
      constraints_string.size(), true, false, LLVMInlineAsmDialectATT);
  std::vector<LValue> operand_values;
  for (; operands_iterator != operands.end(); ++operands_iterator) {
    operand_values.push_back(current_bb_->value(*operands_iterator));
  }
  // push artifact operands' value
  operand_values.push_back(target);
  operand_values.push_back(output().context());
  operand_values.push_back(output().root());
  operand_values.push_back(output().fp());
  int gc_paramter_start = 0;
  if (tailcall) {
    ret =
        output().buildCall(func, operand_values.data(), operand_values.size());
  } else {
    std::vector<LValue> statepoint_operands;
    statepoint_operands.push_back(output().constInt64(state_point_id_next_++));
    statepoint_operands.push_back(func);
    statepoint_operands.push_back(output().constInt64(operand_values.size()));
    statepoint_operands.push_back(output().constInt64(0));  // flags
    for (auto value : operand_values) statepoint_operands.push_back(value);
    statepoint_operands.push_back(output().constInt64(0));  // # transition args
    statepoint_operands.push_back(output().constInt64(0));  // # deopt arguments
    gc_paramter_start = statepoint_operands.size();
    // push current defines
    for (auto& items : current_bb_->values()) {
      LValue to_gc = items.second;
      statepoint_operands.push_back(to_gc);
    }
    ret = output().buildCall(output().repo().statepointIntrinsic(),
                             statepoint_operands.data(),
                             statepoint_operands.size());
  }
  // 2. rebuild value if not tailcall
  if (!tailcall) {
    LValue real_ret =
        output().buildCall(output().repo().gcResultIntrinsic(), ret);
    for (auto& items : current_bb_->values()) {
      LValue relocated =
          output().buildCall(output().repo().gcRelocateIntrinsic(),
                             output().constInt32(gc_paramter_start),
                             output().constInt32(gc_paramter_start));
      items.second = relocated;
      gc_paramter_start++;
    }
    current_bb_->set_value(id, real_ret);
  }
}

void LLVMTFBuilder::VisitBlock(int id, const OperandsVector& predecessors) {
  BasicBlock* bb = basic_block_manager().findBB(id);
  current_bb_ = bb;
  StartBuild(bb, output());
  MergePredecessors(bb);
}

void LLVMTFBuilder::VisitGoto(int bid) {
  BasicBlock* succ = basic_block_manager().ensureBB(bid);
  EnsureNativeBB(succ, output());
  assert(!succ->started());
  output().buildBr(succ->native_bb());
  current_bb_->EndBuild();
  current_bb_ = nullptr;
}

void LLVMTFBuilder::VisitParameter(int id, int pid) {
  LValue value = output().registerParameter(pid);
  current_bb_->set_value(id, value);
}

void LLVMTFBuilder::VisitLoadParentFramePointer(int id) {
  LValue value = output().fp();
  current_bb_->set_value(id, output().buildLoad(value));
}

void LLVMTFBuilder::VisitInt32Constant(int id, int32_t value) {
  current_bb_->set_value(id, output().constInt32(value));
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
      buildAccessPointer(output(), current_bb_->value(base), offset, rep);
  LValue value = output().buildLoad(pointer);
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
    case MachineSemantic::kUint64:
    case MachineSemantic::kInt64:
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
    default:
      LLVM_BUILTIN_TRAP;
  }
  if (castType) value = output().buildCast(opcode, value, castType);
  current_bb_->set_value(id, value);
}

void LLVMTFBuilder::VisitStore(int id, MachineRepresentation rep,
                               WriteBarrierKind barrier, int base, int offset,
                               int value) {
  LValue pointer =
      buildAccessPointer(output(), current_bb_->value(base), offset, rep);
  // FIXME: emit write barrier accordingly.
  assert(barrier == kNoWriteBarrier);
  LValue val = output().buildStore(current_bb_->value(value), pointer);
  // store should not be recorded, whatever.
  current_bb_->set_value(id, val);
}

void LLVMTFBuilder::VisitBitcastWordToTagged(int id, int e) {
  current_bb_->set_value(
      id, output().buildBitCast(current_bb_->value(e), output().taggedType()));
}

void LLVMTFBuilder::VisitInt32Add(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildNSWAdd(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32Sub(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildNSWSub(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32Mul(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildNSWMul(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Shl(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildShl(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Shr(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildShr(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Sar(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildSar(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Mul(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildMul(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32And(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildAnd(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Equal(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildICmp(LLVMIntEQ, e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32LessThanOrEqual(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildICmp(LLVMIntSLE, e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitUint32LessThanOrEqual(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildICmp(LLVMIntULE, e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32LessThan(int id, int e1, int e2) {
  LValue e1_value = current_bb_->value(e1);
  LValue e2_value = current_bb_->value(e2);
  LValue result = output().buildICmp(LLVMIntSLT, e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitBranch(int id, int cmp, int btrue, int bfalse) {
  BasicBlock* bbTrue = basic_block_manager().ensureBB(btrue);
  BasicBlock* bbFalse = basic_block_manager().ensureBB(bfalse);
  EnsureNativeBB(bbTrue, output());
  EnsureNativeBB(bbFalse, output());
  output().buildCondBr(current_bb_->value(cmp), bbTrue->native_bb(),
                       bbFalse->native_bb());
  current_bb_->EndBuild();
  current_bb_ = nullptr;
}

void LLVMTFBuilder::VisitHeapConstant(int id, int64_t magic) {
  char buf[256];
  int len = snprintf(buf, 256, "mov $0, #%lld", static_cast<long long>(magic));
  char kConstraint[] = "=r";
  LValue value =
      output().buildInlineAsm(functionType(output().taggedType()), buf, len,
                              kConstraint, sizeof(kConstraint) - 1, false);
  current_bb_->set_value(id, value);
}

void LLVMTFBuilder::VisitExternalConstant(int id, int64_t magic) {
  char buf[256];
  int len = snprintf(buf, 256, "mov $0, #%lld", static_cast<long long>(magic));
  char kConstraint[] = "=r";
  LValue value =
      output().buildInlineAsm(functionType(output().taggedType()), buf, len,
                              kConstraint, sizeof(kConstraint) - 1, false);
  current_bb_->set_value(id, value);
}

void LLVMTFBuilder::VisitPhi(int id, MachineRepresentation rep,
                             const OperandsVector& operands) {
  LValue phi = output().buildPhi(getMachineRepresentationType(output(), rep));
  auto operands_iterator = operands.cbegin();
  for (BasicBlock* pred : current_bb_->predecessors()) {
    assert(pred->started());
    LValue value = pred->value(*operands_iterator);
    LBasicBlock llvbb_ = pred->native_bb();
    addIncoming(phi, &value, &llvbb_, 1);
    ++operands_iterator;
  }
  current_bb_->set_value(id, phi);
}

void LLVMTFBuilder::VisitCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  DoCommonCall(id, code, registers_for_operands, operands, false);
}

void LLVMTFBuilder::VisitTailCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  DoCommonCall(id, code, registers_for_operands, operands, true);
  output().buildUnreachable();
}
}  // namespace jit

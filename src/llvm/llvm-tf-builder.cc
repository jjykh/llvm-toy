#include "src/llvm/llvm-tf-builder.h"
#include <llvm/Support/Compiler.h>
#include <bitset>
#include <sstream>
#include "src/llvm/basic-block-manager.h"
#include "src/llvm/basic-block.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
namespace {
struct NotMergedPhiDesc {
  BasicBlock* pred;
  int value;
  LValue phi;
};

struct LLVMTFBuilderBasicBlockImpl {
  std::vector<NotMergedPhiDesc> not_merged_phis;
};

void EnsureNativeBB(BasicBlock* bb, Output& output) {
  if (bb->native_bb()) return;
  char buf[256];
  snprintf(buf, 256, "B%d", bb->id());
  LBasicBlock native_bb = output.appendBasicBlock(buf);
  bb->AssignNativeBB(native_bb);
}

void StartBuild(BasicBlock* bb, Output& output) {
  EnsureNativeBB(bb, output);
  bb->StartBuild();
  output.positionToBBEnd(bb->native_bb());
}

std::string ConstraintsToString(const std::vector<std::string>& constraints) {
  std::ostringstream oss;
  auto iterator = constraints.begin();
  if (iterator == constraints.end()) return oss.str();
  oss << *(iterator++);
  if (iterator == constraints.end()) return oss.str();
  for (; iterator != constraints.end(); ++iterator) {
    oss << "," << *(iterator);
  }
  return oss.str();
}

LLVMTFBuilderBasicBlockImpl* EnsureImpl(BasicBlock* bb) {
  if (bb->GetImpl<LLVMTFBuilderBasicBlockImpl>())
    return bb->GetImpl<LLVMTFBuilderBasicBlockImpl>();
  std::unique_ptr<LLVMTFBuilderBasicBlockImpl> impl(
      new LLVMTFBuilderBasicBlockImpl);
  bb->SetImpl(impl.release());
  return bb->GetImpl<LLVMTFBuilderBasicBlockImpl>();
}

class CallOperandResolver {
 public:
  explicit CallOperandResolver(BasicBlock* current_bb, Output& output,
                               LValue target);
  ~CallOperandResolver() = default;
  void Resolve(OperandsVector::const_iterator operands_iterator,
               OperandsVector::const_iterator end,
               const RegistersForOperands& registers_for_operands);
  inline std::vector<LValue>& operand_values() { return operand_values_; }
  inline std::vector<LType>& operand_value_types() {
    return operand_value_types_;
  }

 private:
  static const int kV8CCRegisterParameterCount = 12;
  static const int kContextReg = 7;
  static const int kRootReg = 10;
  static const int kFPReg = 11;
  void SetOperandValue(int reg, LValue value);
  int FindNextReg();
  inline Output& output() { return *output_; }
  std::bitset<kV8CCRegisterParameterCount>
      allocatable_register_set_; /* 0 is allocatable */
  std::vector<LValue> operand_values_;
  std::vector<LType> operand_value_types_;
  BasicBlock* current_bb_;
  Output* output_;
  LValue target_;
  int next_reg_;
};

CallOperandResolver::CallOperandResolver(BasicBlock* current_bb, Output& output,
                                         LValue target)
    : operand_values_(kV8CCRegisterParameterCount,
                      LLVMGetUndef(output.repo().intPtr)),
      operand_value_types_(kV8CCRegisterParameterCount, output.repo().intPtr),
      current_bb_(current_bb),
      output_(&output),
      target_(target),
      next_reg_(0) {}

int CallOperandResolver::FindNextReg() {
  if (next_reg_ < 0) return -1;
  for (int i = next_reg_; i < kV8CCRegisterParameterCount; ++i) {
    if (!allocatable_register_set_.test(i)) {
      next_reg_ = i + 1;
      return i;
    }
  }
  next_reg_ = -1;
  return -1;
}

void CallOperandResolver::SetOperandValue(int reg, LValue llvm_val) {
  LType llvm_val_type = typeOf(llvm_val);
  if (reg >= 0) {
    operand_values_[reg] = llvm_val;
    operand_value_types_[reg] = llvm_val_type;
    allocatable_register_set_.set(reg);
  } else {
    operand_values_.push_back(llvm_val);
    operand_value_types_.push_back(llvm_val_type);
  }
}
void CallOperandResolver::Resolve(
    OperandsVector::const_iterator operands_iterator,
    OperandsVector::const_iterator end,
    const RegistersForOperands& registers_for_operands) {
  // setup register operands
  for (int reg : registers_for_operands) {
    assert(reg < kV8CCRegisterParameterCount);
    LValue llvm_val = current_bb_->value(*operands_iterator);
    SetOperandValue(reg, llvm_val);
    ++operands_iterator;
  }
  // setup artifact operands' value
  SetOperandValue(kContextReg, output().context());
  SetOperandValue(kRootReg, output().root());
  SetOperandValue(kFPReg, output().fp());

  SetOperandValue(FindNextReg(), target_);

  for (; operands_iterator != end; ++operands_iterator) {
    LValue llvm_val = current_bb_->value(*operands_iterator);
    SetOperandValue(FindNextReg(), llvm_val);
  }
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
  EndCurrentBlock();
  ProcessPhiWorkList();
  v8::internal::tf_llvm::ResetImpls<LLVMTFBuilderBasicBlockImpl>(
      basic_block_manager());
  output().positionToBBEnd(output().prologue());
  output().buildBr(basic_block_manager()
                       .findBB(*basic_block_manager().rpo().begin())
                       ->native_bb());
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
    BuildPhiAndPushToWorkList(bb, ref_pred);
    return;
  }
  // Use phi.
  for (int live : bb->liveins()) {
    LValue ref_value = ref_pred->value(live);
    LType ref_type = typeOf(ref_value);
    if (ref_type != output().taggedType()) {
      // FIXME: Should add assert that all values are the same.
      bb->set_value(live, ref_value);
      continue;
    }
    LValue phi = output().buildPhi(ref_type);
    for (BasicBlock* pred : bb->predecessors()) {
      LValue value = pred->value(live);
      LBasicBlock native = pred->native_bb();
      addIncoming(phi, &value, &native, 1);
    }
    bb->set_value(live, phi);
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
  auto impl = EnsureImpl(bb);
  for (int live : bb->liveins()) {
    LValue ref_value = ref_pred->value(live);
    LType ref_type = typeOf(ref_value);
    if (ref_type != output().taggedType()) {
      bb->set_value(live, ref_value);
      continue;
    }
    LValue phi = output().buildPhi(ref_type);
    bb->set_value(live, phi);
    for (BasicBlock* pred : bb->predecessors()) {
      if (!pred->started()) {
        impl->not_merged_phis.emplace_back();
        NotMergedPhiDesc& not_merged_phi = impl->not_merged_phis.back();
        not_merged_phi.phi = phi;
        not_merged_phi.value = live;
        not_merged_phi.pred = pred;
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
    for (auto& e : impl->not_merged_phis) {
      BasicBlock* pred = e.pred;
      assert(pred->started());
      LValue value = pred->value(e.value);
      LBasicBlock native = pred->native_bb();
      addIncoming(e.phi, &value, &native, 1);
    }
    impl->not_merged_phis.clear();
  }
  phi_rebuild_worklist_.clear();
}

void LLVMTFBuilder::DoTailCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  // 1. generate call asm
  std::vector<std::string> constraints;
  constraints.push_back("={r0}");
  for (auto& rname : registers_for_operands) {
    std::string constraint;
    constraint.append("{r");
    constraint.append(1, static_cast<char>('0' + rname));
    constraint.append("}");
    constraints.push_back(constraint);
  }
  auto operands_iterator = operands.begin();
  LValue ret;
  int operand_index_start = 1;
  LValue target;
  if (code) {
    // layout
    // return value | register operands | stack operands | artifact operands
    int code_value = *(operands_iterator++);
    target = output().buildGEPWithByteOffset(
        current_bb_->value(code_value),
        output().constInt32(63) /* Code::kHeaderSize */, output().repo().ref8);
  } else {
    int addr_value = *(operands_iterator++);
    target = current_bb_->value(addr_value);
  }

  std::ostringstream instructions;
  static const size_t kTargetSize = 1;
  int stack_operands_index =
      operand_index_start + registers_for_operands.size();
  int artifact_operands_index_start =
      operand_index_start + operands.size() - kTargetSize;
  int artifact_target_operand_index = artifact_operands_index_start + 0;
  int artifact_fp_operand_index = artifact_operands_index_start + 3;
  size_t stack_parameter_size =
      operands.size() - kTargetSize - registers_for_operands.size();
  for (size_t i = 0; i < stack_parameter_size; ++i) {
    constraints.push_back("r");
    instructions << "push {$" << stack_operands_index++ << "}\n";
  }
  instructions << "add sp, $" << artifact_fp_operand_index << ", #8\n"
               << "pop {r11, lr}\n";
  instructions << "bx"
               << " $" << artifact_target_operand_index << "\n";
  constraints.push_back("r");
  constraints.push_back("{r7}");
  constraints.push_back("{r10}");
  constraints.push_back("{r11}");
  std::string instruction_string = instructions.str();
  std::string constraints_string = ConstraintsToString(constraints);

  std::vector<LValue> operand_values;
  std::vector<LType> operand_value_types;
  for (; operands_iterator != operands.end(); ++operands_iterator) {
    LValue llvm_val = current_bb_->value(*operands_iterator);
    operand_values.push_back(llvm_val);
    operand_value_types.push_back(typeOf(llvm_val));
  }
  // push artifact operands' value
  operand_values.push_back(target);
  operand_value_types.push_back(typeOf(target));
  operand_values.push_back(output().context());
  operand_value_types.push_back(typeOf(output().context()));
  operand_values.push_back(output().root());
  operand_value_types.push_back(typeOf(output().root()));
  operand_values.push_back(output().fp());
  operand_value_types.push_back(typeOf(output().fp()));

  LValue func = LLVMGetInlineAsm(
      functionType(output().taggedType(), operand_value_types.data(),
                   operand_value_types.size(), NotVariadic),
      const_cast<char*>(instruction_string.data()), instruction_string.size(),
      const_cast<char*>(constraints_string.data()), constraints_string.size(),
      true, false, LLVMInlineAsmDialectATT);
  ret = output().buildCall(func, operand_values.data(), operand_values.size());
  current_bb_->set_value(id, ret);
  output().buildUnreachable();
}

void LLVMTFBuilder::DoCall(int id, bool code,
                           const RegistersForOperands& registers_for_operands,
                           const OperandsVector& operands) {
  auto operands_iterator = operands.begin();
  LValue ret;
  LValue target;
  if (code) {
    // layout
    // return value | register operands | stack operands | artifact operands
    int code_value = *(operands_iterator++);
    target = output().buildGEPWithByteOffset(
        current_bb_->value(code_value),
        output().constInt32(63) /* Code::kHeaderSize */, output().repo().ref8);
  } else {
    int addr_value = *(operands_iterator++);
    target = current_bb_->value(addr_value);
  }
  CallOperandResolver call_operand_resolver(current_bb_, output(), target);
  call_operand_resolver.Resolve(operands_iterator, operands.end(),
                                registers_for_operands);
  std::vector<LValue> statepoint_operands;
  LType callee_function_type = functionType(
      output().repo().taggedType,
      call_operand_resolver.operand_value_types().data(),
      call_operand_resolver.operand_value_types().size(), NotVariadic);
  LType callee_type = pointerType(callee_function_type);
  statepoint_operands.push_back(output().constInt64(state_point_id_next_++));
  statepoint_operands.push_back(output().constInt32(4));
  statepoint_operands.push_back(constNull(callee_type));
  statepoint_operands.push_back(output().constInt32(
      call_operand_resolver.operand_values().size()));    // # call params
  statepoint_operands.push_back(output().constInt32(0));  // flags
  for (LValue operand_value : call_operand_resolver.operand_values())
    statepoint_operands.push_back(operand_value);
  statepoint_operands.push_back(output().constInt32(0));  // # transition args
  statepoint_operands.push_back(output().constInt32(0));  // # deopt arguments
  int gc_paramter_start = statepoint_operands.size();
  // push current defines
  for (auto& items : current_bb_->values()) {
    LValue to_gc = items.second;
    if (typeOf(to_gc) != output().taggedType()) continue;
    statepoint_operands.push_back(to_gc);
  }
  LValue statepoint_ret = output().buildCall(
      output().getStatePointFunction(callee_type), statepoint_operands.data(),
      statepoint_operands.size());
  LLVMSetInstructionCallConv(statepoint_ret, LLVMV8CallConv);
  // 2. rebuild value if not tailcall
  for (auto& items : current_bb_->values()) {
    if (typeOf(items.second) != output().taggedType()) continue;
    LValue relocated = output().buildCall(
        output().repo().gcRelocateIntrinsic(), statepoint_ret,
        output().constInt32(gc_paramter_start),
        output().constInt32(gc_paramter_start));
    items.second = relocated;
    gc_paramter_start++;
  }
  ret = output().buildCall(output().repo().gcResultIntrinsic(), statepoint_ret);
  current_bb_->set_value(id, ret);
}

LValue LLVMTFBuilder::EnsureWord32(LValue v) {
  LType type = typeOf(v);
  LLVMTypeKind kind = LLVMGetTypeKind(type);
  if (kind == LLVMPointerTypeKind) {
    return output().buildCast(LLVMPtrToInt, v, output().repo().int32);
  }
  if (type == output().repo().int1) {
    return output().buildCast(LLVMZExt, v, output().repo().int32);
  }
  assert(type == output().repo().int32);
  return v;
}

void LLVMTFBuilder::EndCurrentBlock() {
  if (current_bb_->values().empty()) output().buildUnreachable();
  current_bb_->EndBuild();
  current_bb_ = nullptr;
}

void LLVMTFBuilder::VisitBlock(int id, bool,
                               const OperandsVector& predecessors) {
  BasicBlock* bb = basic_block_manager().findBB(id);
  current_bb_ = bb;
  StartBuild(bb, output());
  MergePredecessors(bb);
}

void LLVMTFBuilder::VisitGoto(int bid) {
  BasicBlock* succ = basic_block_manager().ensureBB(bid);
  EnsureNativeBB(succ, output());
  output().buildBr(succ->native_bb());
  EndCurrentBlock();
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

static LValue buildAccessPointer(Output& output, LValue value, LValue offset,
                                 MachineRepresentation rep) {
  LLVMTypeKind kind = LLVMGetTypeKind(typeOf(value));
  if (kind == LLVMIntegerTypeKind) {
    value = output.buildCast(LLVMIntToPtr, value, output.repo().ref8);
  }
  LValue pointer = output.buildGEPWithByteOffset(
      value, offset, pointerType(getMachineRepresentationType(output, rep)));
  return pointer;
}

void LLVMTFBuilder::VisitLoad(int id, MachineRepresentation rep,
                              MachineSemantic semantic, int base, int offset) {
  LValue pointer = buildAccessPointer(output(), current_bb_->value(base),
                                      current_bb_->value(offset), rep);
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
      break;
  }
  if (castType) value = output().buildCast(opcode, value, castType);
  current_bb_->set_value(id, value);
}

void LLVMTFBuilder::VisitStore(int id, MachineRepresentation rep,
                               WriteBarrierKind barrier, int base, int offset,
                               int value) {
  LValue pointer = buildAccessPointer(output(), current_bb_->value(base),
                                      current_bb_->value(offset), rep);
  // FIXME: emit write barrier accordingly.
  assert(barrier == kNoWriteBarrier);
  LValue llvm_val = current_bb_->value(value);
  LType value_type = typeOf(llvm_val);
  LType pointer_element_type = getElementType(typeOf(pointer));
  if (pointer_element_type != value_type) {
    assert(value_type = output().repo().intPtr);
    llvm_val = output().buildCast(LLVMIntToPtr, llvm_val, pointer_element_type);
  }
  LValue val = output().buildStore(llvm_val, pointer);
  // store should not be recorded, whatever.
  current_bb_->set_value(id, val);
}

void LLVMTFBuilder::VisitBitcastWordToTagged(int id, int e) {
  current_bb_->set_value(id,
                         output().buildCast(LLVMIntToPtr, current_bb_->value(e),
                                            output().taggedType()));
}

void LLVMTFBuilder::VisitInt32Add(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildNSWAdd(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32Sub(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildNSWSub(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32Mul(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildNSWMul(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Shl(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildShl(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Shr(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildShr(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Sar(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildSar(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Mul(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildMul(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32And(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildAnd(e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Equal(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildICmp(LLVMIntEQ, e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32LessThanOrEqual(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildICmp(LLVMIntSLE, e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitUint32LessThanOrEqual(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildICmp(LLVMIntULE, e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32LessThan(int id, int e1, int e2) {
  LValue e1_value = EnsureWord32(current_bb_->value(e1));
  LValue e2_value = EnsureWord32(current_bb_->value(e2));
  LValue result = output().buildICmp(LLVMIntSLT, e1_value, e2_value);
  current_bb_->set_value(id, result);
}

void LLVMTFBuilder::VisitBranch(int id, int cmp, int btrue, int bfalse) {
  BasicBlock* bbTrue = basic_block_manager().ensureBB(btrue);
  BasicBlock* bbFalse = basic_block_manager().ensureBB(bfalse);
  EnsureNativeBB(bbTrue, output());
  EnsureNativeBB(bbFalse, output());
  int expected_value = -1;
  if (bbTrue->is_deferred()) {
    if (!bbFalse->is_deferred()) expected_value = 0;
  } else if (bbFalse->is_deferred()) {
    expected_value = 1;
  }
  LValue cmp_val = current_bb_->value(cmp);
  if (expected_value != -1) {
    cmp_val = output().buildCall(output().repo().expectIntrinsic(), cmp_val,
                                 output().constInt1(expected_value));
  }
  output().buildCondBr(cmp_val, bbTrue->native_bb(), bbFalse->native_bb());
  EndCurrentBlock();
}

void LLVMTFBuilder::VisitHeapConstant(int id, int64_t magic) {
  char buf[256];
  int len = snprintf(buf, 256, "mov $0, #%lld", static_cast<long long>(magic));
  char kConstraint[] = "=r";
  // FIXME: review the sideeffect.
  LValue value =
      output().buildInlineAsm(functionType(output().taggedType()), buf, len,
                              kConstraint, sizeof(kConstraint) - 1, false);
  current_bb_->set_value(id, value);
}

void LLVMTFBuilder::VisitExternalConstant(int id, int64_t magic) {
  char buf[256];
  int len = snprintf(buf, 256, "mov $0, #%lld", static_cast<long long>(magic));
  char kConstraint[] = "=r";
  // FIXME: review the sideeffect.
  LValue value =
      output().buildInlineAsm(functionType(output().taggedType()), buf, len,
                              kConstraint, sizeof(kConstraint) - 1, false);
  current_bb_->set_value(id, value);
}

void LLVMTFBuilder::VisitPhi(int id, MachineRepresentation rep,
                             const OperandsVector& operands) {
  LValue phi = output().buildPhi(getMachineRepresentationType(output(), rep));
  auto operands_iterator = operands.cbegin();
  bool should_add_to_tf_phi_worklist = false;
  for (BasicBlock* pred : current_bb_->predecessors()) {
    if (pred->started()) {
      LValue value = pred->value(*operands_iterator);
      LBasicBlock llvbb_ = pred->native_bb();
      addIncoming(phi, &value, &llvbb_, 1);
    } else {
      should_add_to_tf_phi_worklist = true;
      LLVMTFBuilderBasicBlockImpl* impl = EnsureImpl(current_bb_);
      impl->not_merged_phis.emplace_back();
      auto& not_merged_phi = impl->not_merged_phis.back();
      not_merged_phi.phi = phi;
      not_merged_phi.pred = pred;
      not_merged_phi.value = *operands_iterator;
    }
    ++operands_iterator;
  }
  if (should_add_to_tf_phi_worklist)
    phi_rebuild_worklist_.push_back(current_bb_);
  current_bb_->set_value(id, phi);
}

void LLVMTFBuilder::VisitCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  DoCall(id, code, registers_for_operands, operands);
}

void LLVMTFBuilder::VisitTailCall(
    int id, bool code, const RegistersForOperands& registers_for_operands,
    const OperandsVector& operands) {
  DoTailCall(id, code, registers_for_operands, operands);
}

void LLVMTFBuilder::VisitRoot(int id, int index) {
  LValue offset = output().buildGEPWithByteOffset(
      output().root(), output().constInt32(index * sizeof(void*)),
      pointerType(output().taggedType()));
  LValue value = output().buildLoad(offset);
  current_bb_->set_value(id, value);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

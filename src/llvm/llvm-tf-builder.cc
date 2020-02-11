// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/llvm-tf-builder.h"
#include <llvm/Support/Compiler.h>
#include <bitset>
#include <sstream>
#include "src/codegen/turbo-assembler.h"
#include "src/execution/isolate-data.h"
#include "src/heap/spaces.h"
#include "src/llvm/basic-block-manager.h"
#include "src/llvm/basic-block.h"
#include "src/llvm/load-constant-recorder.h"
#include "src/llvm/output.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
namespace {
// copy from v8.
enum RememberedSetAction { EMIT_REMEMBERED_SET, OMIT_REMEMBERED_SET };

struct NotMergedPhiDesc {
  BasicBlock* pred;
  int value;
  LValue phi;
};

struct GCRelocateDesc {
  int value;
  int where;
  GCRelocateDesc(int v, int w) : value(v), where(w) {}
};

using GCRelocateDescList = std::vector<GCRelocateDesc>;

struct PredecessorCallInfo {
  GCRelocateDescList gc_relocates;
};

enum class ValueType { LLVMValue, RelativeCallTarget };

struct ValueDesc {
  ValueType type;
  union {
    LValue llvm_value;
    int64_t relative_call_target;
  };
};

struct LLVMTFBuilderBasicBlockImpl {
  std::vector<NotMergedPhiDesc> not_merged_phis;
  std::unordered_map<int, ValueDesc> values_;
  PredecessorCallInfo call_info;

  LBasicBlock native_bb = nullptr;
  LBasicBlock continuation = nullptr;
  LValue landing_pad = nullptr;

  bool started = false;
  bool ended = false;
  bool exception_block = false;

  inline void SetLLVMValue(int nid, LValue value) {
    values_[nid] = {ValueType::LLVMValue, {value}};
  }

  inline void SetValue(int nid, const ValueDesc& value) {
    values_[nid] = value;
  }

  inline LValue GetLLVMValue(int nid) const {
    auto found = values_.find(nid);
    EMASSERT(found != values_.end());
    EMASSERT(found->second.type == ValueType::LLVMValue);
    return found->second.llvm_value;
  }

  const ValueDesc& GetValue(int nid) const {
    auto found = values_.find(nid);
    EMASSERT(found != values_.end());
    return found->second;
  }

  inline std::unordered_map<int, ValueDesc>& values() { return values_; }

  inline void StartBuild() {
    EMASSERT(!started);
    EMASSERT(!ended);
    started = true;
  }

  inline void EndBuild() {
    EMASSERT(started);
    EMASSERT(!ended);
    ended = true;
  }
};

LLVMTFBuilderBasicBlockImpl* EnsureImpl(BasicBlock* bb) {
  if (bb->GetImpl<LLVMTFBuilderBasicBlockImpl>())
    return bb->GetImpl<LLVMTFBuilderBasicBlockImpl>();
  std::unique_ptr<LLVMTFBuilderBasicBlockImpl> impl(
      new LLVMTFBuilderBasicBlockImpl);
  bb->SetImpl(impl.release());
  return bb->GetImpl<LLVMTFBuilderBasicBlockImpl>();
}

static LLVMTFBuilderBasicBlockImpl* GetBuilderImpl(BasicBlock* bb) {
  return bb->GetImpl<LLVMTFBuilderBasicBlockImpl>();
}

void EnsureNativeBB(BasicBlock* bb, Output& output) {
  LLVMTFBuilderBasicBlockImpl* impl = EnsureImpl(bb);
  if (impl->native_bb) return;
  char buf[256];
  snprintf(buf, 256, "B%d", bb->id());
  LBasicBlock native_bb = output.appendBasicBlock(buf);
  impl->native_bb = native_bb;
  impl->continuation = native_bb;
}

LBasicBlock GetNativeBB(BasicBlock* bb) {
  return bb->GetImpl<LLVMTFBuilderBasicBlockImpl>()->native_bb;
}

LBasicBlock GetNativeBBContinuation(BasicBlock* bb) {
  return bb->GetImpl<LLVMTFBuilderBasicBlockImpl>()->continuation;
}

bool IsBBStartedToBuild(BasicBlock* bb) {
  auto impl = GetBuilderImpl(bb);
  if (!impl) return false;
  return impl->started;
}

bool IsBBEndedToBuild(BasicBlock* bb) {
  auto impl = GetBuilderImpl(bb);
  if (!impl) return false;
  return impl->ended;
}

void StartBuild(BasicBlock* bb, Output& output) {
  EnsureNativeBB(bb, output);
  GetBuilderImpl(bb)->StartBuild();
  output.positionToBBEnd(GetNativeBB(bb));
}

class ContinuationResolver {
 protected:
  ContinuationResolver(BasicBlock* bb, Output& output, int id);
  ~ContinuationResolver() = default;
  void CreateContination();
  inline Output& output() { return *output_; }
  inline BasicBlock* current_bb() { return current_bb_; }
  inline int id() { return id_; }
  LBasicBlock old_continuation_;
  LLVMTFBuilderBasicBlockImpl* impl_;

 private:
  BasicBlock* current_bb_;
  Output* output_;
  int id_;
};

class CallResolver : public ContinuationResolver {
 public:
  CallResolver(BasicBlock* current_bb, Output& output, int id,
               StackMapInfoMap* stack_map_info_map, int patchid);
  virtual ~CallResolver() = default;
  void Resolve(CallMode mode, const CallDescriptor& call_desc,
               const OperandsVector& operands);

 protected:
  virtual LValue EmitCallInstr(LValue function, LValue* operands,
                               size_t operands_count);
  virtual void BuildCall(const CallDescriptor& call_desc);
  virtual void PopulateCallInfo(CallInfo*);
  virtual bool IsTailCall() const { return false; }
  inline int patchid() { return patchid_; }
  inline size_t location_count() const { return locations_.size(); }
  inline int call_instruction_bytes() const {
    // locations[0] must be the callee. If greater than 1, then an extra stmdb
    // is needed.
    return location_count() > 1 ? 8 : 4;
  }
  inline std::vector<LValue>& operand_values() { return operand_values_; }
  inline std::vector<LType>& operand_value_types() {
    return operand_value_types_;
  }

 private:
  void ResolveOperands(CallMode mode, const OperandsVector& operands,
                       const RegistersForOperands& registers_for_operands);
  void ResolveCallTarget(CallMode mode, int target_id);
  void UpdateAfterStatePoint(LValue statepoint_ret, LType return_type);
  void ProcessGCRelocate(std::vector<LValue>&);
  void PopulateToStackMap();
  inline CallInfo::LocationVector&& release_location() {
    return std::move(locations_);
  }
  void SetOperandValue(int reg, LValue value);
  int FindNextReg();
  size_t AvaiableRegs() const;

  std::bitset<kV8CCRegisterParameterCount>
      allocatable_register_set_; /* 0 is allocatable */
  std::vector<LValue> operand_values_;
  std::vector<LType> operand_value_types_;
  std::vector<GCRelocateDescList> relocate_descs_;
  CallInfo::LocationVector locations_;
  StackMapInfoMap* stack_map_info_map_;
  LValue target_;
  int64_t relative_target_;
  int next_reg_;
  int patchid_;
  int sp_adjust_;
};

class TCCallResolver final : public CallResolver {
 public:
  TCCallResolver(BasicBlock* current_bb, Output& output, int id,
                 StackMapInfoMap* stack_map_info_map, int patchid);
  ~TCCallResolver() = default;

 private:
  void BuildCall(const CallDescriptor& call_desc) override;
  void PopulateCallInfo(CallInfo*) override;
  bool IsTailCall() const final { return true; }
};

class InvokeResolver final : public CallResolver {
 public:
  InvokeResolver(BasicBlock* current_bb, Output& output, int id,
                 StackMapInfoMap* stack_map_info_map, int patchid,
                 BasicBlock* then, BasicBlock* exception);
  ~InvokeResolver() = default;

 private:
  LValue EmitCallInstr(LValue function, LValue* operands,
                       size_t operands_count) override;

  BasicBlock* then_bb_;
  BasicBlock* exception_bb_;
};

class StoreBarrierResolver final : public ContinuationResolver {
 public:
  StoreBarrierResolver(BasicBlock* bb, Output& output, int id,
                       StackMapInfoMap* stack_map_info_map, int patch_point_id);
  void Resolve(LValue base, LValue offset, LValue value,
               compiler::WriteBarrierKind barrier_kind,
               std::function<LValue()> record_write);

 private:
  void CheckPageFlag(LValue base, int flags);
  void CallPatchpoint(LValue base, LValue offset, LValue remembered_set_action,
                      compiler::WriteBarrierKind barrier_kind,
                      std::function<LValue()> record_write);
  void CheckSmi(LValue value);
  StackMapInfoMap* stack_map_info_map_;
  int patch_point_id_;
};

class TruncateFloat64ToWord32Resolver final : public ContinuationResolver {
 public:
  TruncateFloat64ToWord32Resolver(BasicBlock* bb, Output& output, int id);
  LValue Resolve(LValue fp);

 private:
  void FastPath(LValue fp, LBasicBlock slow_bb);
  void SlowPath(LValue fp, LBasicBlock slow_bb);
  void OutOfRange(LBasicBlock bb);
  void Extract(LBasicBlock bb, LValue fp_low, LValue fp_high,
               LValue sub_exponent);
  LValue OnlyLow(LBasicBlock only_low_bb, LBasicBlock negate_bb, LValue fp_low,
                 LValue sub_exponent);
  LValue Mix(LBasicBlock mix_bb, LBasicBlock negate_bb, LValue fp_low,
             LValue fp_high, LValue sub_exponent);
  void Negate(LBasicBlock negate_bb, LValue only_low_value, LValue mix_value,
              LBasicBlock only_low_bb, LBasicBlock mix_bb, LValue fp_high);

  std::vector<LValue> to_merge_value_;
  std::vector<LBasicBlock> to_merge_block_;
};

CallResolver::CallResolver(BasicBlock* current_bb, Output& output, int id,
                           StackMapInfoMap* stack_map_info_map, int patchid)
    : ContinuationResolver(current_bb, output, id),
      allocatable_register_set_(kTargetRegParameterNotAllocatable),
      operand_values_(kV8CCRegisterParameterCount,
                      LLVMGetUndef(output.repo().intPtr)),
      operand_value_types_(kV8CCRegisterParameterCount, output.repo().intPtr),
      stack_map_info_map_(stack_map_info_map),
      target_(nullptr),
      relative_target_(0),
      next_reg_(0),
      patchid_(patchid),
      sp_adjust_(0) {}

int CallResolver::FindNextReg() {
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

size_t CallResolver::AvaiableRegs() const {
  return allocatable_register_set_.size() - allocatable_register_set_.count();
}

void CallResolver::SetOperandValue(int reg, LValue llvm_value) {
  LType llvm_val_type = typeOf(llvm_value);
  if (reg >= 0) {
    operand_values_[reg] = llvm_value;
    operand_value_types_[reg] = llvm_val_type;
    allocatable_register_set_.set(reg);
  } else {
    operand_values_.push_back(llvm_value);
    operand_value_types_.push_back(llvm_val_type);
  }
}

void CallResolver::Resolve(CallMode mode, const CallDescriptor& call_desc,
                           const OperandsVector& operands) {
  ResolveOperands(mode, operands, call_desc.registers_for_operands);
  BuildCall(call_desc);
  PopulateToStackMap();
}

void CallResolver::ResolveCallTarget(CallMode mode, int target_id) {
  switch (mode) {
    case CallMode::kCode: {
      auto& value = GetBuilderImpl(current_bb())->GetValue(target_id);
      if (value.type == ValueType::RelativeCallTarget) {
        relative_target_ = value.relative_call_target;
        return;
      }
      EMASSERT(value.type == ValueType::LLVMValue);
      LValue code_value = value.llvm_value;
      if (typeOf(code_value) != output().taggedType()) {
        EMASSERT(typeOf(code_value) == output().repo().ref8);
        target_ = code_value;
      } else {
        target_ = output().buildGEPWithByteOffset(
            code_value, output().constInt32(Code::kHeaderSize - kHeapObjectTag),
            output().repo().ref8);
      }
      break;
    }
    case CallMode::kAddress: {
      int addr_value = target_id;
      target_ = GetBuilderImpl(current_bb())->GetLLVMValue(addr_value);
      break;
    }
    case CallMode::kBuiltin: {
      LValue builtin_offset = output().buildShl(
          GetBuilderImpl(current_bb())->GetLLVMValue(target_id),
          output().constIntPtr(kSystemPointerSizeLog2 - kSmiTagSize));
      builtin_offset = output().buildAdd(
          builtin_offset,
          output().constIntPtr(IsolateData::builtin_entry_table_offset()));
      builtin_offset = output().buildGEPWithByteOffset(
          output().root(), builtin_offset, pointerType(output().repo().ref8));
      target_ = output().buildLoad(builtin_offset);
      break;
    }
  }
}

void CallResolver::ResolveOperands(
    CallMode mode, const OperandsVector& operands,
    const RegistersForOperands& registers_for_operands) {
  auto operands_iterator = operands.begin();
  // layout
  // return value | register operands | stack operands | artifact operands
  int target_id = *(operands_iterator++);
  ResolveCallTarget(mode, target_id);
  // setup register operands
  OperandsVector stack_operands;
  std::vector<std::tuple<int, int>> floatpoint_operands;
  for (int reg : registers_for_operands) {
    EMASSERT(reg < kV8CCRegisterParameterCount);
    int operand_id = *(operands_iterator++);
    LValue llvm_value = GetBuilderImpl(current_bb())->GetLLVMValue(operand_id);
    LType llvm_value_type = typeOf(llvm_value);
    if (llvm_value_type == output().repo().doubleType ||
        llvm_value_type == output().repo().floatType) {
      floatpoint_operands.emplace_back(operand_id, reg);
      continue;
    }
    if (reg < 0) {
      int stack_index = -reg - 1;
      if (stack_operands.size() <= static_cast<size_t>(stack_index))
        stack_operands.resize(stack_index + 1);
      stack_operands[stack_index] = operand_id;
      continue;
    }
    SetOperandValue(reg, llvm_value);
  }
  // setup callee value.
  int target_reg = -1;
  if (target_) {
    target_reg = FindNextReg();
    SetOperandValue(target_reg, target_);
  }
  locations_.push_back(target_reg);

  if (stack_operands.size() <= kV8CCMaxStackParameterToReg &&
      AvaiableRegs() >= stack_operands.size()) {
    std::vector<int> allocated_regs;
    for (size_t i = 0; i != stack_operands.size(); ++i) {
      int reg = FindNextReg();
      EMASSERT(reg >= 0);
      allocated_regs.push_back(reg);
    }
    auto reg_iterator = allocated_regs.begin();

    for (auto operand : stack_operands) {
      LValue llvm_value = GetBuilderImpl(current_bb())->GetLLVMValue(operand);
      int reg = *(reg_iterator++);
      SetOperandValue(reg, llvm_value);
      locations_.push_back(reg);
    }
  } else {
    CHECK(!IsTailCall());
    for (auto operand : stack_operands) {
      LValue llvm_value = GetBuilderImpl(current_bb())->GetLLVMValue(operand);
      LType type = typeOf(llvm_value);
      EMASSERT(type != output().repo().floatType &&
               type != output().repo().doubleType);
      SetOperandValue(-1, llvm_value);
    }
    sp_adjust_ = stack_operands.size() * kPointerSize;
  }

  int current_floatpoint_reg = 0;
  for (auto tuple : floatpoint_operands) {
    int operand, reg;
    std::tie(operand, reg) = tuple;
    LValue llvm_value = GetBuilderImpl(current_bb())->GetLLVMValue(operand);
    while (current_floatpoint_reg != reg) {
      SetOperandValue(-1, LLVMGetUndef(output().repo().floatType));
      current_floatpoint_reg++;
    }
    SetOperandValue(-1, llvm_value);
    current_floatpoint_reg++;
  }
}

void CallResolver::BuildCall(const CallDescriptor& call_desc) {
  std::vector<LValue> statepoint_operands;

  LType return_type;
  if (call_desc.return_types.size() == 2) {
    return_type = structType(
        output().repo().context_,
        output().getLLVMTypeFromMachineType(call_desc.return_types[0]),
        output().getLLVMTypeFromMachineType(call_desc.return_types[1]));
  } else if (call_desc.return_types.size() == 1) {
    return_type =
        output().getLLVMTypeFromMachineType(call_desc.return_types[0]);
  } else if (call_desc.return_types.size() == 0) {
    return_type = output().repo().voidType;
  } else {
    EMASSERT("return types should only be [0-2]" && false);
  }
  LType callee_function_type =
      functionType(return_type, operand_value_types().data(),
                   operand_value_types().size(), NotVariadic);
  LType callee_type = pointerType(callee_function_type);
  statepoint_operands.push_back(output().constInt64(patchid()));
  statepoint_operands.push_back(output().constInt32(call_instruction_bytes()));
  statepoint_operands.push_back(constNull(callee_type));
  statepoint_operands.push_back(
      output().constInt32(operand_values().size()));      // # call params
  statepoint_operands.push_back(output().constInt32(0));  // flags
  for (LValue operand_value : operand_values())
    statepoint_operands.push_back(operand_value);
  statepoint_operands.push_back(output().constInt32(0));  // # transition args
  statepoint_operands.push_back(output().constInt32(0));  // # deopt arguments
  // push current defines
  ProcessGCRelocate(statepoint_operands);
  LValue statepoint_ret =
      EmitCallInstr(output().getStatePointFunction(callee_type),
                    statepoint_operands.data(), statepoint_operands.size());
  LLVMSetInstructionCallConv(statepoint_ret, LLVMV8CallConv);
  UpdateAfterStatePoint(statepoint_ret, return_type);
}

void CallResolver::ProcessGCRelocate(std::vector<LValue>& statepoint_operands) {
  // value, pos
  std::unordered_map<int, int> position_map;
  auto& values = GetBuilderImpl(current_bb())->values();
  for (BasicBlock* successor : current_bb()->successors()) {
    auto& successor_liveins = successor->liveins();
    GCRelocateDescList desc_list;
    for (int livein : successor_liveins) {
      if (livein == id()) continue;
      auto found = values.find(livein);
      EMASSERT(found != values.end());
      if (found->second.type != ValueType::LLVMValue) continue;
      LValue to_gc = found->second.llvm_value;
      if (typeOf(to_gc) != output().taggedType()) continue;
      auto pair = position_map.emplace(livein, statepoint_operands.size());
      if (pair.second) {
        statepoint_operands.emplace_back(to_gc);
      }
      desc_list.emplace_back(livein, pair.first->second);
    }
    relocate_descs_.emplace_back(desc_list);
  }
}

void CallResolver::UpdateAfterStatePoint(LValue statepoint_ret,
                                         LType return_type) {
  if (current_bb()->successors().empty()) return;
  if (current_bb()->successors().size() == 2) {
    BasicBlock* exception = current_bb()->successors()[1];
    GetBuilderImpl(exception)->exception_block = true;
    GetBuilderImpl(exception)->call_info.gc_relocates =
        std::move(relocate_descs_[1]);
  }
  for (auto& gc_relocate : relocate_descs_[0]) {
    LValue relocated = output().buildCall(
        output().repo().gcRelocateIntrinsic(), statepoint_ret,
        output().constInt32(gc_relocate.where),
        output().constInt32(gc_relocate.where));
    GetBuilderImpl(current_bb())->SetLLVMValue(gc_relocate.value, relocated);
  }
  LValue intrinsic = output().getGCResultFunction(return_type);
  LValue ret = output().buildCall(intrinsic, statepoint_ret);
  GetBuilderImpl(current_bb())->SetLLVMValue(id(), ret);
}

void CallResolver::PopulateToStackMap() {
  // save patch point info
  std::unique_ptr<StackMapInfo> info(
      new CallInfo(std::move(release_location())));
  PopulateCallInfo(static_cast<CallInfo*>(info.get()));
  stack_map_info_map_->emplace(patchid_, std::move(info));
}

void CallResolver::PopulateCallInfo(CallInfo* callinfo) {
  callinfo->set_sp_adjust(sp_adjust_);
  if (relative_target_) callinfo->set_relative_target(relative_target_);
}

TCCallResolver::TCCallResolver(BasicBlock* current_bb, Output& output, int id,
                               StackMapInfoMap* stack_map_info_map, int patchid)
    : CallResolver(current_bb, output, id, stack_map_info_map, patchid) {}

void TCCallResolver::BuildCall(const CallDescriptor& call_desc) {
  std::vector<LValue> patchpoint_operands;
  patchpoint_operands.push_back(output().constInt64(patchid()));
  int additional_instruction_bytes = 0;
  if (output().stack_parameter_count() > 0) additional_instruction_bytes = 4;
  patchpoint_operands.push_back(output().constInt32(
      call_instruction_bytes() + additional_instruction_bytes));
  patchpoint_operands.push_back(constNull(output().repo().ref8));
  patchpoint_operands.push_back(
      output().constInt32(operand_values().size()));  // # call params
  for (LValue operand_value : operand_values())
    patchpoint_operands.push_back(operand_value);
  LValue patchpoint_ret =
      EmitCallInstr(output().repo().patchpointVoidIntrinsic(),
                    patchpoint_operands.data(), patchpoint_operands.size());
  LLVMSetInstructionCallConv(patchpoint_ret, LLVMV8CallConv);
}

void TCCallResolver::PopulateCallInfo(CallInfo* callinfo) {
  CallResolver::PopulateCallInfo(callinfo);
  callinfo->set_is_tailcall(true);
  callinfo->set_tailcall_return_count(output().stack_parameter_count());
}

LValue CallResolver::EmitCallInstr(LValue function, LValue* operands,
                                   size_t operands_count) {
  LValue ret = output().buildCall(function, operands, operands_count);
  CreateContination();
  output().buildBr(impl_->continuation);
  output().positionToBBEnd(impl_->continuation);
  return ret;
}

InvokeResolver::InvokeResolver(BasicBlock* current_bb, Output& output, int id,
                               StackMapInfoMap* stack_map_info_map, int patchid,
                               BasicBlock* then, BasicBlock* exception)
    : CallResolver(current_bb, output, id, stack_map_info_map, patchid),
      then_bb_(then),
      exception_bb_(exception) {}

LValue InvokeResolver::EmitCallInstr(LValue function, LValue* operands,
                                     size_t operands_count) {
  CreateContination();
  LValue ret =
      output().buildInvoke(function, operands, operands_count,
                           impl_->continuation, GetNativeBB(exception_bb_));
  output().positionToBBEnd(impl_->continuation);
  output().buildBr(GetNativeBB(then_bb_));
  LValue terminator = LLVMGetBasicBlockTerminator(impl_->continuation);
  output().positionBefore(terminator);
  return ret;
}

ContinuationResolver::ContinuationResolver(BasicBlock* bb, Output& output,
                                           int id)
    : old_continuation_(nullptr),
      impl_(nullptr),
      current_bb_(bb),
      output_(&output),
      id_(id) {}

void ContinuationResolver::CreateContination() {
  impl_ = GetBuilderImpl(current_bb());
  char buf[256];
  snprintf(buf, 256, "B%d_value%d_continuation", current_bb()->id(), id());
  old_continuation_ = impl_->continuation;
  impl_->continuation = output().appendBasicBlock(buf);
}

StoreBarrierResolver::StoreBarrierResolver(BasicBlock* bb, Output& output,
                                           int id,
                                           StackMapInfoMap* stack_map_info_map,
                                           int patch_point_id)
    : ContinuationResolver(bb, output, id),
      stack_map_info_map_(stack_map_info_map),
      patch_point_id_(patch_point_id) {}

void StoreBarrierResolver::Resolve(LValue base, LValue offset, LValue value,
                                   compiler::WriteBarrierKind barrier_kind,
                                   std::function<LValue()> record_write) {
  CreateContination();
  CheckPageFlag(base, MemoryChunk::kPointersFromHereAreInterestingMask);
  if (barrier_kind > compiler::kPointerWriteBarrier) {
    CheckSmi(value);
  }
  CheckPageFlag(value, MemoryChunk::kPointersToHereAreInterestingMask);
  RememberedSetAction const remembered_set_action =
      barrier_kind > compiler::kMapWriteBarrier ? EMIT_REMEMBERED_SET
                                                : OMIT_REMEMBERED_SET;
  // now v8cc clobbers all fp.
  CallPatchpoint(
      base, offset,
      output().constIntPtr(static_cast<int>(remembered_set_action) << 1),
      barrier_kind, record_write);
  output().buildBr(impl_->continuation);
  output().positionToBBEnd(impl_->continuation);
}

void StoreBarrierResolver::CheckPageFlag(LValue base, int mask) {
  LValue base_int =
      output().buildCast(LLVMPtrToInt, base, output().repo().intPtr);
  const int page_mask = ~((1 << kPageSizeBits) - 1);
  LValue memchunk_int =
      output().buildAnd(base_int, output().constIntPtr(page_mask));
  LValue memchunk_ref8 =
      output().buildCast(LLVMIntToPtr, memchunk_int, output().repo().ref8);
  LValue flag_slot = output().buildGEPWithByteOffset(
      memchunk_ref8, output().constInt32(MemoryChunk::kFlagsOffset),
      output().repo().ref32);
  LValue flag = output().buildLoad(flag_slot);
  LValue and_result = output().buildAnd(flag, output().constInt32(mask));
  LValue cmp =
      output().buildICmp(LLVMIntEQ, and_result, output().repo().int32Zero);
  cmp = output().buildCall(output().repo().expectIntrinsic(), cmp,
                           output().repo().booleanTrue);

  char buf[256];
  snprintf(buf, 256, "B%d_value%d_checkpageflag_%d", current_bb()->id(), id(),
           mask);
  LBasicBlock continuation = output().appendBasicBlock(buf);
  output().buildCondBr(cmp, GetNativeBBContinuation(current_bb()),
                       continuation);
  output().positionToBBEnd(continuation);
}

void StoreBarrierResolver::CallPatchpoint(
    LValue base, LValue offset, LValue remembered_set_action,
    compiler::WriteBarrierKind barrier_kind,
    std::function<LValue()> get_record_write) {
  // blx ip
  // 1 instructions.
  int instructions_count = 2;
  int patchid = patch_point_id_;
  // will not be true again.
  LValue stub_entry = LLVMGetUndef(output().repo().ref8);
  LValue save_fp_mode = LLVMGetUndef(output().repo().intPtr);
  if (!FLAG_embedded_builtins) {
    LValue stub = get_record_write();
    stub_entry = output().buildGEPWithByteOffset(
        stub, output().constInt32(Code::kHeaderSize - kHeapObjectTag),
        output().repo().ref8);
  }

  LValue call = output().buildCall(
      output().repo().patchpointVoidIntrinsic(), output().constInt64(patchid),
      output().constInt32(4 * instructions_count),
      constNull(output().repo().ref8), output().constInt32(8), base, offset,
      remembered_set_action, save_fp_mode, LLVMGetUndef(output().taggedType()),
      LLVMGetUndef(typeOf(output().root())),
      LLVMGetUndef(typeOf(output().fp())), stub_entry);
  LLVMSetInstructionCallConv(call, LLVMV8SBCallConv);
  std::unique_ptr<StoreBarrierInfo> info(new StoreBarrierInfo());
  info->set_write_barrier_kind(barrier_kind);
  EMASSERT(barrier_kind == compiler::kFullWriteBarrier ||
           FLAG_embedded_builtins);
  stack_map_info_map_->emplace(patchid, std::move(info));
}

void StoreBarrierResolver::CheckSmi(LValue value) {
  LValue value_int =
      output().buildCast(LLVMPtrToInt, value, output().repo().intPtr);
  LValue and_result = output().buildAnd(value_int, output().repo().intPtrOne);
  LValue cmp =
      output().buildICmp(LLVMIntEQ, and_result, output().repo().int32Zero);
  cmp = output().buildCall(output().repo().expectIntrinsic(), cmp,
                           output().repo().booleanFalse);
  char buf[256];
  snprintf(buf, 256, "B%d_value%d_checksmi", current_bb()->id(), id());
  LBasicBlock continuation = output().appendBasicBlock(buf);
  output().buildCondBr(cmp, GetNativeBBContinuation(current_bb()),
                       continuation);
  output().positionToBBEnd(continuation);
}

TruncateFloat64ToWord32Resolver::TruncateFloat64ToWord32Resolver(BasicBlock* bb,
                                                                 Output& output,
                                                                 int id)
    : ContinuationResolver(bb, output, id) {}

LValue TruncateFloat64ToWord32Resolver::Resolve(LValue fp) {
  CreateContination();
  char buf[256];
  snprintf(buf, 256, "B_value%d_truncatefloat64toword32_slow", id());
  LBasicBlock slow_bb = output().appendBasicBlock(buf);
  FastPath(fp, slow_bb);
  SlowPath(fp, slow_bb);
#if 0
  char kUdf[] = "udf #0\n";
  char empty[] = "\0";
  output().buildInlineAsm(functionType(output().repo().voidType), kUdf,
                          sizeof(kUdf) - 1, empty, 0, true);
  output().buildUnreachable();
#endif
  output().positionToBBEnd(impl_->continuation);
  LValue real_return = output().buildPhi(output().repo().int32);
  addIncoming(real_return, to_merge_value_.data(), to_merge_block_.data(),
              to_merge_block_.size());
  return real_return;
}

void TruncateFloat64ToWord32Resolver::FastPath(LValue fp, LBasicBlock slow_bb) {
  LValue maybe_return =
      output().buildCast(LLVMFPToSI, fp, output().repo().int32);
  LValue subed = output().buildSub(maybe_return, output().constInt32(1));
  LValue cmp_val =
      output().buildICmp(LLVMIntSGE, subed, output().constInt32(0x7ffffffe));
  cmp_val = output().buildCall(output().repo().expectIntrinsic(), cmp_val,
                               output().repo().booleanFalse);

  output().buildCondBr(cmp_val, slow_bb, impl_->continuation);
  to_merge_value_.push_back(maybe_return);
  to_merge_block_.push_back(old_continuation_);
}

void TruncateFloat64ToWord32Resolver::SlowPath(LValue fp, LBasicBlock slow_bb) {
  output().positionToBBEnd(slow_bb);
  LValue fp_storage =
      output().buildBitCast(output().bitcast_space(), pointerType(typeOf(fp)));
  output().buildStore(fp, fp_storage);
  LValue fp_bitcast_pointer_low =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  LValue fp_low = output().buildLoad(fp_bitcast_pointer_low);
  LValue fp_bitcast_pointer_high = output().buildGEPWithByteOffset(
      fp_bitcast_pointer_low, output().constInt32(sizeof(int32_t)),
      output().repo().ref32);
  LValue fp_high = output().buildLoad(fp_bitcast_pointer_high);
  LValue exponent = output().buildShr(
      fp_high, output().constInt32(HeapNumber::kExponentShift));
  exponent = output().buildAnd(
      exponent, output().constInt32((1 << HeapNumber::kExponentBits) - 1));
  LValue sub_exponent = output().buildSub(
      exponent, output().constInt32(HeapNumber::kExponentBias + 1));
  LValue cmp_value =
      output().buildICmp(LLVMIntSGE, sub_exponent, output().constInt32(83));
  char buf[256];
  snprintf(buf, 256, "B_value%d_truncatefloat64toword32_out_of_range", id());
  LBasicBlock out_of_range_bb = output().appendBasicBlock(buf);
  snprintf(buf, 256, "B_value%d_truncatefloat64toword32_extract", id());
  LBasicBlock extract_bb = output().appendBasicBlock(buf);
  output().buildCondBr(cmp_value, out_of_range_bb, extract_bb);
  OutOfRange(out_of_range_bb);
  Extract(extract_bb, fp_low, fp_high, sub_exponent);
}

void TruncateFloat64ToWord32Resolver::OutOfRange(LBasicBlock bb) {
  output().positionToBBEnd(bb);
  LValue zero = output().repo().int32Zero;
  to_merge_value_.push_back(zero);
  to_merge_block_.push_back(bb);
  output().buildBr(impl_->continuation);
}

void TruncateFloat64ToWord32Resolver::Extract(LBasicBlock bb, LValue fp_low,
                                              LValue fp_high,
                                              LValue sub_exponent) {
  output().positionToBBEnd(bb);
  sub_exponent = output().buildSub(output().constInt32(51), sub_exponent);
  char buf[256];
  snprintf(buf, 256, "B_value%d_truncatefloat64toword32_only_low", id());
  LBasicBlock only_low_bb = output().appendBasicBlock(buf);
  snprintf(buf, 256, "B_value%d_truncatefloat64toword32_mix", id());
  LBasicBlock mix_bb = output().appendBasicBlock(buf);
  snprintf(buf, 256, "B_value%d_truncatefloat64toword32_negate", id());
  LBasicBlock negate_bb = output().appendBasicBlock(buf);
  LValue cmp_value =
      output().buildICmp(LLVMIntSLE, sub_exponent, output().repo().int32Zero);
  output().buildCondBr(cmp_value, only_low_bb, mix_bb);
  LValue only_low_value = OnlyLow(only_low_bb, negate_bb, fp_low, sub_exponent);
  LValue mix_value = Mix(mix_bb, negate_bb, fp_low, fp_high, sub_exponent);
  Negate(negate_bb, only_low_value, mix_value, only_low_bb, mix_bb, fp_high);
}

LValue TruncateFloat64ToWord32Resolver::OnlyLow(LBasicBlock only_low_bb,
                                                LBasicBlock negate_bb,
                                                LValue fp_low,
                                                LValue sub_exponent) {
  output().positionToBBEnd(only_low_bb);
  sub_exponent = output().buildNeg(sub_exponent);
  LValue to_return = output().buildShl(fp_low, sub_exponent);
  output().buildBr(negate_bb);
  return to_return;
}

LValue TruncateFloat64ToWord32Resolver::Mix(LBasicBlock mix_bb,
                                            LBasicBlock negate_bb,
                                            LValue fp_low, LValue fp_high,
                                            LValue sub_exponent) {
  output().positionToBBEnd(mix_bb);
  LValue low = output().buildShr(fp_low, sub_exponent);
  sub_exponent = output().buildSub(output().constInt32(32), sub_exponent);
  LValue mantissa_bits_in_top_word = output().buildAnd(
      fp_high,
      output().constInt32((1 << HeapNumber::kMantissaBitsInTopWord) - 1));
  LValue high = output().buildOr(
      mantissa_bits_in_top_word,
      output().constInt32(1 << HeapNumber::kMantissaBitsInTopWord));
  high = output().buildShl(high, sub_exponent);
  LValue to_return = output().buildOr(low, high);
  output().buildBr(negate_bb);
  return to_return;
}

void TruncateFloat64ToWord32Resolver::Negate(
    LBasicBlock negate_bb, LValue only_low_value, LValue mix_value,
    LBasicBlock only_low_bb, LBasicBlock mix_bb, LValue fp_high) {
  output().positionToBBEnd(negate_bb);
  LValue to_merge = output().buildPhi(output().repo().int32);
  addIncoming(to_merge, &only_low_value, &only_low_bb, 1);
  addIncoming(to_merge, &mix_value, &mix_bb, 1);
  LValue shr = output().buildShr(fp_high, output().constInt32(31));
  LValue asr = output().buildSar(fp_high, output().constInt32(31));
  LValue final_value = output().buildXor(to_merge, asr);
  final_value = output().buildAdd(final_value, shr);
  output().buildBr(impl_->continuation);
  to_merge_value_.push_back(final_value);
  to_merge_block_.push_back(negate_bb);
}

}  // namespace

LLVMTFBuilder::LLVMTFBuilder(Output& output,
                             BasicBlockManager& basic_block_manager,
                             StackMapInfoMap& stack_map_info_map,
                             LoadConstantRecorder& load_constant_recorder)
    : output_(&output),
      basic_block_manager_(&basic_block_manager),
      current_bb_(nullptr),
      stack_map_info_map_(&stack_map_info_map),
      load_constant_recorder_(&load_constant_recorder),
      get_record_write_function_(nullptr),
      get_mod_two_double_function_(nullptr),
      int32_pair_type_(nullptr),
      line_number_(-1),
      debug_file_name_(nullptr),
      state_point_id_next_(0) {
  int32_pair_type_ = structType(output.repo().context_, output.repo().int32,
                                output.repo().int32);
}

void LLVMTFBuilder::End(BuiltinFunctionClient* builtin_function_client) {
  EMASSERT(!!current_bb_);
  EndCurrentBlock();
  ProcessPhiWorkList();
  output().positionToBBEnd(output().prologue());
  output().buildBr(GetNativeBB(
      basic_block_manager().findBB(*basic_block_manager().rpo().begin())));
  output().finalize();
  v8::internal::tf_llvm::ResetImpls<LLVMTFBuilderBasicBlockImpl>(
      basic_block_manager());

  BuildGetModTwoDoubleFunction(builtin_function_client);
  BuildGetRecordWriteBuiltin(builtin_function_client);
}

void LLVMTFBuilder::MergePredecessors(BasicBlock* bb) {
  if (bb->predecessors().empty()) return;
  if (bb->predecessors().size() == 1) {
    // Don't use phi if only one predecessor.
    BasicBlock* pred = bb->predecessors()[0];
    EMASSERT(IsBBStartedToBuild(pred));
    for (int live : bb->liveins()) {
      auto& value = GetBuilderImpl(pred)->GetValue(live);
      GetBuilderImpl(bb)->SetValue(live, value);
    }
    if (GetBuilderImpl(bb)->exception_block) {
      LValue landing_pad = output().buildLandingPad();
      LValue call_value = landing_pad;
      PredecessorCallInfo& callinfo = GetBuilderImpl(bb)->call_info;
      auto& values = GetBuilderImpl(bb)->values();
      for (auto& gc_relocate : callinfo.gc_relocates) {
        auto found = values.find(gc_relocate.value);
        EMASSERT(found->second.type == ValueType::LLVMValue);
        LValue relocated = output().buildCall(
            output().repo().gcRelocateIntrinsic(), call_value,
            output().constInt32(gc_relocate.where),
            output().constInt32(gc_relocate.where));
        found->second.llvm_value = relocated;
      }
      GetBuilderImpl(bb)->landing_pad = landing_pad;
    }
    return;
  }
  BasicBlock* ref_pred = nullptr;
  if (!AllPredecessorStarted(bb, &ref_pred)) {
    EMASSERT(!!ref_pred);
    BuildPhiAndPushToWorkList(bb, ref_pred);
    return;
  }
  // Use phi.
  for (int live : bb->liveins()) {
    auto& value = GetBuilderImpl(ref_pred)->GetValue(live);
    if (value.type != ValueType::LLVMValue) {
      GetBuilderImpl(bb)->SetValue(live, value);
      continue;
    }
    LValue ref_value = value.llvm_value;
    LType ref_type = typeOf(ref_value);
    if (ref_type != output().taggedType()) {
      // FIXME: Should add EMASSERT that all values are the same.
      GetBuilderImpl(bb)->SetLLVMValue(live, ref_value);
      continue;
    }
    LValue phi = output().buildPhi(ref_type);
    for (BasicBlock* pred : bb->predecessors()) {
      LValue value = GetBuilderImpl(pred)->GetLLVMValue(live);
      LBasicBlock native = GetNativeBBContinuation(pred);
      addIncoming(phi, &value, &native, 1);
    }
    GetBuilderImpl(bb)->SetLLVMValue(live, phi);
  }
}

bool LLVMTFBuilder::AllPredecessorStarted(BasicBlock* bb,
                                          BasicBlock** ref_pred) {
  bool ret_value = true;
  for (BasicBlock* pred : bb->predecessors()) {
    if (IsBBStartedToBuild(pred)) {
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
    const ValueDesc& value_desc = GetBuilderImpl(ref_pred)->GetValue(live);
    if (value_desc.type != ValueType::LLVMValue) {
      GetBuilderImpl(bb)->SetValue(live, value_desc);
      continue;
    }
    LValue ref_value = value_desc.llvm_value;
    LType ref_type = typeOf(ref_value);
    if (ref_type != output().taggedType()) {
      GetBuilderImpl(bb)->SetLLVMValue(live, ref_value);
      continue;
    }
    LValue phi = output().buildPhi(ref_type);
    GetBuilderImpl(bb)->SetLLVMValue(live, phi);
    for (BasicBlock* pred : bb->predecessors()) {
      if (!IsBBStartedToBuild(pred)) {
        impl->not_merged_phis.emplace_back();
        NotMergedPhiDesc& not_merged_phi = impl->not_merged_phis.back();
        not_merged_phi.phi = phi;
        not_merged_phi.value = live;
        not_merged_phi.pred = pred;
        continue;
      }
      LValue value = GetBuilderImpl(pred)->GetLLVMValue(live);
      LBasicBlock native = GetNativeBBContinuation(pred);
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
      EMASSERT(IsBBStartedToBuild(pred));
      LValue value = EnsurePhiInput(pred, e.value, typeOf(e.phi));
      LBasicBlock native = GetNativeBBContinuation(pred);
      addIncoming(e.phi, &value, &native, 1);
    }
    impl->not_merged_phis.clear();
  }
  phi_rebuild_worklist_.clear();
}

void LLVMTFBuilder::DoTailCall(int id, CallMode mode,
                               const CallDescriptor& call_desc,
                               const OperandsVector& operands) {
  DoCall(id, mode, call_desc, operands, true);
  output().buildReturnForTailCall();
}

void LLVMTFBuilder::DoCall(int id, CallMode mode,
                           const CallDescriptor& call_desc,
                           const OperandsVector& operands, bool tailcall) {
  std::unique_ptr<CallResolver> call_resolver;
  if (!tailcall)
    call_resolver.reset(new CallResolver(current_bb_, output(), id,
                                         stack_map_info_map_,
                                         state_point_id_next_++));
  else
    call_resolver.reset(new TCCallResolver(current_bb_, output(), id,
                                           stack_map_info_map_,
                                           state_point_id_next_++));
  call_resolver->Resolve(mode, call_desc, operands);
}

LValue LLVMTFBuilder::EnsureWord32(LValue v) {
  LType type = typeOf(v);
  LLVMTypeKind kind = LLVMGetTypeKind(type);
  if (kind == LLVMPointerTypeKind) {
    return output().buildCast(LLVMPtrToInt, v, output().repo().int32);
  }
  if (type == output().repo().boolean) {
    return output().buildCast(LLVMZExt, v, output().repo().int32);
  }
  EMASSERT(type == output().repo().int32);
  return v;
}

LValue LLVMTFBuilder::EnsureWord64(LValue v) {
  LType type = typeOf(v);
  if (type == output().repo().int64) return v;
  LLVMTypeKind kind = LLVMGetTypeKind(type);
  if (kind == LLVMPointerTypeKind) {
    return output().buildCast(LLVMPtrToInt, v, output().repo().int64);
  }
  if (kind == LLVMIntegerTypeKind) {
    return output().buildCast(LLVMZExt, v, output().repo().int64);
  }
  return v;
}

LValue LLVMTFBuilder::EnsureBoolean(LValue v) {
  LType type = typeOf(v);
  LLVMTypeKind kind = LLVMGetTypeKind(type);
  if (kind == LLVMPointerTypeKind)
    v = output().buildCast(LLVMPtrToInt, v, output().repo().intPtr);
  type = typeOf(v);
  if (LLVMGetIntTypeWidth(type) == 1) return v;
  v = output().buildICmp(LLVMIntNE, v, output().repo().intPtrZero);
  return v;
}

LValue LLVMTFBuilder::EnsurePhiInput(BasicBlock* pred, int index, LType type) {
  LValue val = GetBuilderImpl(pred)->GetLLVMValue(index);
  LType value_type = typeOf(val);
  if (value_type == type) return val;
  LValue terminator =
      LLVMGetBasicBlockTerminator(GetNativeBBContinuation(pred));
  if ((value_type == output().repo().intPtr) &&
      (type == output().taggedType())) {
    output().positionBefore(terminator);
    LValue ret_val =
        output().buildCast(LLVMIntToPtr, val, output().taggedType());
    return ret_val;
  }
  LLVMTypeKind value_type_kind = LLVMGetTypeKind(value_type);
  if ((LLVMPointerTypeKind == value_type_kind) &&
      (type == output().repo().intPtr)) {
    output().positionBefore(terminator);
    LValue ret_val =
        output().buildCast(LLVMPtrToInt, val, output().repo().intPtr);
    return ret_val;
  }
  if ((value_type == output().repo().boolean) &&
      (type == output().repo().intPtr)) {
    output().positionBefore(terminator);
    LValue ret_val = output().buildCast(LLVMZExt, val, output().repo().intPtr);
    return ret_val;
  }
  LLVMTypeKind type_kind = LLVMGetTypeKind(type);
  if ((LLVMIntegerTypeKind == value_type_kind) &&
      (value_type_kind == type_kind)) {
    // handle both integer
    output().positionBefore(terminator);
    LValue ret_val;
    if (LLVMGetIntTypeWidth(value_type) > LLVMGetIntTypeWidth(type)) {
      ret_val = output().buildCast(LLVMTrunc, val, type);
    } else {
      ret_val = output().buildCast(LLVMZExt, val, type);
    }
    return ret_val;
  }
  __builtin_trap();
}

LValue LLVMTFBuilder::EnsurePhiInputAndPosition(BasicBlock* pred, int index,
                                                LType type) {
  LValue value = EnsurePhiInput(pred, index, type);
  output().positionToBBEnd(GetNativeBB(current_bb_));
  return value;
}

void LLVMTFBuilder::EndCurrentBlock() {
  if (current_bb_->successors().empty()) output().buildUnreachable();
  GetBuilderImpl(current_bb_)->EndBuild();
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
  output().buildBr(GetNativeBB(succ));
  EndCurrentBlock();
}

void LLVMTFBuilder::VisitParameter(int id, int pid) {
  SetDebugLine(id);
  LValue value = output().parameter(pid + 1);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitLoadParentFramePointer(int id) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, output().parent_fp());
}

void LLVMTFBuilder::VisitIdentity(int id, int value) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(id, GetBuilderImpl(current_bb_)->GetLLVMValue(value));
}

void LLVMTFBuilder::VisitLoadFramePointer(int id) {
  SetDebugLine(id);
  LValue fp = output().fp();
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, fp);
}

void LLVMTFBuilder::VisitDebugBreak(int id) {
  SetDebugLine(id);
  output().buildCall(output().repo().trapIntrinsic());
}

void LLVMTFBuilder::VisitStackPointerGreaterThan(int id, int value) {
  auto impl = GetBuilderImpl(current_bb_);
  LValue stack_pointer =
      output().buildCall(output().repo().stackSaveIntrinsic());
  LValue stack_pointer_int =
      output().buildCast(LLVMPtrToInt, stack_pointer, output().repo().intPtr);
  LValue llvm_value = impl->GetLLVMValue(value);
  LValue cmp = output().buildICmp(LLVMIntUGT, stack_pointer_int, llvm_value);
  impl->SetLLVMValue(id, cmp);
}

void LLVMTFBuilder::VisitTrapIf(int id, int value) {
  SetDebugLine(id);
  auto impl = GetBuilderImpl(current_bb_);
  LValue trap_val = EnsureWord32(impl->GetLLVMValue(value));

  LValue cmp_val =
      output().buildICmp(LLVMIntNE, trap_val, output().repo().int32Zero);
  cmp_val = output().buildCall(output().repo().expectIntrinsic(), cmp_val,
                               output().repo().booleanFalse);
  char buf[256];
  snprintf(buf, 256, "B%d_trap", current_bb_->id());
  LBasicBlock trap_bb = output().appendBasicBlock(buf);
  snprintf(buf, 256, "B%d_trap_continuation", current_bb_->id());
  impl->continuation = output().appendBasicBlock(buf);
  output().buildCondBr(cmp_val, trap_bb, impl->continuation);
  output().positionToBBEnd(trap_bb);
  output().buildCall(output().repo().trapIntrinsic());

  output().buildBr(impl->continuation);
  output().positionToBBEnd(impl->continuation);
}

void LLVMTFBuilder::VisitTrapUnless(int id, int value) {
  SetDebugLine(id);
  auto impl = GetBuilderImpl(current_bb_);
  LValue trap_val = EnsureWord32(impl->GetLLVMValue(value));

  LValue cmp_val =
      output().buildICmp(LLVMIntEQ, trap_val, output().repo().int32Zero);
  cmp_val = output().buildCall(output().repo().expectIntrinsic(), cmp_val,
                               output().repo().booleanFalse);
  char buf[256];
  snprintf(buf, 256, "B%d_trap", current_bb_->id());
  LBasicBlock trap_bb = output().appendBasicBlock(buf);
  snprintf(buf, 256, "B%d_trap_continuation", current_bb_->id());
  impl->continuation = output().appendBasicBlock(buf);
  output().buildCondBr(cmp_val, trap_bb, impl->continuation);
  output().positionToBBEnd(trap_bb);
  output().buildCall(output().repo().trapIntrinsic());

  output().buildBr(impl->continuation);
  output().positionToBBEnd(impl->continuation);
}

void LLVMTFBuilder::VisitInt32Constant(int id, int32_t value) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, output().constInt32(value));
}

void LLVMTFBuilder::VisitInt64Constant(int id, int64_t value) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, output().constInt64(value));
}

void LLVMTFBuilder::VisitRelocatableInt32Constant(int id, int32_t magic,
                                                  int rmode) {
  SetDebugLine(id);
  magic = static_cast<int32_t>(load_constant_recorder_->Register(
      magic, LoadConstantRecorder::kRelocatableInt32Constant, rmode));
  LValue value = output().buildLoadMagic(output().repo().int32, magic);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitFloat64SilenceNaN(int id, int value) {
  SetDebugLine(id);
  LValue llvalue = GetBuilderImpl(current_bb_)->GetLLVMValue(value);
  LValue result =
      output().buildFSub(llvalue, constReal(output().repo().doubleType, 0.0));
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

static LType getMachineRepresentationType(Output& output,
                                          MachineRepresentation rep) {
  LType dstType;
  switch (rep) {
    case MachineRepresentation::kTaggedSigned:
      dstType = output.repo().intPtr;
      break;
    case MachineRepresentation::kTagged:
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
    case MachineRepresentation::kBit:
      dstType = output.repo().boolean;
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
  // For ElementOffsetFromIndex ignores BitcastTaggedToWord.
  if (typeOf(offset) == output.taggedType()) {
    offset = output.buildCast(LLVMPtrToInt, offset, output.repo().intPtr);
  }
  LValue pointer = output.buildGEPWithByteOffset(
      value, offset, pointerType(getMachineRepresentationType(output, rep)));
  return pointer;
}

void LLVMTFBuilder::VisitLoad(int id, MachineRepresentation rep,
                              MachineSemantic semantic, int base, int offset) {
  SetDebugLine(id);
  LValue pointer = buildAccessPointer(
      output(), GetBuilderImpl(current_bb_)->GetLLVMValue(base),
      GetBuilderImpl(current_bb_)->GetLLVMValue(offset), rep);
  LValue value = output().buildLoad(pointer);
  LType pointer_type = typeOf(pointer);
  if ((pointer_type != pointerType(output().taggedType()) &&
       (pointer_type != pointerType(output().repo().doubleType))))
    LLVMSetAlignment(value, 1);
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
        case MachineRepresentation::kTaggedSigned:
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
      break;
    default:
      break;
  }
  if (castType) value = output().buildCast(opcode, value, castType);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitStore(int id, MachineRepresentation rep,
                               compiler::WriteBarrierKind barrier, int base,
                               int offset, int value) {
  SetDebugLine(id);
  LValue pointer = buildAccessPointer(
      output(), GetBuilderImpl(current_bb_)->GetLLVMValue(base),
      GetBuilderImpl(current_bb_)->GetLLVMValue(offset), rep);
  LValue llvm_value = GetBuilderImpl(current_bb_)->GetLLVMValue(value);
  LType value_type = typeOf(llvm_value);
  LType pointer_element_type = getElementType(typeOf(pointer));
  if (pointer_element_type != value_type) {
    LLVMTypeKind pointer_element_kind = LLVMGetTypeKind(pointer_element_type);
    LLVMTypeKind value_kind = LLVMGetTypeKind(value_type);
    if (value_kind == LLVMIntegerTypeKind) {
      if (pointer_element_kind == LLVMPointerTypeKind)
        llvm_value =
            output().buildCast(LLVMIntToPtr, llvm_value, pointer_element_type);
      else if ((pointer_element_kind == LLVMIntegerTypeKind) &&
               (LLVMGetIntTypeWidth(value_type) >
                LLVMGetIntTypeWidth(pointer_element_type)))
        llvm_value =
            output().buildCast(LLVMTrunc, llvm_value, pointer_element_type);
      else if (value_type == output().repo().boolean) {
        llvm_value =
            output().buildCast(LLVMZExt, llvm_value, pointer_element_type);
      } else
        // FIXME: this cast does not follow the sematic hint of llvm_value.
        // Need somewhere to store the sematic of llvm_value for query.
        llvm_value =
            output().buildCast(LLVMSExt, llvm_value, pointer_element_type);
    } else if ((value_type == output().taggedType()) &&
               (pointer_element_kind == LLVMIntegerTypeKind)) {
      LValue val =
          output().buildCast(LLVMPtrToInt, llvm_value, output().repo().intPtr);
      if (LLVMGetIntTypeWidth(output().repo().intPtr) >
          LLVMGetIntTypeWidth(pointer_element_type))
        val = output().buildCast(LLVMTrunc, val, pointer_element_type);
      llvm_value = val;
    } else {
      LLVM_BUILTIN_TRAP;
    }
  }
  LValue val = output().buildStore(llvm_value, pointer);
  LType pointer_type = typeOf(pointer);
  if ((pointer_type != pointerType(output().taggedType()) &&
       (pointer_type != pointerType(output().repo().doubleType))))
    LLVMSetAlignment(val, 1);
  // store should not be recorded, whatever.
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, val);
  if (barrier != compiler::kNoWriteBarrier) {
    StoreBarrierResolver resolver(current_bb_, output(), id,
                                  stack_map_info_map_, state_point_id_next_++);
    resolver.Resolve(GetBuilderImpl(current_bb_)->GetLLVMValue(base), pointer,
                     llvm_value, barrier,
                     [this]() { return CallGetRecordWriteBuiltin(); });
  }
}

void LLVMTFBuilder::VisitUnalignedLoad(int id, MachineRepresentation rep,
                                       int base, int offset) {
  SetDebugLine(id);
  LValue pointer = buildAccessPointer(
      output(), GetBuilderImpl(current_bb_)->GetLLVMValue(base),
      GetBuilderImpl(current_bb_)->GetLLVMValue(offset), rep);
  LValue value = output().buildLoad(pointer);
  LLVMSetAlignment(value, 1);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitUnalignedStore(int id, MachineRepresentation rep,
                                        int base, int offset, int value) {
  SetDebugLine(id);
  LValue pointer = buildAccessPointer(
      output(), GetBuilderImpl(current_bb_)->GetLLVMValue(base),
      GetBuilderImpl(current_bb_)->GetLLVMValue(offset), rep);
  LValue llvm_value = GetBuilderImpl(current_bb_)->GetLLVMValue(value);
  LType value_type = typeOf(llvm_value);
  LType pointer_element_type = getElementType(typeOf(pointer));
  if (pointer_element_type != value_type) {
    LLVMTypeKind element_kind = LLVMGetTypeKind(pointer_element_type);
    if (value_type == output().repo().intPtr) {
      if (element_kind == LLVMPointerTypeKind)
        llvm_value =
            output().buildCast(LLVMIntToPtr, llvm_value, pointer_element_type);
      else if ((pointer_element_type == output().repo().int8) ||
               (pointer_element_type == output().repo().int16))
        llvm_value =
            output().buildCast(LLVMTrunc, llvm_value, pointer_element_type);
      else
        LLVM_BUILTIN_TRAP;
    } else if ((value_type == output().taggedType()) &&
               (element_kind == LLVMIntegerTypeKind)) {
      LValue val =
          output().buildCast(LLVMPtrToInt, llvm_value, output().repo().intPtr);
      if (LLVMGetIntTypeWidth(output().repo().intPtr) >
          LLVMGetIntTypeWidth(pointer_element_type))
        val = output().buildCast(LLVMTrunc, val, pointer_element_type);
      llvm_value = val;
    } else {
      LLVM_BUILTIN_TRAP;
    }
  }
  LValue val = output().buildStore(llvm_value, pointer);
  LLVMSetAlignment(val, 1);
  // store should not be recorded, whatever.
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, val);
}

void LLVMTFBuilder::VisitBitcastWordToTagged(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMIntToPtr,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().taggedType()));
}

void LLVMTFBuilder::VisitBitcastTaggedToWord(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMPtrToInt,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().intPtr));
}

void LLVMTFBuilder::VisitChangeInt32ToFloat64(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(
                  LLVMSIToFP,
                  EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e)),
                  output().repo().doubleType));
}

void LLVMTFBuilder::VisitChangeFloat32ToFloat64(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMFPExt,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().doubleType));
}

void LLVMTFBuilder::VisitChangeUint32ToFloat64(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMUIToFP,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().doubleType));
}

void LLVMTFBuilder::VisitChangeFloat64ToInt32(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMFPToSI,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().int32));
}

void LLVMTFBuilder::VisitChangeFloat64ToUint32(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMFPToUI,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().int32));
}

void LLVMTFBuilder::VisitChangeFloat64ToUint64(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMFPToUI,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().int64));
}

void LLVMTFBuilder::VisitChangeUint32ToUint64(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMZExt,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().int64));
}

void LLVMTFBuilder::VisitChangeInt32ToInt64(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMSExt,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().int64));
}

void LLVMTFBuilder::VisitBitcastInt32ToFloat32(int id, int e) {
  SetDebugLine(id);
  LValue value_storage =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  output().buildStore(GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                      value_storage);
  LValue float_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().refFloat);
  LValue result = output().buildLoad(float_ref);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitBitcastInt64ToFloat64(int id, int e) {
  SetDebugLine(id);
  LValue value_storage =
      output().buildBitCast(output().bitcast_space(), output().repo().ref64);
  output().buildStore(GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                      value_storage);
  LValue double_ref = output().buildBitCast(output().bitcast_space(),
                                            output().repo().refDouble);
  LValue result = output().buildLoad(double_ref);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitBitcastFloat64ToInt64(int id, int e) {
  SetDebugLine(id);
  LValue double_storage = output().buildBitCast(output().bitcast_space(),
                                                output().repo().refDouble);
  output().buildStore(GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                      double_storage);
  LValue int64_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().ref64);
  LValue result = output().buildLoad(int64_ref);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitBitcastFloat32ToInt32(int id, int e) {
  SetDebugLine(id);
  LValue value_storage =
      output().buildBitCast(output().bitcast_space(), output().repo().refFloat);
  output().buildStore(GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                      value_storage);
  LValue word_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  LValue result = output().buildLoad(word_ref);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitTruncateFloat64ToWord32(int id, int e) {
  SetDebugLine(id);
  TruncateFloat64ToWord32Resolver resolver(current_bb_, output(), id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, resolver.Resolve(GetBuilderImpl(current_bb_)->GetLLVMValue(e)));
}

void LLVMTFBuilder::VisitTruncateInt64ToWord32(int id, int e) {
  SetDebugLine(id);
  LValue int64_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue int32_value =
      output().buildCast(LLVMTrunc, int64_value, output().repo().int32);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, int32_value);
}

void LLVMTFBuilder::VisitTruncateFloat64ToFloat32(int id, int e) {
  SetDebugLine(id);
  TruncateFloat64ToWord32Resolver resolver(current_bb_, output(), id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMFPTrunc,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().floatType));
}

void LLVMTFBuilder::VisitTruncateFloat64ToUint32(int id, int e) {
  SetDebugLine(id);
  TruncateFloat64ToWord32Resolver resolver(current_bb_, output(), id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMFPToUI,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().int32));
}

void LLVMTFBuilder::VisitTruncateFloat32ToInt32(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMFPToSI,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().int32));
}

void LLVMTFBuilder::VisitRoundFloat64ToInt32(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMFPToSI,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().int32));
}

void LLVMTFBuilder::VisitFloat64ExtractHighWord32(int id, int e) {
  SetDebugLine(id);
  LValue value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue value_storage = output().buildBitCast(output().bitcast_space(),
                                               output().repo().refDouble);
  output().buildStore(value, value_storage);
  LValue value_bitcast_pointer_high = output().buildGEPWithByteOffset(
      output().bitcast_space(), output().constInt32(sizeof(int32_t)),
      output().repo().ref32);
  LValue value_high = output().buildLoad(value_bitcast_pointer_high);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value_high);
}

void LLVMTFBuilder::VisitFloat64ExtractLowWord32(int id, int e) {
  SetDebugLine(id);
  LValue value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue value_storage = output().buildBitCast(output().bitcast_space(),
                                               output().repo().refDouble);
  output().buildStore(value, value_storage);
  LValue value_bitcast_pointer_low =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  LValue value_low = output().buildLoad(value_bitcast_pointer_low);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value_low);
}

void LLVMTFBuilder::VisitRoundInt32ToFloat32(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMSIToFP,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().floatType));
}

void LLVMTFBuilder::VisitRoundUint32ToFloat32(int id, int e) {
  SetDebugLine(id);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(
          id, output().buildCast(LLVMUIToFP,
                                 GetBuilderImpl(current_bb_)->GetLLVMValue(e),
                                 output().repo().floatType));
}

void LLVMTFBuilder::VisitInt32Add(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildNSWAdd(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt64Add(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildNSWAdd(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt32AddWithOverflow(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildCall(
      output().repo().addWithOverflow32Intrinsic(), e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt32Sub(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildNSWSub(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt64Sub(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildNSWSub(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt32SubWithOverflow(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildCall(
      output().repo().subWithOverflow32Intrinsic(), e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt32Mul(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildMul(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt64Mul(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildMul(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt32Div(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
#if 0
  LValue result = output().buildSDiv(e1_value, e2_value);
#else
  LValue e1_double =
      output().buildCast(LLVMSIToFP, e1_value, output().repo().doubleType);
  LValue e2_double =
      output().buildCast(LLVMSIToFP, e2_value, output().repo().doubleType);
  LValue result_double = output().buildFDiv(e1_double, e2_double);
  LValue result =
      output().buildCast(LLVMFPToSI, result_double, output().repo().int32);
#endif
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitUint32Mod(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
#if 0
  LValue result = output().buildSRem(e1_value, e2_value);
#else
  LValue e1_double =
      output().buildCast(LLVMUIToFP, e1_value, output().repo().doubleType);
  LValue e2_double =
      output().buildCast(LLVMUIToFP, e2_value, output().repo().doubleType);
  LValue div_double = output().buildFDiv(e1_double, e2_double);
  LValue div =
      output().buildCast(LLVMFPToUI, div_double, output().repo().int32);
  LValue mul = output().buildMul(div, e2_value);
  LValue result = output().buildSub(e1_value, mul);
#endif
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitUint32Div(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
#if 0
  LValue result = output().buildSDiv(e1_value, e2_value);
#else
  LValue e1_double =
      output().buildCast(LLVMUIToFP, e1_value, output().repo().doubleType);
  LValue e2_double =
      output().buildCast(LLVMUIToFP, e2_value, output().repo().doubleType);
  LValue result_double = output().buildFDiv(e1_double, e2_double);
  LValue result =
      output().buildCast(LLVMFPToUI, result_double, output().repo().int32);
#endif
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt32Mod(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
#if 0
  LValue result = output().buildSRem(e1_value, e2_value);
#else
  LValue e1_double =
      output().buildCast(LLVMSIToFP, e1_value, output().repo().doubleType);
  LValue e2_double =
      output().buildCast(LLVMSIToFP, e2_value, output().repo().doubleType);
  LValue div_double = output().buildFDiv(e1_double, e2_double);
  LValue div =
      output().buildCast(LLVMFPToSI, div_double, output().repo().int32);
  LValue mul = output().buildMul(div, e2_value);
  LValue result = output().buildSub(e1_value, mul);
#endif
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64InsertLowWord32(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue float64_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue word32_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue float64_storage = output().buildBitCast(
      output().bitcast_space(), pointerType(typeOf(float64_value)));
  output().buildStore(float64_value, float64_storage);
  LValue word32_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  output().buildStore(word32_value, word32_ref);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(id, output().buildLoad(float64_storage));
}

void LLVMTFBuilder::VisitFloat64InsertHighWord32(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue float64_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue word32_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue float64_storage = output().buildBitCast(
      output().bitcast_space(), pointerType(typeOf(float64_value)));
  output().buildStore(float64_value, float64_storage);
  LValue word32_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  word32_ref = output().buildGEP(word32_ref, output().constInt32(1));
  output().buildStore(word32_value, word32_ref);
  GetBuilderImpl(current_bb_)
      ->SetLLVMValue(id, output().buildLoad(float64_storage));
}

void LLVMTFBuilder::VisitInt32MulWithOverflow(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildCall(
      output().repo().mulWithOverflow32Intrinsic(), e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Shl(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildShl(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Xor(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildXor(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Ror(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue right_part = output().buildShr(e1_value, e2_value);
  LValue left_shift_value =
      output().buildSub(output().constInt32(32), e2_value);
  LValue left_part = output().buildShl(e1_value, left_shift_value);
  LValue result = output().buildOr(left_part, right_part);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Shr(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildShr(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Sar(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildSar(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Mul(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildMul(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32And(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildAnd(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Or(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildOr(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Equal(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntEQ, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord64Equal(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntEQ, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord32Clz(int id, int e) {
  SetDebugLine(id);
  LValue e_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e));
  LValue result = output().buildCall(output().repo().ctlz32Intrinsic(), e_value,
                                     output().repo().booleanTrue);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitWord64Clz(int id, int e) {
  SetDebugLine(id);
  LValue e_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e));
  LValue result = output().buildCall(output().repo().ctlz64Intrinsic(), e_value,
                                     output().repo().booleanTrue);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt32LessThanOrEqual(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntSLE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitUint32LessThanOrEqual(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntULE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitUint32LessThan(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntULT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt32LessThan(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntSLT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

#define DEFINE_WORD64_BINOP(name, llvm_op)                           \
  void LLVMTFBuilder::VisitWord64##name(int id, int e1, int e2) {    \
    SetDebugLine(id);                                                \
    LValue e1_value =                                                \
        EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1)); \
    LValue e2_value =                                                \
        EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2)); \
    LValue result = output().build##llvm_op(e1_value, e2_value);     \
    GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);           \
  }

DEFINE_WORD64_BINOP(Shl, Shl)
DEFINE_WORD64_BINOP(Shr, Shr)
DEFINE_WORD64_BINOP(Sar, Sar)
DEFINE_WORD64_BINOP(And, And)
DEFINE_WORD64_BINOP(Or, Or)
DEFINE_WORD64_BINOP(Xor, Xor)

void LLVMTFBuilder::VisitInt64LessThanOrEqual(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntSLE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitUint64LessThanOrEqual(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntULE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitUint64LessThan(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntULT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitInt64LessThan(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e1));
  LValue e2_value = EnsureWord64(GetBuilderImpl(current_bb_)->GetLLVMValue(e2));
  LValue result = output().buildICmp(LLVMIntSLT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitBranch(int id, int cmp, int btrue, int bfalse,
                                BranchHint hint) {
  SetDebugLine(id);
  BasicBlock* bbTrue = basic_block_manager().ensureBB(btrue);
  BasicBlock* bbFalse = basic_block_manager().ensureBB(bfalse);
  EnsureNativeBB(bbTrue, output());
  EnsureNativeBB(bbFalse, output());
  int expected_value = -1;
  if (hint != BranchHint::kNone) {
    switch (hint) {
      case BranchHint::kTrue:
        expected_value = 1;
        break;
      case BranchHint::kFalse:
        expected_value = 0;
        break;
      default:
        EMASSERT(false);
        break;
    }
  } else if (bbTrue->is_deferred()) {
    if (!bbFalse->is_deferred()) expected_value = 0;
  } else if (bbFalse->is_deferred()) {
    expected_value = 1;
  }
  LValue cmp_val = GetBuilderImpl(current_bb_)->GetLLVMValue(cmp);
  cmp_val = EnsureBoolean(cmp_val);
  if (expected_value != -1) {
    cmp_val = output().buildCall(output().repo().expectIntrinsic(), cmp_val,
                                 expected_value ? output().repo().booleanTrue
                                                : output().repo().booleanFalse);
  }
  output().buildCondBr(cmp_val, GetNativeBB(bbTrue), GetNativeBB(bbFalse));
  EndCurrentBlock();
}

void LLVMTFBuilder::VisitSwitch(int id, int val,
                                const OperandsVector& successors) {
  SetDebugLine(id);
  // Last successor must be Default.
  BasicBlock* default_block = basic_block_manager().ensureBB(successors.back());
  LValue cmp_val = GetBuilderImpl(current_bb_)->GetLLVMValue(val);
  EnsureNativeBB(default_block, output());
  output().buildSwitch(cmp_val, GetNativeBB(default_block),
                       successors.size() - 1);
  EndCurrentBlock();
}

void LLVMTFBuilder::VisitIfValue(int id, int val) {
  SetDebugLine(id);
  BasicBlock* pred = current_bb_->predecessors().front();
  EMASSERT(IsBBEndedToBuild(pred));
  LValue switch_val =
      LLVMGetBasicBlockTerminator(GetNativeBBContinuation(pred));
  LLVMAddCase(switch_val, output().constInt32(val), GetNativeBB(current_bb_));
}

void LLVMTFBuilder::VisitIfDefault(int id) { SetDebugLine(id); }

void LLVMTFBuilder::VisitIfException(int id) {
  SetDebugLine(id);
  LValue exception =
      output().buildCall(output().repo().gcExceptionIntrinsic(),
                         GetBuilderImpl(current_bb_)->landing_pad);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, exception);
}

void LLVMTFBuilder::VisitAbortCSAAssert(int id) {
  SetDebugLine(id);
  output().buildCall(output().repo().trapIntrinsic());
}

void LLVMTFBuilder::VisitHeapConstant(int id, uintptr_t magic) {
  SetDebugLine(id);
  magic = load_constant_recorder_->Register(
      magic, LoadConstantRecorder::kHeapConstant);
  LValue value = output().buildLoadMagic(output().taggedType(), magic);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitExternalConstant(int id, uintptr_t magic) {
  SetDebugLine(id);
  magic = load_constant_recorder_->Register(
      magic, LoadConstantRecorder::kExternalReference);
  LValue value = output().buildLoadMagic(output().repo().ref8, magic);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitPhi(int id, MachineRepresentation rep,
                             const OperandsVector& operands) {
  SetDebugLine(id);
  LType phi_type = getMachineRepresentationType(output(), rep);
  LValue phi = output().buildPhi(phi_type);
  auto operands_iterator = operands.cbegin();
  bool should_add_to_tf_phi_worklist = false;
  for (BasicBlock* pred : current_bb_->predecessors()) {
    if (IsBBStartedToBuild(pred)) {
      LValue value =
          EnsurePhiInputAndPosition(pred, (*operands_iterator), phi_type);
      LBasicBlock llvbb_ = GetNativeBBContinuation(pred);
      addIncoming(phi, &value, &llvbb_, 1);
    } else {
      should_add_to_tf_phi_worklist = true;
      LLVMTFBuilderBasicBlockImpl* impl = GetBuilderImpl(current_bb_);
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
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, phi);
}

void LLVMTFBuilder::VisitCall(int id, CallMode mode,
                              const CallDescriptor& call_desc,
                              const OperandsVector& operands) {
  SetDebugLine(id);
  DoCall(id, mode, call_desc, operands, false);
}

void LLVMTFBuilder::VisitTailCall(int id, CallMode mode,
                                  const CallDescriptor& call_desc,
                                  const OperandsVector& operands) {
  SetDebugLine(id);
  DoTailCall(id, mode, call_desc, operands);
}

void LLVMTFBuilder::VisitInvoke(int id, CallMode mode,
                                const CallDescriptor& call_desc,
                                const OperandsVector& operands, int then,
                                int exception) {
  SetDebugLine(id);
  BasicBlock* then_bb = basic_block_manager().ensureBB(then);
  BasicBlock* exception_bb = basic_block_manager().ensureBB(exception);
  EnsureNativeBB(then_bb, output());
  EnsureNativeBB(exception_bb, output());
  InvokeResolver resolver(current_bb_, output(), id, stack_map_info_map_,
                          state_point_id_next_++, then_bb, exception_bb);
  resolver.Resolve(mode, call_desc, operands);
}

void LLVMTFBuilder::VisitCallWithCallerSavedRegisters(
    int id, const OperandsVector& operands, bool save_fp) {
  SetDebugLine(id);
  std::vector<LType> types;
  std::vector<LValue> values;
  auto it = operands.begin();
  auto impl = GetBuilderImpl(current_bb_);
  LValue function = impl->GetLLVMValue(*(it++));
  LLVMTypeKind kind = LLVMGetTypeKind(typeOf(function));
  if (kind == LLVMIntegerTypeKind) {
    function = output().buildCast(LLVMIntToPtr, function, output().repo().ref8);
  }
  for (; it != operands.end(); ++it) {
    LValue val = impl->GetLLVMValue(*it);
    LType val_type = typeOf(val);
    types.push_back(val_type);
    values.push_back(val);
  }
  LType function_type = functionType(output().repo().intPtr, types.data(),
                                     types.size(), NotVariadic);
  LValue result = output().buildCall(
      output().buildBitCast(function, pointerType(function_type)),
      values.data(), values.size());
  impl->SetLLVMValue(id, result);
  // Only enable when the function itself is v8sbcc.
  if (save_fp && !output().is_v8cc()) {
    static const char kSaveFp[] = "save-fp";
    LLVMAttributeRef attr =
        output().createStringAttr(kSaveFp, sizeof(kSaveFp) - 1, nullptr, 0);
    LLVMAddCallSiteAttribute(result, ~0U, attr);
  }
}

void LLVMTFBuilder::VisitRoot(int id, RootIndex index) {
  SetDebugLine(id);
  LValue offset = output().buildGEPWithByteOffset(
      output().root(),
      output().constInt32(
          TurboAssemblerBase::RootRegisterOffsetForRootIndex(index)),
      pointerType(output().taggedType()));
  LValue value = output().buildInvariantLoad(offset);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitRootRelative(int id, int offset, bool tagged) {
  SetDebugLine(id);
  LType type =
      pointerType(tagged ? output().taggedType() : output().repo().ref8);
  LValue offset_value = output().buildGEPWithByteOffset(
      output().root(), output().constInt32(offset), type);
  LValue value = output().buildInvariantLoad(offset_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitRootOffset(int id, int offset) {
  SetDebugLine(id);
  LType type = output().repo().ref8;
  LValue offset_value = output().buildGEPWithByteOffset(
      output().root(), output().constInt32(offset), type);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, offset_value);
}

void LLVMTFBuilder::VisitLoadFromConstantTable(int id, int constant_index) {
  SetDebugLine(id);
  LValue constant_table_offset = output().buildGEPWithByteOffset(
      output().root(),
      output().constInt32(TurboAssemblerBase::RootRegisterOffsetForRootIndex(
          RootIndex::kBuiltinsConstantsTable)),
      pointerType(output().taggedType()));
  LValue constant_table = output().buildLoad(constant_table_offset);
  LValue offset = output().buildGEPWithByteOffset(
      constant_table,
      output().constInt32(FixedArray::kHeaderSize +
                          constant_index * kPointerSize - kHeapObjectTag),
      pointerType(output().taggedType()));

  GetBuilderImpl(current_bb_)->SetLLVMValue(id, output().buildLoad(offset));
}

void LLVMTFBuilder::VisitCodeForCall(int id, uintptr_t magic, bool relative) {
  SetDebugLine(id);
  ValueDesc value;
  if (relative) {
    value.type = ValueType::RelativeCallTarget;
    value.relative_call_target = magic;
  } else {
    magic = load_constant_recorder_->Register(
        magic, LoadConstantRecorder::kCodeConstant);
    LValue llvm_value = output().buildLoadMagic(output().repo().ref8, magic);
    value.type = ValueType::LLVMValue;
    value.llvm_value = llvm_value;
  }
  GetBuilderImpl(current_bb_)->SetValue(id, value);
}

void LLVMTFBuilder::VisitSmiConstant(int id, uintptr_t smi_value) {
  SetDebugLine(id);
  LValue value = output().constTagged(smi_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitFloat64Sqrt(int id, int e) {
  SetDebugLine(id);
  LValue e_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue result =
      output().buildCall(output().repo().doubleSqrtIntrinsic(), e_value);

  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat32Sqrt(int id, int e) {
  SetDebugLine(id);
  LValue e_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue result =
      output().buildCall(output().repo().floatSqrtIntrinsic(), e_value);

  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64Constant(int id, double float_value) {
  SetDebugLine(id);
  LValue value = constReal(output().repo().doubleType, float_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitFloat32Constant(int id, double float_value) {
  SetDebugLine(id);
  LValue value = constReal(output().repo().floatType, float_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitProjection(int id, int e, int index) {
  SetDebugLine(id);
  LValue projection = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue value = output().buildExtractValue(projection, index);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitFloat64Add(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFAdd(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64Sub(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFSub(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64Mul(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFMul(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64Div(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFDiv(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64Mod(int id, int e1, int e2) {
  SetDebugLine(id);
  LType double_type = output().repo().doubleType;
  LType function_type = functionType(double_type, double_type, double_type);
  LValue function = CallGetModTwoDoubleFunction();
  function = output().buildBitCast(function, pointerType(function_type));
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildCall(function, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64LessThan(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFCmp(LLVMRealOLT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64LessThanOrEqual(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFCmp(LLVMRealOLE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64Equal(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFCmp(LLVMRealOEQ, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat64Neg(int id, int e) {
  SetDebugLine(id);
  LValue e_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue value = output().buildFNeg(e_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitFloat64Abs(int id, int e) {
  SetDebugLine(id);
  LValue e_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue value =
      output().buildCall(output().repo().doubleAbsIntrinsic(), e_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitFloat32Abs(int id, int e) {
  SetDebugLine(id);
  LValue e_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue value =
      output().buildCall(output().repo().floatAbsIntrinsic(), e_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitFloat32Equal(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFCmp(LLVMRealOEQ, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat32LessThan(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFCmp(LLVMRealOLT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat32LessThanOrEqual(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFCmp(LLVMRealOLE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat32Add(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFAdd(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat32Sub(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFSub(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat32Mul(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFMul(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat32Div(int id, int e1, int e2) {
  SetDebugLine(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  LValue result = output().buildFDiv(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, result);
}

void LLVMTFBuilder::VisitFloat32Neg(int id, int e) {
  SetDebugLine(id);
  LValue e_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e);
  LValue value = output().buildFNeg(e_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

void LLVMTFBuilder::VisitInt32PairAdd(int id, int e0, int e1, int e2, int e3) {
  SetDebugLine(id);
  LValue n0 = BuildInt64FromPair(e0, e1);
  LValue n1 = BuildInt64FromPair(e2, e3);
  LValue result = output().buildNSWAdd(n0, n1);
  SetInt32PairFromInt64(id, result);
}

void LLVMTFBuilder::VisitInt32PairSub(int id, int e0, int e1, int e2, int e3) {
  SetDebugLine(id);
  LValue n0 = BuildInt64FromPair(e0, e1);
  LValue n1 = BuildInt64FromPair(e2, e3);
  LValue result = output().buildNSWSub(n0, n1);
  SetInt32PairFromInt64(id, result);
}

void LLVMTFBuilder::VisitInt32PairMul(int id, int e0, int e1, int e2, int e3) {
  SetDebugLine(id);
  LValue n0 = BuildInt64FromPair(e0, e1);
  LValue n1 = BuildInt64FromPair(e2, e3);
  LValue result = output().buildMul(n0, n1);
  SetInt32PairFromInt64(id, result);
}

void LLVMTFBuilder::VisitWord32PairShl(int id, int e0, int e1, int e2) {
  SetDebugLine(id);
  LValue n0 = BuildInt64FromPair(e0, e1);
  LValue n1 = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  n1 = output().buildCast(LLVMZExt, n1, output().repo().int64);
  LValue result = output().buildShl(n0, n1);
  SetInt32PairFromInt64(id, result);
}

void LLVMTFBuilder::VisitWord32PairShr(int id, int e0, int e1, int e2) {
  SetDebugLine(id);
  LValue n0 = BuildInt64FromPair(e0, e1);
  LValue n1 = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  n1 = output().buildCast(LLVMZExt, n1, output().repo().int64);
  LValue result = output().buildShr(n0, n1);
  SetInt32PairFromInt64(id, result);
}

void LLVMTFBuilder::VisitWord32PairSar(int id, int e0, int e1, int e2) {
  SetDebugLine(id);
  LValue n0 = BuildInt64FromPair(e0, e1);
  LValue n1 = GetBuilderImpl(current_bb_)->GetLLVMValue(e2);
  n1 = output().buildCast(LLVMZExt, n1, output().repo().int64);
  LValue result = output().buildSar(n0, n1);
  SetInt32PairFromInt64(id, result);
}

void LLVMTFBuilder::VisitReturn(int id, int pop_count,
                                const OperandsVector& operands) {
  SetDebugLine(id);
  int instructions_count = 2;
  EMASSERT(operands.size() <= 2);
  LValue undefined_value = LLVMGetUndef(output().repo().intPtr);
  LValue return_values[2] = {undefined_value, undefined_value};
  for (size_t i = 0; i < operands.size(); ++i) {
    return_values[i] = GetBuilderImpl(current_bb_)->GetLLVMValue(operands[i]);
  }
  LValue pop_count_value = GetBuilderImpl(current_bb_)->GetLLVMValue(pop_count);
  std::unique_ptr<StackMapInfo> info(new ReturnInfo());
  ReturnInfo* rinfo = static_cast<ReturnInfo*>(info.get());
  if (LLVMIsConstant(pop_count_value)) {
    int pop_count_constant = LLVMConstIntGetZExtValue(pop_count_value) +
                             output().stack_parameter_count();
    rinfo->set_pop_count_is_constant(true);
    rinfo->set_constant(pop_count_constant);
    pop_count_value = undefined_value;
    if (pop_count_constant == 0) instructions_count = 1;
  }
  int patchid = state_point_id_next_++;
  LValue call = output().buildCall(
      output().repo().patchpointVoidIntrinsic(), output().constInt64(patchid),
      output().constInt32(instructions_count * 4),
      constNull(output().repo().ref8), output().constInt32(3), return_values[0],
      return_values[1], pop_count_value);
  LLVMSetInstructionCallConv(call, LLVMV8CallConv);
  output().buildReturnForTailCall();
  stack_map_info_map_->emplace(patchid, std::move(info));
}

LValue LLVMTFBuilder::CallGetRecordWriteBuiltin() {
  if (!get_record_write_function_) {
    get_record_write_function_ = output().addFunction(
        "GetRecordWrite", functionType(output().taggedType(),
                                       pointerType(output().taggedType())));
    LLVMSetLinkage(get_record_write_function_, LLVMInternalLinkage);
  }
  return output().buildCall(get_record_write_function_, output().root());
}

LValue LLVMTFBuilder::CallGetModTwoDoubleFunction() {
  if (!get_mod_two_double_function_) {
    get_mod_two_double_function_ = output().addFunction(
        "GetModTwoDouble",
        functionType(output().repo().ref8, pointerType(output().taggedType())));
    LLVMSetLinkage(get_mod_two_double_function_, LLVMInternalLinkage);
  }
  return output().buildCall(get_mod_two_double_function_, output().root());
}

void LLVMTFBuilder::BuildFunctionUtil(LValue func,
                                      std::function<void(LValue)> f) {
  if (func) {
    LBasicBlock bb = output().appendBasicBlock(func);
    output().positionToBBEnd(bb);
    f(LLVMGetParam(func, 0));
  }
}

void LLVMTFBuilder::BuildGetRecordWriteBuiltin(
    BuiltinFunctionClient* builtin_function_client) {
  BuildFunctionUtil(
      get_record_write_function_, [this, builtin_function_client](LValue root) {
        builtin_function_client->BuildGetRecordWriteFunction(output(), root);
      });
}

void LLVMTFBuilder::BuildGetModTwoDoubleFunction(
    BuiltinFunctionClient* builtin_function_client) {
  BuildFunctionUtil(get_mod_two_double_function_,
                    [this, builtin_function_client](LValue root) {
                      builtin_function_client->BuildGetModTwoDoubleFunction(
                          output(), root);
                    });
}

void LLVMTFBuilder::VisitStackSlot(int id, int size, int alignment) {
  LType array_type = arrayType(output().repo().int8, size);
  output().positionToBBEnd(output().prologue());
  LValue value = output().buildAlloca(array_type);
  output().positionToBBEnd(GetNativeBBContinuation(current_bb_));
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, value);
}

LValue LLVMTFBuilder::BuildInt64FromPair(int e0, int e1) {
  LValue e0_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e0);
  LValue e1_value = GetBuilderImpl(current_bb_)->GetLLVMValue(e1);
  e0_value = output().buildCast(LLVMZExt, e0_value, output().repo().int64);
  e1_value = output().buildCast(LLVMZExt, e1_value, output().repo().int64);
  e1_value = output().buildShl(e1_value, output().constInt64(32));
  return output().buildOr(e0_value, e1_value);
}

void LLVMTFBuilder::SetInt32PairFromInt64(int id, LValue n) {
  LValue e0_value = output().buildCast(LLVMTrunc, n, output().repo().int32);
  LValue e1_value = output().buildShr(n, output().constInt64(32));
  e1_value = output().buildCast(LLVMTrunc, e1_value, output().repo().int32);
  LValue ret = LLVMGetUndef(int32_pair_type_);
  ret = output().buildInsertValue(ret, 0, e0_value);
  ret = output().buildInsertValue(ret, 1, e1_value);
  GetBuilderImpl(current_bb_)->SetLLVMValue(id, ret);
}

void LLVMTFBuilder::SetDebugLine(int id) {
  int line_no = id;
  if (line_number_ != -1) line_no = line_number_;
  output().setDebugInfo(line_no, debug_file_name_);
}

void LLVMTFBuilder::SetSourcePosition(int line, const char* file_name) {
  line_number_ = line;
  debug_file_name_ = file_name;
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

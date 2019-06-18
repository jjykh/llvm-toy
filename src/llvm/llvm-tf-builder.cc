// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/llvm-tf-builder.h"
#include <llvm/Support/Compiler.h>
#include <bitset>
#include <sstream>
#include "src/heap/spaces.h"
#include "src/llvm/basic-block-manager.h"
#include "src/llvm/basic-block.h"
#include "src/llvm/load-constant-recorder.h"
#include "src/llvm/output.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace tf_llvm {
namespace {
// copy from v8.
enum RememberedSetAction { EMIT_REMEMBERED_SET, OMIT_REMEMBERED_SET };
enum SaveFPRegsMode { kDontSaveFPRegs, kSaveFPRegs };

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

struct PredecessorCallInfo {
  std::vector<GCRelocateDesc> gc_relocates;
  LValue call_value = nullptr;
  int call_id = -1;
  int return_count = 0;
  int target_patch_id = 0;
};

struct LLVMTFBuilderBasicBlockImpl {
  std::vector<NotMergedPhiDesc> not_merged_phis;
  std::unordered_map<int, LValue> values_;
  PredecessorCallInfo call_info;

  LBasicBlock native_bb = nullptr;
  LBasicBlock continuation = nullptr;
  LValue landing_pad = nullptr;

  bool started = false;
  bool ended = false;
  bool exception_block = false;

  inline void set_value(int nid, LValue value) { values_[nid] = value; }

  inline LValue value(int nid) {
    auto found = values_.find(nid);
    EMASSERT(found != values_.end());
    return found->second;
  }

  inline std::unordered_map<int, LValue>& values() { return values_; }

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

class CallResolver {
 public:
  CallResolver(BasicBlock* current_bb, Output& output, int id,
               StackMapInfoMap* stack_map_info_map, int patchid);
  virtual ~CallResolver() = default;
  void Resolve(
      bool code, const CallDescriptor& call_desc,
      const OperandsVector& operands,
      const LLVMTFBuilder::NodeRelativeCallTargetMap& node_call_target_map);

 protected:
  virtual LValue EmitCallInstr(LValue function, LValue* operands,
                               size_t operands_count);
  virtual void BuildCall(const CallDescriptor& call_desc);
  virtual void PopulateCallInfo(CallInfo*);
  inline Output& output() { return *output_; }
  inline int id() { return id_; }
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
  void ResolveOperands(
      bool code, const OperandsVector& operands,
      const RegistersForOperands& registers_for_operands,
      const LLVMTFBuilder::NodeRelativeCallTargetMap& node_call_target_map);
  void ResolveCallTarget(
      bool code, int target_id,
      const LLVMTFBuilder::NodeRelativeCallTargetMap& node_call_target_map);
  void UpdateAfterStatePoint(const CallDescriptor& call_desc,
                             LValue statepoint_ret);
  void EmitToSuccessors(std::vector<LValue>&);
  void PopulateToStackMap();
  inline CallInfo::LocationVector&& release_location() {
    return std::move(locations_);
  }
  void SetOperandValue(int reg, LValue value);
  int FindNextReg();

  std::bitset<kV8CCRegisterParameterCount>
      allocatable_register_set_; /* 0 is allocatable */
  std::vector<LValue> operand_values_;
  std::vector<LType> operand_value_types_;
  CallInfo::LocationVector locations_;
  BasicBlock* current_bb_;
  StackMapInfoMap* stack_map_info_map_;
  Output* output_;
  LValue target_;
  int64_t relative_target_;
  int id_;
  int next_reg_;
  int patchid_;
};

class TCCallResolver final : public CallResolver {
 public:
  TCCallResolver(BasicBlock* current_bb, Output& output, int id,
                 StackMapInfoMap* stack_map_info_map, int patchid);
  ~TCCallResolver() = default;

 private:
  void BuildCall(const CallDescriptor& call_desc) override;
  void PopulateCallInfo(CallInfo*) override;
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

class StoreBarrierResolver final : public ContinuationResolver {
 public:
  StoreBarrierResolver(BasicBlock* bb, Output& output, int id,
                       StackMapInfoMap* stack_map_info_map, int patch_point_id);
  void Resolve(LValue base, LValue offset, LValue value,
               WriteBarrierKind barrier_kind, std::function<LValue()> isolate,
               std::function<LValue()> record_write);

 private:
  void CheckPageFlag(LValue base, int flags);
  void CallPatchpoint(LValue base, LValue offset, LValue remembered_set_action,
                      LValue save_fp_mode, std::function<LValue()> isolate,
                      std::function<LValue()> record_write);
  void CheckSmi(LValue value);
  StackMapInfoMap* stack_map_info_map_;
  int id_;
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
    : operand_values_(kV8CCRegisterParameterCount,
                      LLVMGetUndef(output.repo().intPtr)),
      operand_value_types_(kV8CCRegisterParameterCount, output.repo().intPtr),
      current_bb_(current_bb),
      stack_map_info_map_(stack_map_info_map),
      output_(&output),
      target_(nullptr),
      relative_target_(0),
      id_(id),
      next_reg_(0),
      patchid_(patchid) {}

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

void CallResolver::SetOperandValue(int reg, LValue llvm_val) {
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

void CallResolver::Resolve(
    bool code, const CallDescriptor& call_desc, const OperandsVector& operands,
    const LLVMTFBuilder::NodeRelativeCallTargetMap& node_call_target_map) {
  ResolveOperands(code, operands, call_desc.registers_for_operands,
                  node_call_target_map);
  BuildCall(call_desc);
  PopulateToStackMap();
}

void CallResolver::ResolveCallTarget(
    bool code, int target_id,
    const LLVMTFBuilder::NodeRelativeCallTargetMap& node_call_target_map) {
  if (code) {
    auto relative_target_iter = node_call_target_map.find(target_id);
    if (relative_target_iter != node_call_target_map.end()) {
      relative_target_ = relative_target_iter->second;
      return;
    }
    LValue code_value = GetBuilderImpl(current_bb_)->value(target_id);
    if (typeOf(code_value) != output().taggedType()) {
      EMASSERT(typeOf(code_value) == output().repo().ref8);
      target_ = code_value;
    } else {
      target_ = output().buildGEPWithByteOffset(
          code_value, output().constInt32(Code::kHeaderSize - kHeapObjectTag),
          output().repo().ref8);
    }
  } else {
    int addr_value = target_id;
    target_ = GetBuilderImpl(current_bb_)->value(addr_value);
  }
}

void CallResolver::ResolveOperands(
    bool code, const OperandsVector& operands,
    const RegistersForOperands& registers_for_operands,
    const LLVMTFBuilder::NodeRelativeCallTargetMap& node_call_target_map) {
  auto operands_iterator = operands.begin();
  // layout
  // return value | register operands | stack operands | artifact operands
  int target_id = *(operands_iterator++);
  ResolveCallTarget(code, target_id, node_call_target_map);
  // setup register operands
  OperandsVector stack_operands;
  for (int reg : registers_for_operands) {
    EMASSERT(reg < kV8CCRegisterParameterCount);
    if (reg < 0) {
      stack_operands.push_back(*(operands_iterator++));
      continue;
    }
    LValue llvm_val =
        GetBuilderImpl(current_bb_)->value(*(operands_iterator++));
    SetOperandValue(reg, llvm_val);
  }
  // setup callee value.
  if (target_) {
    int target_reg = FindNextReg();
    SetOperandValue(target_reg, target_);
    locations_.push_back(target_reg);
  }

  std::vector<int> allocated_regs;

  for (size_t i = 0; i != stack_operands.size(); ++i) {
    int reg = FindNextReg();
    EMASSERT(reg >= 0);
    allocated_regs.push_back(reg);
  }

  auto reg_iterator = allocated_regs.rbegin();

  for (auto operand : stack_operands) {
    LValue llvm_val = GetBuilderImpl(current_bb_)->value(operand);
    int reg = *(reg_iterator++);
    SetOperandValue(reg, llvm_val);
    locations_.push_back(reg);
  }
}

void CallResolver::BuildCall(const CallDescriptor& call_desc) {
  std::vector<LValue> statepoint_operands;
  LType ret_type = output().repo().taggedType;
  if (call_desc.return_count == 2) {
    ret_type = structType(output().repo().context_, output().taggedType(),
                          output().taggedType());
  }
  LType callee_function_type =
      functionType(ret_type, operand_value_types().data(),
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
  EmitToSuccessors(statepoint_operands);
  LValue statepoint_ret =
      EmitCallInstr(output().getStatePointFunction(callee_type),
                    statepoint_operands.data(), statepoint_operands.size());
  LLVMSetInstructionCallConv(statepoint_ret, LLVMV8CallConv);
  UpdateAfterStatePoint(call_desc, statepoint_ret);
}

void CallResolver::EmitToSuccessors(std::vector<LValue>& statepoint_operands) {
  // value, pos
  std::unordered_map<int, int> position_map;
  auto& values = GetBuilderImpl(current_bb_)->values();
  for (BasicBlock* successor : current_bb_->successors()) {
    auto impl = EnsureImpl(successor);
    auto& successor_liveins = successor->liveins();
    for (int livein : successor_liveins) {
      if (livein == id_) continue;
      auto found = values.find(livein);
      EMASSERT(found != values.end());
      LValue to_gc = found->second;
      if (typeOf(to_gc) != output().taggedType()) continue;
      auto pair = position_map.insert(
          std::make_pair(livein, statepoint_operands.size()));
      if (pair.second) {
        statepoint_operands.emplace_back(to_gc);
      }
      impl->call_info.gc_relocates.emplace_back(livein, pair.first->second);
    }
  }
}

void CallResolver::UpdateAfterStatePoint(const CallDescriptor& call_desc,
                                         LValue statepoint_ret) {
  BasicBlock* successor = current_bb_->successors().front();
  GetBuilderImpl(successor)->call_info.call_id = id_;
  GetBuilderImpl(successor)->call_info.call_value = statepoint_ret;
  GetBuilderImpl(successor)->call_info.return_count = call_desc.return_count;
  if (current_bb_->successors().size() == 2) {
    BasicBlock* exception = current_bb_->successors()[1];
    GetBuilderImpl(exception)->exception_block = true;
    GetBuilderImpl(exception)->call_info.target_patch_id = patchid_;
  }
}

void CallResolver::PopulateToStackMap() {
  // save patch point info
  std::unique_ptr<StackMapInfo> info(
      new CallInfo(std::move(release_location())));
  PopulateCallInfo(static_cast<CallInfo*>(info.get()));
#if defined(UC_3_0)
  stack_map_info_map_->emplace(patchid_, std::move(info));
#else
  stack_map_info_map_->insert(std::make_pair(patchid_, std::move(info)));
#endif
}

void CallResolver::PopulateCallInfo(CallInfo* callinfo) {
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
  return output().buildCall(function, operands, operands_count);
}

InvokeResolver::InvokeResolver(BasicBlock* current_bb, Output& output, int id,
                               StackMapInfoMap* stack_map_info_map, int patchid,
                               BasicBlock* then, BasicBlock* exception)
    : CallResolver(current_bb, output, id, stack_map_info_map, patchid),
      then_bb_(then),
      exception_bb_(exception) {}

LValue InvokeResolver::EmitCallInstr(LValue function, LValue* operands,
                                     size_t operands_count) {
  return output().buildInvoke(function, operands, operands_count,
                              GetNativeBB(then_bb_),
                              GetNativeBB(exception_bb_));
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
                                   WriteBarrierKind barrier_kind,
                                   std::function<LValue()> isolate,
                                   std::function<LValue()> record_write) {
  CreateContination();
  CheckPageFlag(base, MemoryChunk::kPointersFromHereAreInterestingMask);
  if (barrier_kind > kPointerWriteBarrier) {
    CheckSmi(value);
  }
  CheckPageFlag(value, MemoryChunk::kPointersToHereAreInterestingMask);
  RememberedSetAction const remembered_set_action =
      barrier_kind > kMapWriteBarrier ? EMIT_REMEMBERED_SET
                                      : OMIT_REMEMBERED_SET;
  // now v8cc clobbers all fp.
  SaveFPRegsMode const save_fp_mode = kDontSaveFPRegs;
  CallPatchpoint(
      base, offset,
      output().constIntPtr(static_cast<int>(remembered_set_action) << 1),
      output().constIntPtr(static_cast<int>(save_fp_mode) << 1), isolate,
      record_write);
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
#if !defined(FEATURE_SAMPLE_PGO)
  cmp = output().buildCall(output().repo().expectIntrinsic(), cmp,
                           output().repo().booleanTrue);
#endif  // FEATURE_SAMPLE_PGO

  char buf[256];
  snprintf(buf, 256, "B%d_value%d_checkpageflag_%d", current_bb()->id(), id_,
           mask);
  LBasicBlock continuation = output().appendBasicBlock(buf);
  output().buildCondBr(cmp, GetNativeBBContinuation(current_bb()),
                       continuation);
  output().positionToBBEnd(continuation);
}

void StoreBarrierResolver::CallPatchpoint(
    LValue base, LValue offset, LValue remembered_set_action,
    LValue save_fp_mode, std::function<LValue()> get_isolate,
    std::function<LValue()> get_record_write) {
  // blx ip
  // 1 instructions.
  int instructions_count = 1;
  int patchid = patch_point_id_;
  // will not be true again.
  LValue isolate = get_isolate();
  LValue stub = get_record_write();
  LValue stub_entry = output().buildGEPWithByteOffset(
      stub, output().constInt32(Code::kHeaderSize - kHeapObjectTag),
      output().repo().ref8);

  LValue call = output().buildCall(
      output().repo().patchpointVoidIntrinsic(), output().constInt64(patchid),
      output().constInt32(4 * instructions_count),
      constNull(output().repo().ref8), output().constInt32(8), base, offset,
      isolate, remembered_set_action, save_fp_mode,
      LLVMGetUndef(typeOf(output().root())),
      LLVMGetUndef(typeOf(output().fp())), stub_entry);
  LLVMSetInstructionCallConv(call, LLVMV8SBCallConv);
  std::unique_ptr<StackMapInfo> info(
      new StackMapInfo(StackMapInfoType::kStoreBarrier));
#if defined(UC_3_0)
  stack_map_info_map_->emplace(patchid, std::move(info));
#else
  stack_map_info_map_->insert(std::make_pair(patchid, std::move(info)));
#endif
}

void StoreBarrierResolver::CheckSmi(LValue value) {
  LValue value_int =
      output().buildCast(LLVMPtrToInt, value, output().repo().intPtr);
  LValue and_result = output().buildAnd(value_int, output().repo().intPtrOne);
  LValue cmp =
      output().buildICmp(LLVMIntEQ, and_result, output().repo().int32Zero);
#if !defined(FEATURE_SAMPLE_PGO)
  cmp = output().buildCall(output().repo().expectIntrinsic(), cmp,
                           output().repo().booleanFalse);
#endif  // FEATURE_SAMPLE_PGO
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
#if !defined(FEATURE_SAMPLE_PGO)
  cmp_val = output().buildCall(output().repo().expectIntrinsic(), cmp_val,
                               output().repo().booleanFalse);
#endif  // FEATURE_SAMPLE_PGO

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
      get_isolate_function_(nullptr),
      get_record_write_function_(nullptr),
      get_mod_two_double_function_(nullptr),
      state_point_id_next_(0) {}

void LLVMTFBuilder::End(BuiltinFunctionClient* builtin_function_client) {
  EMASSERT(!!current_bb_);
  EndCurrentBlock();
  ProcessPhiWorkList();
  output().positionToBBEnd(output().prologue());
  output().buildBr(GetNativeBB(
      basic_block_manager().findBB(*basic_block_manager().rpo().begin())));
  output().finalizeDebugInfo();
  v8::internal::tf_llvm::ResetImpls<LLVMTFBuilderBasicBlockImpl>(
      basic_block_manager());

  BuildGetIsolateFunction(builtin_function_client);
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
      if (live == GetBuilderImpl(bb)->call_info.call_id) continue;
      LValue value = GetBuilderImpl(pred)->value(live);
      GetBuilderImpl(bb)->set_value(live, value);
    }
    PredecessorCallInfo& callinfo = GetBuilderImpl(bb)->call_info;
    LValue call_value = callinfo.call_value;
    if (GetBuilderImpl(bb)->exception_block) {
      LValue landing_pad = output().buildLandingPad();
      GetBuilderImpl(bb)->landing_pad = landing_pad;
      call_value = landing_pad;
    }
    auto& values = GetBuilderImpl(bb)->values();
    for (auto& gc_relocate : callinfo.gc_relocates) {
      auto found = values.find(gc_relocate.value);
      LValue relocated =
          output().buildCall(output().repo().gcRelocateIntrinsic(), call_value,
                             output().constInt32(gc_relocate.where),
                             output().constInt32(gc_relocate.where));
      found->second = relocated;
    }
    if (callinfo.call_id != -1) {
      LValue intrinsic = output().repo().gcResultIntrinsic();
      if (callinfo.return_count == 2) {
        intrinsic = output().repo().gcResult2Intrinsic();
      }
      LValue ret = output().buildCall(intrinsic, callinfo.call_value);
      GetBuilderImpl(current_bb_)->set_value(callinfo.call_id, ret);
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
    LValue ref_value = GetBuilderImpl(ref_pred)->value(live);
    LType ref_type = typeOf(ref_value);
    if (ref_type != output().taggedType()) {
      // FIXME: Should add EMASSERT that all values are the same.
      GetBuilderImpl(bb)->set_value(live, ref_value);
      continue;
    }
    LValue phi = output().buildPhi(ref_type);
    for (BasicBlock* pred : bb->predecessors()) {
      LValue value = GetBuilderImpl(pred)->value(live);
      LBasicBlock native = GetNativeBBContinuation(pred);
      addIncoming(phi, &value, &native, 1);
    }
    GetBuilderImpl(bb)->set_value(live, phi);
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
    LValue ref_value = GetBuilderImpl(ref_pred)->value(live);
    LType ref_type = typeOf(ref_value);
    if (ref_type != output().taggedType()) {
      GetBuilderImpl(bb)->set_value(live, ref_value);
      continue;
    }
    LValue phi = output().buildPhi(ref_type);
    GetBuilderImpl(bb)->set_value(live, phi);
    for (BasicBlock* pred : bb->predecessors()) {
      if (!IsBBStartedToBuild(pred)) {
        impl->not_merged_phis.emplace_back();
        NotMergedPhiDesc& not_merged_phi = impl->not_merged_phis.back();
        not_merged_phi.phi = phi;
        not_merged_phi.value = live;
        not_merged_phi.pred = pred;
        continue;
      }
      LValue value = GetBuilderImpl(pred)->value(live);
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

void LLVMTFBuilder::DoTailCall(int id, bool code,
                               const CallDescriptor& call_desc,
                               const OperandsVector& operands) {
  DoCall(id, code, call_desc, operands, true);
  output().buildUnreachable();
}

void LLVMTFBuilder::DoCall(int id, bool code, const CallDescriptor& call_desc,
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
  call_resolver->Resolve(code, call_desc, operands, node_call_target_map_);
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

LValue LLVMTFBuilder::EnsurePhiInput(BasicBlock* pred, int index, LType type) {
  LValue val = GetBuilderImpl(pred)->value(index);
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
  output().setLineNumber(id);
  LValue value = output().registerParameter(pid + 1);
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitLoadParentFramePointer(int id) {
  output().setLineNumber(id);
  LValue fp = output().fp();
  if (basic_block_manager().needs_frame())
    GetBuilderImpl(current_bb_)->set_value(id, output().parent_fp());
  else
    GetBuilderImpl(current_bb_)
        ->set_value(id, output().buildBitCast(fp, output().repo().ref8));
}

void LLVMTFBuilder::VisitIdentity(int id, int value) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, GetBuilderImpl(current_bb_)->value(value));
}

void LLVMTFBuilder::VisitLoadFramePointer(int id) {
  output().setLineNumber(id);
  LValue fp = output().fp();
  GetBuilderImpl(current_bb_)->set_value(id, fp);
}

void LLVMTFBuilder::VisitLoadStackPointer(int id) {
  output().setLineNumber(id);
  LValue value = output().buildCall(output().repo().stackSaveIntrinsic());
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitDebugBreak(int id) {
  output().setLineNumber(id);
  char kUdf[] = "udf #0\n";
  char empty[] = "\0";
  output().buildInlineAsm(functionType(output().repo().voidType), kUdf,
                          sizeof(kUdf) - 1, empty, 0, true);
}

void LLVMTFBuilder::VisitInt32Constant(int id, int32_t value) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)->set_value(id, output().constInt32(value));
}

void LLVMTFBuilder::VisitFloat64SilenceNaN(int id, int value) {
  output().setLineNumber(id);
  LValue llvalue = GetBuilderImpl(current_bb_)->value(value);
  LValue result =
      output().buildFSub(llvalue, constReal(output().repo().doubleType, 0.0));
  GetBuilderImpl(current_bb_)->set_value(id, result);
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
  output().setLineNumber(id);
  LValue pointer =
      buildAccessPointer(output(), GetBuilderImpl(current_bb_)->value(base),
                         GetBuilderImpl(current_bb_)->value(offset), rep);
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
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitStore(int id, MachineRepresentation rep,
                               WriteBarrierKind barrier, int base, int offset,
                               int value) {
  output().setLineNumber(id);
  LValue pointer =
      buildAccessPointer(output(), GetBuilderImpl(current_bb_)->value(base),
                         GetBuilderImpl(current_bb_)->value(offset), rep);
  LValue llvm_val = GetBuilderImpl(current_bb_)->value(value);
  LType value_type = typeOf(llvm_val);
  LType pointer_element_type = getElementType(typeOf(pointer));
  if (pointer_element_type != value_type) {
    if (value_type == output().repo().intPtr) {
      LLVMTypeKind kind = LLVMGetTypeKind(pointer_element_type);
      if (kind == LLVMPointerTypeKind)
        llvm_val =
            output().buildCast(LLVMIntToPtr, llvm_val, pointer_element_type);
      else if ((pointer_element_type == output().repo().int8) ||
               (pointer_element_type == output().repo().int16))
        llvm_val =
            output().buildCast(LLVMTrunc, llvm_val, pointer_element_type);
      else
        LLVM_BUILTIN_TRAP;
    } else if ((value_type == output().taggedType()) &&
               (pointer_element_type == output().repo().intPtr)) {
      llvm_val =
          output().buildCast(LLVMPtrToInt, llvm_val, pointer_element_type);
    } else {
      LLVM_BUILTIN_TRAP;
    }
  }
  LValue val = output().buildStore(llvm_val, pointer);
  LType pointer_type = typeOf(pointer);
  if ((pointer_type != pointerType(output().taggedType()) &&
       (pointer_type != pointerType(output().repo().doubleType))))
    LLVMSetAlignment(val, 1);
  // store should not be recorded, whatever.
  GetBuilderImpl(current_bb_)->set_value(id, val);
  if (barrier != kNoWriteBarrier) {
    StoreBarrierResolver resolver(current_bb_, output(), id,
                                  stack_map_info_map_, state_point_id_next_++);
    resolver.Resolve(GetBuilderImpl(current_bb_)->value(base), pointer,
                     llvm_val, barrier,
                     [this]() { return CallGetIsolateFunction(); },
                     [this]() { return CallGetRecordWriteBuiltin(); });
  }
}

void LLVMTFBuilder::VisitBitcastWordToTagged(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMIntToPtr,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().taggedType()));
}

void LLVMTFBuilder::VisitChangeInt32ToFloat64(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMSIToFP,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().doubleType));
}

void LLVMTFBuilder::VisitChangeFloat32ToFloat64(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMFPExt,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().doubleType));
}

void LLVMTFBuilder::VisitChangeUint32ToFloat64(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMUIToFP,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().doubleType));
}

void LLVMTFBuilder::VisitChangeFloat64ToInt32(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMFPToSI,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().int32));
}

void LLVMTFBuilder::VisitChangeFloat64ToUint32(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMFPToUI,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().int32));
}

void LLVMTFBuilder::VisitChangeFloat64ToUint64(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMFPToUI,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().int64));
}

void LLVMTFBuilder::VisitBitcastInt32ToFloat32(int id, int e) {
  output().setLineNumber(id);
  LValue value_storage =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  output().buildStore(GetBuilderImpl(current_bb_)->value(e), value_storage);
  LValue float_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().refFloat);
  LValue result = output().buildLoad(float_ref);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitBitcastFloat32ToInt32(int id, int e) {
  output().setLineNumber(id);
  LValue value_storage =
      output().buildBitCast(output().bitcast_space(), output().repo().refFloat);
  output().buildStore(GetBuilderImpl(current_bb_)->value(e), value_storage);
  LValue word_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  LValue result = output().buildLoad(word_ref);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitTruncateFloat64ToWord32(int id, int e) {
  output().setLineNumber(id);
  TruncateFloat64ToWord32Resolver resolver(current_bb_, output(), id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, resolver.Resolve(GetBuilderImpl(current_bb_)->value(e)));
}

void LLVMTFBuilder::VisitTruncateFloat64ToFloat32(int id, int e) {
  output().setLineNumber(id);
  TruncateFloat64ToWord32Resolver resolver(current_bb_, output(), id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMFPTrunc,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().floatType));
}

void LLVMTFBuilder::VisitRoundFloat64ToInt32(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMFPToSI,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().int32));
}

void LLVMTFBuilder::VisitFloat64ExtractHighWord32(int id, int e) {
  output().setLineNumber(id);
  LValue value = GetBuilderImpl(current_bb_)->value(e);
  LValue value_storage = output().buildBitCast(output().bitcast_space(),
                                               output().repo().refDouble);
  output().buildStore(value, value_storage);
  LValue value_bitcast_pointer_high = output().buildGEPWithByteOffset(
      output().bitcast_space(), output().constInt32(sizeof(int32_t)),
      output().repo().ref32);
  LValue value_high = output().buildLoad(value_bitcast_pointer_high);
  GetBuilderImpl(current_bb_)->set_value(id, value_high);
}

void LLVMTFBuilder::VisitFloat64ExtractLowWord32(int id, int e) {
  output().setLineNumber(id);
  LValue value = GetBuilderImpl(current_bb_)->value(e);
  LValue value_storage = output().buildBitCast(output().bitcast_space(),
                                               output().repo().refDouble);
  output().buildStore(value, value_storage);
  LValue value_bitcast_pointer_low =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  LValue value_low = output().buildLoad(value_bitcast_pointer_low);
  GetBuilderImpl(current_bb_)->set_value(id, value_low);
}

void LLVMTFBuilder::VisitRoundInt32ToFloat32(int id, int e) {
  output().setLineNumber(id);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildCast(LLVMSIToFP,
                                         GetBuilderImpl(current_bb_)->value(e),
                                         output().repo().floatType));
}

void LLVMTFBuilder::VisitInt32Add(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildNSWAdd(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32AddWithOverflow(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildCall(
      output().repo().addWithOverflow32Intrinsic(), e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32Sub(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildNSWSub(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32SubWithOverflow(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildCall(
      output().repo().subWithOverflow32Intrinsic(), e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32Mul(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildNSWMul(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32Div(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
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
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32Mod(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
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
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64InsertLowWord32(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue float64_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue word32_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue float64_storage = output().buildBitCast(
      output().bitcast_space(), pointerType(typeOf(float64_value)));
  output().buildStore(float64_value, float64_storage);
  LValue word32_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  output().buildStore(word32_value, word32_ref);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildLoad(float64_storage));
}

void LLVMTFBuilder::VisitFloat64InsertHighWord32(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue float64_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue word32_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue float64_storage = output().buildBitCast(
      output().bitcast_space(), pointerType(typeOf(float64_value)));
  output().buildStore(float64_value, float64_storage);
  LValue word32_ref =
      output().buildBitCast(output().bitcast_space(), output().repo().ref32);
  word32_ref = output().buildGEP(word32_ref, output().constInt32(1));
  output().buildStore(word32_value, word32_ref);
  GetBuilderImpl(current_bb_)
      ->set_value(id, output().buildLoad(float64_storage));
}

void LLVMTFBuilder::VisitInt32MulWithOverflow(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildCall(
      output().repo().mulWithOverflow32Intrinsic(), e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Shl(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildShl(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Xor(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildXor(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Shr(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildShr(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Sar(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildSar(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Mul(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildMul(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32And(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildAnd(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Or(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildOr(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Equal(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildICmp(LLVMIntEQ, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitWord32Clz(int id, int e) {
  output().setLineNumber(id);
  LValue e_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e));
  LValue result = output().buildCall(output().repo().ctlz32Intrinsic(), e_value,
                                     output().repo().booleanTrue);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32LessThanOrEqual(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildICmp(LLVMIntSLE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitUint32LessThanOrEqual(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildICmp(LLVMIntULE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitUint32LessThan(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildICmp(LLVMIntULT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitInt32LessThan(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e1));
  LValue e2_value = EnsureWord32(GetBuilderImpl(current_bb_)->value(e2));
  LValue result = output().buildICmp(LLVMIntSLT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitBranch(int id, int cmp, int btrue, int bfalse) {
  output().setLineNumber(id);
  BasicBlock* bbTrue = basic_block_manager().ensureBB(btrue);
  BasicBlock* bbFalse = basic_block_manager().ensureBB(bfalse);
  EnsureNativeBB(bbTrue, output());
  EnsureNativeBB(bbFalse, output());
#if !defined(FEATURE_SAMPLE_PGO)
  int expected_value = -1;
  if (bbTrue->is_deferred()) {
    if (!bbFalse->is_deferred()) expected_value = 0;
  } else if (bbFalse->is_deferred()) {
    expected_value = 1;
  }
#endif  // FEATURE_SAMPLE_PGO
  LValue cmp_val = GetBuilderImpl(current_bb_)->value(cmp);
  if (typeOf(cmp_val) == output().repo().intPtr) {
    // need to trunc before continue
    cmp_val = output().buildCast(LLVMTrunc, cmp_val, output().repo().boolean);
  }
#if !defined(FEATURE_SAMPLE_PGO)
  if (expected_value != -1) {
    cmp_val = output().buildCall(output().repo().expectIntrinsic(), cmp_val,
                                 expected_value ? output().repo().booleanTrue
                                                : output().repo().booleanFalse);
  }
#endif  // FEATURE_SAMPLE_PGO
  output().buildCondBr(cmp_val, GetNativeBB(bbTrue), GetNativeBB(bbFalse));
  EndCurrentBlock();
}

void LLVMTFBuilder::VisitSwitch(int id, int val,
                                const OperandsVector& successors) {
  output().setLineNumber(id);
  // Last successor must be Default.
  BasicBlock* default_block = basic_block_manager().ensureBB(successors.back());
  LValue cmp_val = GetBuilderImpl(current_bb_)->value(val);
  EnsureNativeBB(default_block, output());
  output().buildSwitch(cmp_val, GetNativeBB(default_block),
                       successors.size() - 1);
  EndCurrentBlock();
}

void LLVMTFBuilder::VisitIfValue(int id, int val) {
  output().setLineNumber(id);
  BasicBlock* pred = current_bb_->predecessors().front();
  EMASSERT(IsBBEndedToBuild(pred));
  LValue switch_val =
      LLVMGetBasicBlockTerminator(GetNativeBBContinuation(pred));
  LLVMAddCase(switch_val, output().constInt32(val), GetNativeBB(current_bb_));
}

void LLVMTFBuilder::VisitIfDefault(int id) { output().setLineNumber(id); }

void LLVMTFBuilder::VisitIfException(int id) {
  output().setLineNumber(id);
#if 0
  std::vector<LValue> statepoint_operands;
  LType ret_type = output().repo().taggedType;
  LType callee_function_type = functionType(ret_type);
  LType callee_type = pointerType(callee_function_type);
  int patchid = state_point_id_next_++;
  statepoint_operands.push_back(output().constInt64(patchid));
  statepoint_operands.push_back(output().constInt32(4));
  statepoint_operands.push_back(constNull(callee_type));
  statepoint_operands.push_back(output().constInt32(0));  // # call params
  statepoint_operands.push_back(output().constInt32(0));  // flags
  statepoint_operands.push_back(output().constInt32(0));  // # transition args
  statepoint_operands.push_back(output().constInt32(0));  // # deopt arguments
  LValue result = output().buildCall(
      output().getStatePointFunction(callee_type), statepoint_operands.data(),
      statepoint_operands.size());
  LLVMSetInstructionCallConv(result, LLVMV8CallConv);
  LValue exception =
      output().buildCall(output().repo().gcResultIntrinsic(), result);
#endif
  char kEmpty[] = "\0";
  char kConstraint[] = "={r0}";
  LValue exception =
      output().buildInlineAsm(functionType(output().taggedType()), kEmpty, 0,
                              kConstraint, sizeof(kConstraint) - 1, true);
  GetBuilderImpl(current_bb_)->set_value(id, exception);
}

void LLVMTFBuilder::VisitHeapConstant(int id, int64_t magic) {
  output().setLineNumber(id);
  LValue value = output().buildLoadMagic(output().taggedType(), magic);
  GetBuilderImpl(current_bb_)->set_value(id, value);
  load_constant_recorder_->Register(magic, LoadConstantRecorder::kHeapConstant);
}

void LLVMTFBuilder::VisitExternalConstant(int id, int64_t magic) {
  output().setLineNumber(id);
  LValue value = output().buildLoadMagic(output().repo().ref8, magic);
  GetBuilderImpl(current_bb_)->set_value(id, value);
  load_constant_recorder_->Register(magic,
                                    LoadConstantRecorder::kExternalReference);
}

void LLVMTFBuilder::VisitPhi(int id, MachineRepresentation rep,
                             const OperandsVector& operands) {
  output().setLineNumber(id);
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
  GetBuilderImpl(current_bb_)->set_value(id, phi);
}

void LLVMTFBuilder::VisitCall(int id, bool code,
                              const CallDescriptor& call_desc,
                              const OperandsVector& operands) {
  output().setLineNumber(id);
  DoCall(id, code, call_desc, operands, false);
}

void LLVMTFBuilder::VisitTailCall(int id, bool code,
                                  const CallDescriptor& call_desc,
                                  const OperandsVector& operands) {
  output().setLineNumber(id);
  DoTailCall(id, code, call_desc, operands);
}

void LLVMTFBuilder::VisitInvoke(int id, bool code,
                                const CallDescriptor& call_desc,
                                const OperandsVector& operands, int then,
                                int exception) {
  output().setLineNumber(id);
  BasicBlock* then_bb = basic_block_manager().ensureBB(then);
  BasicBlock* exception_bb = basic_block_manager().ensureBB(exception);
  EnsureNativeBB(then_bb, output());
  EnsureNativeBB(exception_bb, output());
  InvokeResolver resolver(current_bb_, output(), id, stack_map_info_map_,
                          state_point_id_next_++, then_bb, exception_bb);
  resolver.Resolve(code, call_desc, operands, node_call_target_map_);
}

void LLVMTFBuilder::VisitCallWithCallerSavedRegisters(
    int id, const OperandsVector& operands) {
  output().setLineNumber(id);
  std::vector<LType> types;
  std::vector<LValue> values;
  auto it = operands.begin();
  auto impl = GetBuilderImpl(current_bb_);
  LValue function = impl->value(*(it++));
  LLVMTypeKind kind = LLVMGetTypeKind(typeOf(function));
  if (kind == LLVMIntegerTypeKind) {
    function = output().buildCast(LLVMIntToPtr, function, output().repo().ref8);
  }
  for (; it != operands.end(); ++it) {
    LValue val = impl->value(*it);
    LType val_type = typeOf(val);
    types.push_back(val_type);
    values.push_back(val);
  }
  LType function_type = functionType(output().repo().intPtr, types.data(),
                                     types.size(), NotVariadic);
  LValue result = output().buildCall(
      output().buildBitCast(function, pointerType(function_type)),
      values.data(), values.size());
  impl->set_value(id, result);
}

void LLVMTFBuilder::VisitRoot(int id, int index) {
  output().setLineNumber(id);
  LValue offset = output().buildGEPWithByteOffset(
      output().root(),
      output().constInt32(index * sizeof(void*) - kRootRegisterBias),
      pointerType(output().taggedType()));
  LValue value = output().buildLoad(offset);
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitRootRelative(int id, int offset, bool tagged) {
  output().setLineNumber(id);
  LType type =
      pointerType(tagged ? output().taggedType() : output().repo().ref8);
  LValue offset_value = output().buildGEPWithByteOffset(
      output().root(), output().constInt32(offset), type);
  LValue value = output().buildLoad(offset_value);
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitRootOffset(int id, int offset) {
  output().setLineNumber(id);
  LType type = output().repo().ref8;
  LValue offset_value = output().buildGEPWithByteOffset(
      output().root(), output().constInt32(offset), type);
  GetBuilderImpl(current_bb_)->set_value(id, offset_value);
}

void LLVMTFBuilder::VisitLoadFromConstantTable(int id, int constant_index) {
  output().setLineNumber(id);
  LValue constant_table_offset = output().buildGEPWithByteOffset(
      output().root(),
      output().constInt32(Heap::kBuiltinsConstantsTableRootIndex *
                              sizeof(void*) -
                          kRootRegisterBias),
      pointerType(output().taggedType()));
  LValue constant_table = output().buildLoad(constant_table_offset);
  LValue offset = output().buildGEPWithByteOffset(
      constant_table,
      output().constInt32(FixedArray::kHeaderSize +
                          constant_index * kPointerSize - kHeapObjectTag),
      pointerType(output().taggedType()));

  GetBuilderImpl(current_bb_)->set_value(id, output().buildLoad(offset));
}

void LLVMTFBuilder::VisitCodeForCall(int id, int64_t magic, bool relative) {
  output().setLineNumber(id);
  if (relative) {
    node_call_target_map_.emplace(id, magic);
  } else {
    LValue value = output().buildLoadMagic(output().repo().ref8, magic);
    GetBuilderImpl(current_bb_)->set_value(id, value);
    load_constant_recorder_->Register(magic,
                                      LoadConstantRecorder::kCodeConstant);
  }
}

void LLVMTFBuilder::VisitSmiConstant(int id, void* smi_value) {
  output().setLineNumber(id);
  LValue value = output().constTagged(smi_value);
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitFloat64Constant(int id, double float_value) {
  output().setLineNumber(id);
  LValue value = constReal(output().repo().doubleType, float_value);
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitProjection(int id, int e, int index) {
  output().setLineNumber(id);
  LValue projection = GetBuilderImpl(current_bb_)->value(e);
  LValue value = output().buildExtractValue(projection, index);
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitFloat64Add(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue result = output().buildFAdd(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64Sub(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue result = output().buildFSub(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64Mul(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue result = output().buildFMul(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64Div(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue result = output().buildFDiv(e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64Mod(int id, int e1, int e2) {
  output().setLineNumber(id);
  LType double_type = output().repo().doubleType;
  LType function_type = functionType(double_type, double_type, double_type);
  LValue function = CallGetModTwoDoubleFunction();
  function = output().buildBitCast(function, pointerType(function_type));
  LValue e1_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue result = output().buildCall(function, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64LessThan(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue result = output().buildFCmp(LLVMRealOLT, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64LessThanOrEqual(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue result = output().buildFCmp(LLVMRealOLE, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64Equal(int id, int e1, int e2) {
  output().setLineNumber(id);
  LValue e1_value = GetBuilderImpl(current_bb_)->value(e1);
  LValue e2_value = GetBuilderImpl(current_bb_)->value(e2);
  LValue result = output().buildFCmp(LLVMRealOEQ, e1_value, e2_value);
  GetBuilderImpl(current_bb_)->set_value(id, result);
}

void LLVMTFBuilder::VisitFloat64Neg(int id, int e) {
  output().setLineNumber(id);
  LValue e_value = GetBuilderImpl(current_bb_)->value(e);
  LValue value = output().buildFNeg(e_value);
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitFloat64Abs(int id, int e) {
  output().setLineNumber(id);
  LValue e_value = GetBuilderImpl(current_bb_)->value(e);
  LValue value =
      output().buildCall(output().repo().doubleAbsIntrinsic(), e_value);
  GetBuilderImpl(current_bb_)->set_value(id, value);
}

void LLVMTFBuilder::VisitReturn(int id, int pop_count,
                                const OperandsVector& operands) {
  output().setLineNumber(id);
  int instructions_count = 2;
  if (operands.size() == 1) {
    LValue return_value = GetBuilderImpl(current_bb_)->value(operands[0]);
    LValue pop_count_value = GetBuilderImpl(current_bb_)->value(pop_count);
    std::unique_ptr<StackMapInfo> info(new ReturnInfo());
    ReturnInfo* rinfo = static_cast<ReturnInfo*>(info.get());
    if (LLVMIsConstant(pop_count_value)) {
      int pop_count_constant = LLVMConstIntGetZExtValue(pop_count_value) +
                               output().stack_parameter_count();
      rinfo->set_pop_count_is_constant(true);
      rinfo->set_constant(pop_count_constant);
      pop_count_value = LLVMGetUndef(output().repo().intPtr);
      if (pop_count_constant == 0) instructions_count = 1;
    }
    int patchid = state_point_id_next_++;
    output().buildCall(output().repo().patchpointVoidIntrinsic(),
                       output().constInt64(patchid),
                       output().constInt32(instructions_count * 4),
                       constNull(output().repo().ref8), output().constInt32(2),
                       return_value, pop_count_value);
    output().buildUnreachable();
#if defined(UC_3_0)
    stack_map_info_map_->emplace(patchid, std::move(info));
#else
    stack_map_info_map_->insert(std::make_pair(patchid, std::move(info)));
#endif
  } else
    __builtin_trap();
}

LValue LLVMTFBuilder::CallGetIsolateFunction() {
  if (!get_isolate_function_) {
    get_isolate_function_ = output().addFunction(
        "GetIsolate",
        functionType(output().repo().ref8, pointerType(output().taggedType())));
    LLVMSetLinkage(get_isolate_function_, LLVMInternalLinkage);
  }
  return output().buildCall(get_isolate_function_, output().root());
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

void LLVMTFBuilder::BuildGetIsolateFunction(
    BuiltinFunctionClient* builtin_function_client) {
  BuildFunctionUtil(
      get_isolate_function_, [this, builtin_function_client](LValue root) {
        builtin_function_client->BuildGetIsolateFunction(output(), root);
      });
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
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

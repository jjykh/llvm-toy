// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/tf/v8-code-assemble.h"

#include "src/codegen/assembler-inl.h"
#include "src/llvm/compiler-state.h"
#include "src/llvm/exception-table-arm.h"
#include "src/llvm/stack-maps.h"

#include <unordered_set>
#include "src/codegen/callable.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/safepoint-table.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"

namespace v8 {
namespace internal {
namespace tf_llvm {

namespace {
class RelocationProcessor {
 public:
  void ProcessConstantLoad(const uint8_t* code_start, const uint8_t* pc,
                           const LoadConstantRecorder&);
  void EmitRelativeCall(int pc);
  void ProcessRelocationWorkList(TurboAssembler* tasm);
  RelocationProcessor() = default;
  ~RelocationProcessor() = default;

 private:
  void EmitLoadRelocation(int constant, int pc,
                          const LoadConstantRecorder::MagicInfo&);
  // constant pc, pc, type, rmode
  using WorkListEntry = std::tuple<int, int, LoadConstantRecorder::MagicInfo>;
  std::vector<WorkListEntry> work_list_;
  using ConstantLocationSet = std::unordered_set<int>;
  ConstantLocationSet constant_location_set_;
};

class CodeAssemblerLLVM {
 public:
  CodeAssemblerLLVM(TurboAssembler*, SafepointTableBuilder*, int*, Zone*);
  ~CodeAssemblerLLVM() = default;
  bool Assemble(const CompilerState& state);

 private:
  int HandleCall(const CallInfo*, const StackMaps::Record&);
  int HandleStoreBarrier(const StackMaps::Record&);
  int HandleReturn(const ReturnInfo*, const StackMaps::Record&);
  int HandleStackMapInfo(const StackMapInfo* stack_map_info,
                         const StackMaps::Record* record);
  void ProcessRecordMap(const StackMaps::RecordMap& rm,
                        const StackMapInfoMap& info_map);
  // Exception Handling
  void EmitHandlerTable(const CompilerState& state);
  // Adjust for LLVM's callseq_end, which will emit a SDNode
  // Copy r0. And it will becomes a defined instruction if register allocator
  // allocates result other than r0.
  void AdjustCallSite(int* callsite);
  void AdjustHandler(int* handler);
  bool IsCallAt(int offset);

  struct RecordReference {
    const StackMaps::Record* record;
    const StackMapInfo* info;
    RecordReference(const StackMaps::Record*, const StackMapInfo*);
    ~RecordReference() = default;
  };

  typedef std::unordered_map<uint32_t, RecordReference> RecordReferenceMap;
  TurboAssembler& tasm_;
  SafepointTableBuilder& safepoint_table_builder_;
  RecordReferenceMap record_reference_map_;
  RelocationProcessor relocation_processor_;
  int& handler_table_offset_;
  uint32_t reference_instruction_;
  int slot_count_ = 0;
};

CodeAssemblerLLVM::CodeAssemblerLLVM(
    TurboAssembler* tasm, SafepointTableBuilder* safepoint_table_builder,
    int* handler_table_offset, Zone* zone)
    : tasm_(*tasm),
      safepoint_table_builder_(*safepoint_table_builder),
      handler_table_offset_(*handler_table_offset) {}

int CodeAssemblerLLVM::HandleCall(const CallInfo* call_info,
                                  const StackMaps::Record& record) {
  auto call_paramters_iterator = call_info->locations().begin();
  int call_target_reg = *(call_paramters_iterator++);
  int pc_offset = tasm_.pc_offset();
  int64_t relative_target = call_info->relative_target();
  RegList reg_list = 0;
  for (; call_paramters_iterator != call_info->locations().end();
       ++call_paramters_iterator) {
    int reg = *call_paramters_iterator;
    reg_list |= 1 << reg;
  }
  if (call_info->is_tailcall() && call_info->tailcall_return_count()) {
    tasm_.add(sp, sp, Operand(call_info->tailcall_return_count() * 4));
  }
  if (reg_list != 0) tasm_.stm(db_w, sp, reg_list);
  // Emit branch instr.
  if (!relative_target) {
    if (!call_info->is_tailcall())
      tasm_.blx(Register::from_code(call_target_reg));
    else
      tasm_.bx(Register::from_code(call_target_reg));
  } else {
    int code_target_index = tasm_.LLVMAddCodeTarget(
        Handle<Code>(reinterpret_cast<Address*>(relative_target)));
    relocation_processor_.EmitRelativeCall(tasm_.pc_offset());
    if (!call_info->is_tailcall())
      tasm_.bl(code_target_index * kInstrSize);
    else
      tasm_.b(code_target_index * kInstrSize);
  }
  if (!call_info->is_tailcall()) {
    // record safepoint
    // FIXME: (UC_linzj) kLazyDeopt is abusing, pass frame-state flags to
    // determine.
    Safepoint safepoint =
        safepoint_table_builder_.DefineSafepoint(&tasm_, Safepoint::kLazyDeopt);
    for (auto& location : record.locations) {
      if (location.kind != StackMaps::Location::Indirect) continue;
      // only understand stack slot
      if (location.dwarfReg == 13) {
        // Remove the effect from safepoint-table.cc
        safepoint.DefinePointerSlot(slot_count_ - 1 -
                                    (location.offset - call_info->sp_adjust()) /
                                        kPointerSize);
      } else {
        CHECK(location.dwarfReg == 11);
        safepoint.DefinePointerSlot(-location.offset / kPointerSize + 1);
      }
    }
  }
  CHECK(0 == ((tasm_.pc_offset() - pc_offset) % sizeof(uint32_t)));
  return (tasm_.pc_offset() - pc_offset) / sizeof(uint32_t);
}

int CodeAssemblerLLVM::HandleStoreBarrier(const StackMaps::Record& r) {
  int pc_offset = tasm_.pc_offset();
  tasm_.blx(ip);
  CHECK(0 == ((tasm_.pc_offset() - pc_offset) % sizeof(uint32_t)));
  return (tasm_.pc_offset() - pc_offset) / sizeof(uint32_t);
}

int CodeAssemblerLLVM::HandleReturn(const ReturnInfo* info,
                                    const StackMaps::Record&) {
  int instruction_count = 2;
  if (info->pop_count_is_constant()) {
    if (info->constant() != 0)
      tasm_.add(sp, sp, Operand(info->constant() * 4));
    else
      instruction_count = 1;
  } else {
    tasm_.add(sp, sp, Operand(r2, LSL, 2));
  }
  tasm_.bx(lr);
  return instruction_count;
}

int CodeAssemblerLLVM::HandleStackMapInfo(const StackMapInfo* stack_map_info,
                                          const StackMaps::Record* record) {
  switch (stack_map_info->GetType()) {
    case StackMapInfoType::kCallInfo:
      return HandleCall(static_cast<const CallInfo*>(stack_map_info), *record);
    case StackMapInfoType::kStoreBarrier:
      return HandleStoreBarrier(*record);
    case StackMapInfoType::kReturn:
      return HandleReturn(static_cast<const ReturnInfo*>(stack_map_info),
                          *record);
  }
  UNREACHABLE();
}

bool CodeAssemblerLLVM::Assemble(const CompilerState& state) {
  auto rm = state.sm_.computeRecordMap();
  const ByteBuffer& code = state.codeSectionList_.front();

  const uint32_t* code_start = reinterpret_cast<const uint32_t*>(code.data());
  const uint32_t* instruction_pointer = code_start;

  unsigned num_bytes = code.size();
  ProcessRecordMap(rm, state.stack_map_info_map_);

  slot_count_ = state.sm_.stackSize() / kPointerSize;

  int incremental = 0;
  int base_offset = tasm_.pc_offset();
  {
    Assembler::BlockConstPoolScope block_const_pool(&tasm_);

    for (; num_bytes; num_bytes -= incremental * sizeof(uint32_t),
                      instruction_pointer += incremental) {
      int pc_offset = tasm_.pc_offset() - base_offset;
      CHECK((pc_offset + num_bytes) == code.size());
      auto found = record_reference_map_.find(pc_offset);
      if (found != record_reference_map_.end()) {
        reference_instruction_ = *instruction_pointer;
        auto& record_reference = found->second;
        incremental =
            HandleStackMapInfo(record_reference.info, record_reference.record);
        continue;
      }
      relocation_processor_.ProcessConstantLoad(
          reinterpret_cast<const uint8_t*>(code_start),
          reinterpret_cast<const uint8_t*>(instruction_pointer),
          state.load_constant_recorder_);
      incremental = 1;
      uint32_t instruction = *instruction_pointer;
      tasm_.dd(instruction);
    }
  }
  safepoint_table_builder_.Emit(&tasm_, slot_count_);
  EmitHandlerTable(state);
  instruction_pointer = reinterpret_cast<const uint32_t*>(code.data());
  relocation_processor_.ProcessRelocationWorkList(&tasm_);
  record_reference_map_.clear();
  return true;
}

void CodeAssemblerLLVM::ProcessRecordMap(const StackMaps::RecordMap& rm,
                                         const StackMapInfoMap& info_map) {
  for (auto& item : rm) {
    CHECK(item.second.size() == 1);
    auto& record = item.second.front();
    uint32_t instruction_offset = item.first;
    auto stack_map_info_found = info_map.find(record.patchpointID);
    CHECK(stack_map_info_found != info_map.end());
    const StackMapInfo* stack_map_info = stack_map_info_found->second.get();
    switch (stack_map_info->GetType()) {
      case StackMapInfoType::kCallInfo:
      case StackMapInfoType::kStoreBarrier:
      case StackMapInfoType::kReturn:
        break;
      default:
        UNREACHABLE();
    }
#if defined(UC_3_0)
    record_reference_map_.emplace(instruction_offset,
                                  RecordReference(&record, stack_map_info));
#else
    record_reference_map_.insert(std::make_pair(
        instruction_offset, RecordReference(&record, stack_map_info)));
#endif
  }
}

void CodeAssemblerLLVM::EmitHandlerTable(const CompilerState& state) {
  if (!state.exception_table_) return;
  ExceptionTableARM exception_table(state.exception_table_->data(),
                                    state.exception_table_->size());
  const std::vector<std::tuple<int, int>>& callsite_handler_pairs =
      exception_table.CallSiteHandlerPairs();
  handler_table_offset_ = HandlerTable::EmitReturnTableStart(&tasm_);
  for (size_t i = 0; i < callsite_handler_pairs.size(); ++i) {
    int callsite, handler;
    std::tie(callsite, handler) = callsite_handler_pairs[i];
    AdjustCallSite(&callsite);
    AdjustHandler(&handler);
    HandlerTable::EmitReturnEntry(&tasm_, callsite, handler);
  }
}

// Adjust for LLVM's callseq_end, which will emit a SDNode
// Copy r0. And it will becomes a defined instruction if register allocator
// allocates result other than r0.
void CodeAssemblerLLVM::AdjustCallSite(int* callsite) {
  int callsite_adj = *callsite;
  callsite_adj -= sizeof(Instr);
  while ((callsite_adj >= 0) && !IsCallAt(callsite_adj)) {
    callsite_adj -= sizeof(Instr);
  }
  EMASSERT(callsite_adj >= 0);
  *callsite = callsite_adj + sizeof(Instr);
}

// Adjust for LLVM's unmergeable block, which results in a branch.
void CodeAssemblerLLVM::AdjustHandler(int* handler) {
  int handler_adj = *handler;
  Instr instr = tasm_.instr_at(handler_adj);
  Instruction* where = Instruction::At(reinterpret_cast<Address>(&instr));
  if (where->IsBranch()) {
    handler_adj = handler_adj + where->GetBranchOffset() + 8;
  }
  *handler = handler_adj;
}

bool CodeAssemblerLLVM::IsCallAt(int offset) {
  Instr instr = tasm_.instr_at(offset);
  return Assembler::IsBlxReg(instr) || Assembler::IsBlOffset(instr);
}

CodeAssemblerLLVM::RecordReference::RecordReference(
    const StackMaps::Record* _record, const StackMapInfo* _info)
    : record(_record), info(_info) {}

void RelocationProcessor::ProcessConstantLoad(
    const uint8_t* code_start, const uint8_t* instruction_pointer,
    const LoadConstantRecorder& load_constant_recorder) {
  Instr instruction = *reinterpret_cast<const Instr*>(instruction_pointer);
  if (Assembler::IsLdrPcImmediateOffset(instruction)) {
    Address& address = Memory<Address>(Assembler::constant_pool_entry_address(
        reinterpret_cast<Address>(const_cast<uint8_t*>(instruction_pointer)),
        0));
    std::unique_ptr<StackMapInfo> to_push;
    int constant_pc_offset =
        std::distance(code_start, reinterpret_cast<const uint8_t*>(&address));
    if (constant_location_set_.find(constant_pc_offset) !=
        constant_location_set_.end())
      return;
    constant_location_set_.insert(constant_pc_offset);
    int pc_offset = std::distance(code_start, instruction_pointer);

    auto info = load_constant_recorder.Query(static_cast<uintptr_t>(address));
    EmitLoadRelocation(constant_pc_offset, pc_offset, info);
  }
}

void RelocationProcessor::EmitLoadRelocation(
    int constant, int pc, const LoadConstantRecorder::MagicInfo& info) {
  work_list_.emplace_back(constant, pc, info);
}

void RelocationProcessor::EmitRelativeCall(int pc) {
  work_list_.emplace_back(
      0, pc,
      LoadConstantRecorder::MagicInfo(LoadConstantRecorder::kRelativeCall));
}

void RelocationProcessor::ProcessRelocationWorkList(TurboAssembler* tasm) {
  int pc_offset = tasm->pc_offset();
  tasm->LLVMGrowBuffer();
  std::stable_sort(work_list_.begin(), work_list_.end(),
                   [](const WorkListEntry& lhs, const WorkListEntry& rhs) {
                     return std::get<0>(lhs) < std::get<0>(rhs);
                   });
  for (auto& entry : work_list_) {
    const auto& magic_info = std::get<2>(entry);
    bool should_tune_constant = true;
    switch (magic_info.type) {
      case LoadConstantRecorder::kHeapConstant:
        tasm->reset_pc(std::get<1>(entry));
        tasm->RecordRelocInfo(RelocInfo::FULL_EMBEDDED_OBJECT);
        break;
      case LoadConstantRecorder::kCodeConstant:
        tasm->reset_pc(std::get<1>(entry));
        tasm->RecordRelocInfo(RelocInfo::CODE_TARGET);
        break;
      case LoadConstantRecorder::kExternalReference:
        tasm->reset_pc(std::get<1>(entry));
        tasm->RecordRelocInfo(RelocInfo::EXTERNAL_REFERENCE);
        break;
      case LoadConstantRecorder::kRelativeCall:
        tasm->reset_pc(std::get<1>(entry));
        tasm->RecordRelocInfo(RelocInfo::RELATIVE_CODE_TARGET);
        should_tune_constant = false;
        break;
      case LoadConstantRecorder::kRelocatableInt32Constant:
        tasm->reset_pc(std::get<1>(entry));
        tasm->RecordRelocInfo(static_cast<RelocInfo::Mode>(magic_info.rmode));
        break;
      default:
        UNREACHABLE();
    }
    if (should_tune_constant) {
      tasm->reset_pc(std::get<0>(entry));
      tasm->dd(magic_info.real_magic);
    }
  }
  tasm->reset_pc(pc_offset);
}
}  // namespace

bool AssembleCode(const CompilerState& state, TurboAssembler* tasm,
                  SafepointTableBuilder* safepoint_builder,
                  int* handler_table_offset, Zone* zone) {
  CodeAssemblerLLVM code_assembler(tasm, safepoint_builder,
                                   handler_table_offset, zone);
  return code_assembler.Assemble(state);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

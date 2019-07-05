// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/tf/v8-codegen.h"

#include "src/assembler-inl.h"
#include "src/llvm/compiler-state.h"
#include "src/llvm/exception-table-arm.h"
#include "src/llvm/stack-maps.h"

#include <unordered_set>
#include "src/callable.h"
#include "src/handles-inl.h"
#include "src/heap/factory.h"
#include "src/macro-assembler.h"
#include "src/safepoint-table.h"

namespace v8 {
namespace internal {
namespace tf_llvm {

namespace {
class RelocationProcessor {
 public:
  enum Type {
    // must sync with LoadConstantRecorder's Type
    kExternalReference,
    kHeapConstant,
    kCodeConstant,
    kRelativeCall
  };
  void ProcessConstantLoad(const uint8_t* code_start, const uint8_t* pc,
                           const LoadConstantRecorder&);
  void EmitRelativeCall(int pc);
  void ProcessRelocationWorkList(MacroAssembler* masm);
  RelocationProcessor() = default;
  ~RelocationProcessor() = default;

 private:
  void EmitLoadRelocation(int constant, int pc, Type type);
  // constant pc, pc, type
  using WorkListEntry = std::tuple<int, int, Type>;
  std::vector<WorkListEntry> work_list_;
  using ConstantLocationSet = std::unordered_set<int>;
  ConstantLocationSet constant_location_set_;
};

class CodeGeneratorLLVM {
 public:
  CodeGeneratorLLVM(Isolate*);
  ~CodeGeneratorLLVM() = default;
  Handle<Code> Generate(const CompilerState& state);

 private:
  int HandleCall(const CallInfo*, const StackMaps::Record&);
  int HandleStoreBarrier(const StackMaps::Record&);
  int HandleReturn(const ReturnInfo*, const StackMaps::Record&);
  int HandleStackMapInfo(const StackMapInfo* stack_map_info,
                         const StackMaps::Record* record);
  void ProcessRecordMap(const StackMaps::RecordMap& rm,
                        const StackMapInfoMap& info_map);
  // Exception Handling
  void EmitHandlerTable(const CompilerState& state, Isolate* isolate);
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
  Isolate* isolate_;
  Zone zone_;
  MacroAssembler masm_;
  SafepointTableBuilder safepoint_table_builder_;
  RecordReferenceMap record_reference_map_;
  RelocationProcessor relocation_processor_;
  uint32_t reference_instruction_;
  int slot_count_ = 0;
  int handler_table_offset_ = 0;
  bool needs_frame_ = false;
};

CodeGeneratorLLVM::CodeGeneratorLLVM(Isolate* isolate)
    : isolate_(isolate),
      zone_(isolate->allocator(), "llvm"),
      masm_(isolate, nullptr, 0, CodeObjectRequired::kYes),
      safepoint_table_builder_(&zone_) {}

int CodeGeneratorLLVM::HandleCall(const CallInfo* call_info,
                                  const StackMaps::Record& record) {
  auto call_paramters_iterator = call_info->locations().begin();
  int call_target_reg = *(call_paramters_iterator++);
  int pc_offset = masm_.pc_offset();
  int64_t relative_target = call_info->relative_target();
  RegList reg_list = 0;
  for (; call_paramters_iterator != call_info->locations().end();
       ++call_paramters_iterator) {
    int reg = *call_paramters_iterator;
    reg_list |= 1 << reg;
  }
  if (call_info->is_tailcall() && call_info->tailcall_return_count()) {
    masm_.add(sp, sp, Operand(call_info->tailcall_return_count() * 4));
  }
  if (reg_list != 0) masm_.stm(db_w, sp, reg_list);
  // Emit branch instr.
  if (!relative_target) {
    if (!call_info->is_tailcall())
      masm_.blx(Register::from_code(call_target_reg));
    else
      masm_.bx(Register::from_code(call_target_reg));
  } else {
    int code_target_index = masm_.LLVMAddCodeTarget(
        Handle<Code>(reinterpret_cast<Code**>(relative_target)));
    relocation_processor_.EmitRelativeCall(masm_.pc_offset());
    if (!call_info->is_tailcall())
      masm_.bl(code_target_index * Instruction::kInstrSize);
    else
      masm_.b(code_target_index * Instruction::kInstrSize);
  }
  if (!call_info->is_tailcall()) {
    // record safepoint
    // FIXME: (UC_linzj) kLazyDeopt is abusing, pass frame-state flags to
    // determine.
    Safepoint safepoint = safepoint_table_builder_.DefineSafepoint(
        &masm_, Safepoint::kSimple, 0, Safepoint::kLazyDeopt);
    for (auto& location : record.locations) {
      if (location.kind != StackMaps::Location::Indirect) continue;
      // only understand stack slot
      if (location.dwarfReg == 13) {
        // Remove the effect from safepoint-table.cc
        safepoint.DefinePointerSlot(
            slot_count_ - 1 - location.offset / kPointerSize, &zone_);
      } else {
        CHECK(location.dwarfReg == 11);
        safepoint.DefinePointerSlot(-location.offset / kPointerSize + 1,
                                    &zone_);
      }
    }
  }
  CHECK(0 == ((masm_.pc_offset() - pc_offset) % sizeof(uint32_t)));
  return (masm_.pc_offset() - pc_offset) / sizeof(uint32_t);
}

int CodeGeneratorLLVM::HandleStoreBarrier(const StackMaps::Record& r) {
  int pc_offset = masm_.pc_offset();
  masm_.blx(ip);
  CHECK(0 == ((masm_.pc_offset() - pc_offset) % sizeof(uint32_t)));
  return (masm_.pc_offset() - pc_offset) / sizeof(uint32_t);
}

int CodeGeneratorLLVM::HandleReturn(const ReturnInfo* info,
                                    const StackMaps::Record&) {
  int instruction_count = 2;
  if (info->pop_count_is_constant()) {
    if (info->constant() != 0)
      masm_.add(sp, sp, Operand(info->constant() * 4));
    else
      instruction_count = 1;
  } else {
    masm_.add(sp, sp, Operand(r1, LSL, 2));
  }
  masm_.bx(lr);
  return instruction_count;
}

int CodeGeneratorLLVM::HandleStackMapInfo(const StackMapInfo* stack_map_info,
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

Handle<Code> CodeGeneratorLLVM::Generate(const CompilerState& state) {
  StackMaps sm;
  if (state.stackMapsSection_) {
    DataView dv(state.stackMapsSection_->data());
    sm.parse(&dv);
  }
  auto rm = sm.computeRecordMap();
  const ByteBuffer& code = state.codeSectionList_.front();
  needs_frame_ = state.needs_frame_;

  const uint32_t* code_start = reinterpret_cast<const uint32_t*>(code.data());
  const uint32_t* instruction_pointer = code_start;

  unsigned num_bytes = code.size();
  const uint32_t* instruction_end =
      reinterpret_cast<const uint32_t*>(code.data() + num_bytes);
  ProcessRecordMap(rm, state.stack_map_info_map_);

  slot_count_ = sm.stackSize() / kPointerSize;
  CHECK(slot_count_ < 0x1000);
  int incremental = 0;
  int base_offset = masm_.pc_offset();
  masm_.set_builtin_index(state.builtin_index_);
  {
    Assembler::BlockConstPoolScope block_const_pool(&masm_);

    for (; num_bytes; num_bytes -= incremental * sizeof(uint32_t),
                      instruction_pointer += incremental) {
      int pc_offset = masm_.pc_offset() - base_offset;
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
      masm_.dd(instruction);
    }
  }
  EmitHandlerTable(state, isolate_);
  instruction_pointer = reinterpret_cast<const uint32_t*>(code.data());
  relocation_processor_.ProcessRelocationWorkList(&masm_);
  record_reference_map_.clear();
  safepoint_table_builder_.Emit(&masm_, slot_count_);
  CodeDesc desc;
  masm_.GetCode(isolate_, &desc);

  MaybeHandle<Code> maybe_code = isolate_->factory()->TryNewCode(
      desc, static_cast<Code::Kind>(state.code_kind_), Handle<Object>(),
      state.builtin_index_, MaybeHandle<ByteArray>(),
      MaybeHandle<DeoptimizationData>(), kMovable, state.stub_key_, true,
      slot_count_, safepoint_table_builder_.GetCodeOffset(),
      handler_table_offset_);

  Handle<Code> result;
  if (!maybe_code.ToHandle(&result)) {
    masm_.AbortedCodeGeneration();
    return Handle<Code>();
  }
  return result;
}

void CodeGeneratorLLVM::ProcessRecordMap(const StackMaps::RecordMap& rm,
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

void CodeGeneratorLLVM::EmitHandlerTable(const CompilerState& state,
                                         Isolate* isolate) {
  if (!state.exception_table_) return;
  ExceptionTableARM exception_table(state.exception_table_->data(),
                                    state.exception_table_->size());
  const std::vector<std::tuple<int, int>>& callsite_handler_pairs =
      exception_table.CallSiteHandlerPairs();
  handler_table_offset_ = HandlerTable::EmitReturnTableStart(
      &masm_, static_cast<int>(callsite_handler_pairs.size()));
  for (size_t i = 0; i < callsite_handler_pairs.size(); ++i) {
    int callsite, handler;
    std::tie(callsite, handler) = callsite_handler_pairs[i];
    AdjustCallSite(&callsite);
    AdjustHandler(&handler);
    HandlerTable::EmitReturnEntry(&masm_, callsite, handler);
  }
}

// Adjust for LLVM's callseq_end, which will emit a SDNode
// Copy r0. And it will becomes a defined instruction if register allocator
// allocates result other than r0.
void CodeGeneratorLLVM::AdjustCallSite(int* callsite) {
  int callsite_adj = *callsite;
  callsite_adj -= sizeof(Instr);
  while ((callsite_adj >= 0) && !IsCallAt(callsite_adj)) {
    callsite_adj -= sizeof(Instr);
  }
  EMASSERT(callsite_adj >= 0);
  *callsite = callsite_adj;
}

// Adjust for LLVM's unmergeable block, which results in a branch.
void CodeGeneratorLLVM::AdjustHandler(int* handler) {
  int handler_adj = *handler;
  Instr instr = masm_.instr_at(handler_adj);
  Instruction* where = Instruction::At(reinterpret_cast<Address>(&instr));
  if (where->IsBranch()) {
    handler_adj = handler_adj + where->GetBranchOffset() + 8;
  }
  *handler = handler_adj;
}

bool CodeGeneratorLLVM::IsCallAt(int offset) {
  Instr instr = masm_.instr_at(offset);
  return Assembler::IsBlxReg(instr) || Assembler::IsBlOffset(instr);
}

CodeGeneratorLLVM::RecordReference::RecordReference(
    const StackMaps::Record* _record, const StackMapInfo* _info)
    : record(_record), info(_info) {}

void RelocationProcessor::ProcessConstantLoad(
    const uint8_t* code_start, const uint8_t* instruction_pointer,
    const LoadConstantRecorder& load_constant_recorder) {
  Instr instruction = *reinterpret_cast<const Instr*>(instruction_pointer);
  if (Assembler::IsLdrPcImmediateOffset(instruction)) {
    Address& address =
        Memory::Address_at(Assembler::constant_pool_entry_address(
            reinterpret_cast<Address>(
                const_cast<uint8_t*>(instruction_pointer)),
            0));
    std::unique_ptr<StackMapInfo> to_push;
    int constant_pc_offset =
        std::distance(code_start, reinterpret_cast<const uint8_t*>(&address));
    if (constant_location_set_.find(constant_pc_offset) !=
        constant_location_set_.end())
      return;
    constant_location_set_.insert(constant_pc_offset);
    int pc_offset = std::distance(code_start, instruction_pointer);

    LoadConstantRecorder::Type type =
        load_constant_recorder.Query(static_cast<int64_t>(address));
    EmitLoadRelocation(constant_pc_offset, pc_offset, static_cast<Type>(type));
  }
}

void RelocationProcessor::EmitLoadRelocation(int constant, int pc, Type type) {
  work_list_.emplace_back(constant, pc, type);
}

void RelocationProcessor::EmitRelativeCall(int pc) {
  work_list_.emplace_back(0, pc, kRelativeCall);
}

void RelocationProcessor::ProcessRelocationWorkList(MacroAssembler* masm) {
  int pc_offset = masm->pc_offset();
  masm->LLVMGrowBuffer();
  std::stable_sort(work_list_.begin(), work_list_.end(),
                   [](const WorkListEntry& lhs, const WorkListEntry& rhs) {
                     return std::get<0>(lhs) < std::get<0>(rhs);
                   });
  for (auto& entry : work_list_) {
    switch (std::get<2>(entry)) {
      case kHeapConstant:
        masm->reset_pc(std::get<1>(entry));
        masm->RecordRelocInfo(RelocInfo::EMBEDDED_OBJECT);
        break;
      case kCodeConstant:
        masm->reset_pc(std::get<1>(entry));
        masm->RecordRelocInfo(RelocInfo::CODE_TARGET);
        break;
      case kExternalReference:
        masm->reset_pc(std::get<1>(entry));
        masm->RecordRelocInfo(RelocInfo::EXTERNAL_REFERENCE);
        break;
      case kRelativeCall:
        masm->reset_pc(std::get<1>(entry));
        masm->RecordRelocInfo(RelocInfo::RELATIVE_CODE_TARGET);
        break;
      default:
        UNREACHABLE();
    }
  }
  masm->reset_pc(pc_offset);
}
}  // namespace

Handle<Code> GenerateCode(Isolate* isolate, const CompilerState& state) {
  HandleScope handle_scope(isolate);
  CodeGeneratorLLVM code_generator(isolate);
  return handle_scope.CloseAndEscape(code_generator.Generate(state));
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

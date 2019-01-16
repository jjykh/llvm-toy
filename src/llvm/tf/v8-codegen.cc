#include "src/llvm/tf/v8-codegen.h"

#include "src/assembler-inl.h"
#include "src/llvm/compiler-state.h"
#include "src/llvm/stack-maps.h"

#include <unordered_set>
#include "src/callable.h"
#include "src/factory.h"
#include "src/handles-inl.h"
#include "src/macro-assembler.h"
#include "src/safepoint-table.h"

namespace v8 {
namespace internal {
namespace tf_llvm {

namespace {
class CodeGeneratorLLVM {
 public:
  CodeGeneratorLLVM(Isolate*);
  ~CodeGeneratorLLVM() = default;
  Handle<Code> Generate(const CompilerState& state);

 private:
  int HandleHeapConstant();
  int HandleExternalReference();
  int HandleCodeConstant();
  int HandleIsolateExternalReferenceLocation();
  int HandleRecordStubCodeLocation();
  int HandleCall(const CallInfo*, const StackMaps::Record&);
  int HandleStoreBarrier(const StackMaps::Record&);
  int HandleStackMapInfo(const StackMapInfo* stack_map_info,
                         const StackMaps::Record* record);
  void ProcessCode(const uint32_t* code_start, const uint32_t* code_end,
                   const LoadConstantRecorder&);
  void ProcessRecordMap(const StackMaps::RecordMap& rm,
                        const StackMapInfoMap& info_map);
  void InsertLoadConstantInfoIfNeeded(
      int constant_pc_offset, int pc_offset, StackMapInfoType type,
      StackMapInfoType* location_type = nullptr);

  struct RecordReference {
    const StackMaps::Record* record;
    const StackMapInfo* info;
    RecordReference(const StackMaps::Record*, const StackMapInfo*);
    ~RecordReference() = default;
  };

  typedef std::unordered_map<uint32_t, RecordReference> RecordReferenceMap;
  typedef std::unordered_set<uint32_t> ConstantLocationSet;
  typedef std::vector<std::unique_ptr<StackMapInfo>> InfoStorage;
  Isolate* isolate_;
  Zone zone_;
  MacroAssembler masm_;
  SafepointTableBuilder safepoint_table_builder_;
  RecordReferenceMap record_reference_map_;
  InfoStorage info_storage_;
  ConstantLocationSet constant_location_set_;
  uint32_t reference_instruction_;
  int slot_count_ = 0;
  bool needs_frame_ = false;
};

CodeGeneratorLLVM::CodeGeneratorLLVM(Isolate* isolate)
    : isolate_(isolate),
      zone_(isolate->allocator(), "llvm"),
      masm_(isolate, nullptr, 0, CodeObjectRequired::kYes),
      safepoint_table_builder_(&zone_) {}

int CodeGeneratorLLVM::HandleHeapConstant() {
  masm_.RecordRelocInfo(RelocInfo::EMBEDDED_OBJECT);
  masm_.dd(reference_instruction_);
  return 1;
}

int CodeGeneratorLLVM::HandleExternalReference() {
  masm_.RecordRelocInfo(RelocInfo::EXTERNAL_REFERENCE);
  masm_.dd(reference_instruction_);
  return 1;
}

int CodeGeneratorLLVM::HandleCodeConstant() {
  masm_.RecordRelocInfo(RelocInfo::CODE_TARGET);
  masm_.dd(reference_instruction_);
  return 1;
}

int CodeGeneratorLLVM::HandleIsolateExternalReferenceLocation() {
  ExternalReference isolate_external_reference =
      ExternalReference::isolate_address(isolate_);
  masm_.dd(reinterpret_cast<intptr_t>(isolate_external_reference.address()));
  return 1;
}

int CodeGeneratorLLVM::HandleRecordStubCodeLocation() {
  Callable const callable =
      Builtins::CallableFor(isolate_, Builtins::kRecordWrite);
  masm_.dd(reinterpret_cast<intptr_t>(callable.code().location()));
  return 1;
}

int CodeGeneratorLLVM::HandleCall(const CallInfo* call_info,
                                  const StackMaps::Record& record) {
  auto call_paramters_iterator = call_info->locations().begin();
  int call_target_reg = *(call_paramters_iterator++);
  int pc_offset = masm_.pc_offset();
  if (needs_frame_ && call_info->tailcall()) {
    masm_.LeaveFrame(StackFrame::STUB);
  }
  for (; call_paramters_iterator != call_info->locations().end();
       ++call_paramters_iterator) {
    masm_.push(Register::from_code(*call_paramters_iterator));
  }

  if (!call_info->tailcall())
    masm_.blx(Register::from_code(call_target_reg));
  else
    masm_.bx(Register::from_code(call_target_reg));
  if (!call_info->tailcall()) {
    // record safepoint
    // FIXME: (UC_linzj) kLazyDeopt is abusing, pass frame-state flags to
    // determine.
    Safepoint safepoint = safepoint_table_builder_.DefineSafepoint(
        &masm_, Safepoint::kSimple, 0, Safepoint::kLazyDeopt);
    for (auto& location : record.locations) {
      if (location.kind != StackMaps::Location::Indirect) continue;
      // only understand stack slot
      CHECK(location.dwarfReg == 13);
      // Remove the effect from safepoint-table.cc
      safepoint.DefinePointerSlot(
          slot_count_ - 1 - location.offset / kPointerSize, &zone_);
    }
  }
  CHECK(0 == ((masm_.pc_offset() - pc_offset) % sizeof(uint32_t)));
  return (masm_.pc_offset() - pc_offset) / sizeof(uint32_t);
}

int CodeGeneratorLLVM::HandleStoreBarrier(const StackMaps::Record& r) {
  int pc_offset = masm_.pc_offset();
  if (!needs_frame_) masm_.Push(lr);
  masm_.blx(ip);
  if (!needs_frame_) masm_.Pop(lr);
  CHECK(0 == ((masm_.pc_offset() - pc_offset) % sizeof(uint32_t)));
  return (masm_.pc_offset() - pc_offset) / sizeof(uint32_t);
}

int CodeGeneratorLLVM::HandleStackMapInfo(const StackMapInfo* stack_map_info,
                                          const StackMaps::Record* record) {
  switch (stack_map_info->GetType()) {
    case StackMapInfoType::kHeapConstant:
      return HandleHeapConstant();
    case StackMapInfoType::kExternalReference:
      return HandleExternalReference();
    case StackMapInfoType::kCodeConstant:
      return HandleCodeConstant();
    case StackMapInfoType::kIsolateExternalReferenceLocation:
      return HandleIsolateExternalReferenceLocation();
    case StackMapInfoType::kRecordStubCodeLocation:
      return HandleRecordStubCodeLocation();
    case StackMapInfoType::kCallInfo:
      return HandleCall(static_cast<const CallInfo*>(stack_map_info), *record);
    case StackMapInfoType::kStoreBarrier:
      return HandleStoreBarrier(*record);
  }
  UNREACHABLE();
}

Handle<Code> CodeGeneratorLLVM::Generate(const CompilerState& state) {
  StackMaps sm;
  DataView dv(state.stackMapsSection_->data());
  sm.parse(&dv);
  auto rm = sm.computeRecordMap();
  const ByteBuffer& code = state.codeSectionList_.front();
  needs_frame_ = state.needs_frame_;

  const uint32_t* instruction_pointer =
      reinterpret_cast<const uint32_t*>(code.data());
  unsigned num_bytes = code.size();
  const uint32_t* instruction_end =
      reinterpret_cast<const uint32_t*>(code.data() + num_bytes);
  ProcessCode(instruction_pointer, instruction_end,
              state.load_constant_recorder_);
  ProcessRecordMap(rm, state.stack_map_info_map_);

  slot_count_ = state.frame_slot_count_;
  int incremental = 0;
  if (needs_frame_) {
    switch (state.prologue_kind_) {
      case PrologueKind::JSFunctionCall:
        masm_.Prologue();
        masm_.Push(kJavaScriptCallArgCountRegister);
        break;
      case PrologueKind::Stub:
        masm_.StubPrologue(StackFrame::STUB);
        break;
      default:
        UNREACHABLE();
    }
  }
  int base_offset = masm_.pc_offset();
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
      incremental = 1;
      uint32_t instruction = *instruction_pointer;
      masm_.dd(instruction);
    }
  }
  masm_.CheckConstPool(true, false);
  record_reference_map_.clear();
  safepoint_table_builder_.Emit(&masm_, slot_count_);
  CodeDesc desc;
  masm_.GetCode(isolate_, &desc);
  Handle<Code> new_object = isolate_->factory()->NewCode(
      desc, static_cast<Code::Kind>(state.code_kind_), masm_.CodeObject());
  new_object->set_stack_slots(slot_count_);
  new_object->set_safepoint_table_offset(
      safepoint_table_builder_.GetCodeOffset());
  new_object->set_is_turbofanned(true);
  return new_object;
}

void CodeGeneratorLLVM::ProcessCode(
    const uint32_t* code_start, const uint32_t* code_end,
    const LoadConstantRecorder& load_constant_recorder) {
  for (auto instruction_pointer = code_start; instruction_pointer != code_end;
       instruction_pointer += 1) {
    uint32_t instruction = *instruction_pointer;
    if (Assembler::IsLdrPcImmediateOffset(instruction)) {
      Address& address =
          Memory::Address_at(Assembler::constant_pool_entry_address(
              reinterpret_cast<Address>(
                  const_cast<uint32_t*>(instruction_pointer)),
              nullptr));
      std::unique_ptr<StackMapInfo> to_push;
      int constant_pc_offset =
          std::distance(reinterpret_cast<const uint8_t*>(code_start),
                        reinterpret_cast<const uint8_t*>(&address));
      int pc_offset =
          std::distance(reinterpret_cast<const uint8_t*>(code_start),
                        reinterpret_cast<const uint8_t*>(instruction_pointer));

      LoadConstantRecorder::Type type =
          load_constant_recorder.Query(reinterpret_cast<int64_t>(address));
      switch (type) {
        case LoadConstantRecorder::kHeapConstant:
          InsertLoadConstantInfoIfNeeded(constant_pc_offset, pc_offset,
                                         StackMapInfoType::kHeapConstant);
          break;
        case LoadConstantRecorder::kExternalReference:
          InsertLoadConstantInfoIfNeeded(constant_pc_offset, pc_offset,
                                         StackMapInfoType::kExternalReference);
          break;
        case LoadConstantRecorder::kCodeConstant:
          InsertLoadConstantInfoIfNeeded(constant_pc_offset, pc_offset,
                                         StackMapInfoType::kCodeConstant);
          break;
        case LoadConstantRecorder::kIsolateExternalReference: {
          StackMapInfoType location_type =
              StackMapInfoType::kIsolateExternalReferenceLocation;
          InsertLoadConstantInfoIfNeeded(constant_pc_offset, pc_offset,
                                         StackMapInfoType::kExternalReference,
                                         &location_type);
        } break;
        case LoadConstantRecorder::kRecordStubCodeConstant: {
          StackMapInfoType location_type =
              StackMapInfoType::kRecordStubCodeLocation;
          InsertLoadConstantInfoIfNeeded(constant_pc_offset, pc_offset,
                                         StackMapInfoType::kCodeConstant,
                                         &location_type);
        } break;
        default:
          UNREACHABLE();
      }
    }
  }
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

void CodeGeneratorLLVM::InsertLoadConstantInfoIfNeeded(
    int constant_pc_offset, int pc_offset, StackMapInfoType type,
    StackMapInfoType* location_type) {
  auto found = constant_location_set_.find(constant_pc_offset);
  if (found == constant_location_set_.end()) {
    constant_location_set_.insert(constant_pc_offset);
    info_storage_.emplace_back(new StackMapInfo(type));
#if defined(UC_3_0)
    record_reference_map_.emplace(
        pc_offset, RecordReference(nullptr, info_storage_.back().get()));
#else
    record_reference_map_.insert(std::make_pair(
        pc_offset, RecordReference(nullptr, info_storage_.back().get())));
#endif
    if (location_type) {
      info_storage_.emplace_back(new StackMapInfo(*location_type));
#if defined(UC_3_0)
      record_reference_map_.emplace(
          constant_pc_offset,
          RecordReference(nullptr, info_storage_.back().get()));
#else
      record_reference_map_.insert(
          std::make_pair(constant_pc_offset,
                         RecordReference(nullptr, info_storage_.back().get())));
#endif
    }
  }
}

CodeGeneratorLLVM::RecordReference::RecordReference(
    const StackMaps::Record* _record, const StackMapInfo* _info)
    : record(_record), info(_info) {}
}  // namespace

Handle<Code> GenerateCode(Isolate* isolate, const CompilerState& state) {
  HandleScope handle_scope(isolate);
  CodeGeneratorLLVM code_generator(isolate);
  return handle_scope.CloseAndEscape(code_generator.Generate(state));
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

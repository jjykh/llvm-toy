#include "src/llvm/tf/v8-codegen.h"

#include "src/assembler-inl.h"
#include "src/llvm/compiler-state.h"
#include "src/llvm/stack-maps.h"

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
  int HandleHeapConstant(const HeapConstantInfo*);
  int HandleHeapConstantLocation(const HeapConstantLocationInfo*);
  int HandleExternalReference(const ExternalReferenceInfo*);
  int HandleExternalReferenceLocation(const ExternalReferenceLocationInfo*);
  int HandleCall(const CallInfo*, const StackMaps::Record&);
  int HandleStoreBarrier(const StackMaps::Record&);
  int HandleStackMapInfo(const StackMapInfo* stack_map_info,
                         const StackMaps::Record* record);
  void ProcessCode(const uint32_t* code_start, const uint32_t* code_end);
  void ProcessRecordMap(const StackMaps::RecordMap& rm,
                        const StackMapInfoMap& info_map);

  struct RecordReference {
    const StackMaps::Record* record;
    const StackMapInfo* info;
    RecordReference(const StackMaps::Record*, const StackMapInfo*);
    ~RecordReference() = default;
  };

  typedef std::unordered_map<uint32_t, RecordReference> RecordReferenceMap;
  typedef std::vector<std::unique_ptr<StackMapInfo>> InfoStorage;
  Isolate* isolate_;
  Zone zone_;
  MacroAssembler masm_;
  SafepointTableBuilder safepoint_table_builder_;
  RecordReferenceMap record_reference_map_;
  InfoStorage info_storage_;
  int slot_count_ = 0;
  bool needs_frame_ = false;
};

CodeGeneratorLLVM::CodeGeneratorLLVM(Isolate* isolate)
    : isolate_(isolate),
      zone_(isolate->allocator(), "llvm"),
      masm_(isolate, nullptr, 0, CodeObjectRequired::kYes),
      safepoint_table_builder_(&zone_) {}

int CodeGeneratorLLVM::HandleHeapConstant(const HeapConstantInfo* heap_info) {
  masm_.RecordRelocInfo(RelocInfo::EMBEDDED_OBJECT);
  masm_.dd(*static_cast<const uint32_t*>(heap_info->pc()));
  return 1;
}

int CodeGeneratorLLVM::HandleHeapConstantLocation(
    const HeapConstantLocationInfo* heap_location_info) {
  Handle<HeapObject> object =
      handle(reinterpret_cast<HeapObject*>(
                 reinterpret_cast<intptr_t>(heap_location_info->pc())),
             isolate_);
  masm_.dd(reinterpret_cast<intptr_t>(object.location()));
  return 1;
}

int CodeGeneratorLLVM::HandleExternalReference(
    const ExternalReferenceInfo* external_info) {
  masm_.RecordRelocInfo(RelocInfo::EXTERNAL_REFERENCE);
  masm_.dd(*static_cast<const uint32_t*>(external_info->pc()));
  return 1;
}

int CodeGeneratorLLVM::HandleExternalReferenceLocation(
    const ExternalReferenceLocationInfo* external_location_info) {
  masm_.dd(reinterpret_cast<intptr_t>(external_location_info->pc()));
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

  if (!call_info->code_magic()) {
    if (!call_info->tailcall())
      masm_.blx(Register::from_code(call_target_reg));
    else
      masm_.bx(Register::from_code(call_target_reg));
  } else {
    Handle<Code> code =
        handle(reinterpret_cast<Code*>(call_info->code_magic()), isolate_);
    if (!call_info->tailcall())
      masm_.Call(code, RelocInfo::CODE_TARGET);
    else
      masm_.Jump(code, RelocInfo::CODE_TARGET);
  }
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
  Callable const callable =
      Builtins::CallableFor(isolate_, Builtins::kRecordWrite);
  if (!needs_frame_) masm_.Push(lr);
  masm_.mov(r2, Operand(ExternalReference::isolate_address(isolate_)));
  masm_.Call(callable.code(), RelocInfo::CODE_TARGET);
  if (!needs_frame_) masm_.Pop(lr);
  CHECK(0 == ((masm_.pc_offset() - pc_offset) % sizeof(uint32_t)));
  return (masm_.pc_offset() - pc_offset) / sizeof(uint32_t);
}

int CodeGeneratorLLVM::HandleStackMapInfo(const StackMapInfo* stack_map_info,
                                          const StackMaps::Record* record) {
  switch (stack_map_info->GetType()) {
    case StackMapInfoType::kHeapConstant:
      return HandleHeapConstant(
          static_cast<const HeapConstantInfo*>(stack_map_info));
    case StackMapInfoType::kHeapConstantLocation:
      return HandleHeapConstantLocation(
          static_cast<const HeapConstantLocationInfo*>(stack_map_info));
    case StackMapInfoType::kExternalReference:
      return HandleExternalReference(
          static_cast<const ExternalReferenceInfo*>(stack_map_info));
    case StackMapInfoType::kExternalReferenceLocation:
      return HandleExternalReferenceLocation(
          static_cast<const ExternalReferenceLocationInfo*>(stack_map_info));
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
  ProcessCode(instruction_pointer, instruction_end);
  ProcessRecordMap(rm, state.stack_map_info_map);

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

void CodeGeneratorLLVM::ProcessCode(const uint32_t* code_start,
                                    const uint32_t* code_end) {
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
      if (reinterpret_cast<intptr_t>(address) & 1) {
        to_push.reset(new HeapConstantInfo(instruction_pointer));
        auto found = record_reference_map_.find(constant_pc_offset);
        if (found == record_reference_map_.end()) {
          info_storage_.emplace_back(new HeapConstantLocationInfo(address));
#if defined(UC_3_0)
          record_reference_map_.emplace(
              constant_pc_offset,
              RecordReference(nullptr, info_storage_.back().get()));
#else
          record_reference_map_.insert(std::make_pair(
              constant_pc_offset,
              RecordReference(nullptr, info_storage_.back().get())));
#endif
        }
      } else {
        to_push.reset(new ExternalReferenceInfo(instruction_pointer));
        auto found = record_reference_map_.find(constant_pc_offset);
        if (found == record_reference_map_.end()) {
          info_storage_.emplace_back(
              new ExternalReferenceLocationInfo(address));
#if defined(UC_3_0)
          record_reference_map_.emplace(
              constant_pc_offset,
              RecordReference(nullptr, info_storage_.back().get()));
#else
          record_reference_map_.insert(std::make_pair(
              constant_pc_offset,
              RecordReference(nullptr, info_storage_.back().get())));
#endif
        }
      }
      int pc_offset =
          std::distance(reinterpret_cast<const uint8_t*>(code_start),
                        reinterpret_cast<const uint8_t*>(instruction_pointer));
      info_storage_.emplace_back(std::move(to_push));
#if defined(UC_3_0)
      record_reference_map_.emplace(
          pc_offset, RecordReference(nullptr, info_storage_.back().get()));
#else
      record_reference_map_.insert(std::make_pair(
          pc_offset, RecordReference(nullptr, info_storage_.back().get())));
#endif
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

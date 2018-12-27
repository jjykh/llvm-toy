#include "src/llvm/tf/v8-codegen.h"

#include "src/llvm/compiler-state.h"
#include "src/llvm/stack-maps.h"

#include "src/arm/assembler-arm-inl.h"
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
  int HandleHeapConstant(const HeapConstantInfo*, const StackMaps::Record&);
  int HandleExternalReference(const ExternalReferenceInfo*,
                              const StackMaps::Record&);
  int HandleCall(const CallInfo*, const StackMaps::Record&);
  int HandleStackMapInfo(const StackMapInfo* stack_map_info,
                         const StackMaps::Record& record);
  void ProcessRecordMap(const StackMaps::RecordMap& rm,
                        const CompilerState& state, const uint32_t* code_start);

  static void AdjustInstructionOffset(uint32_t*, const uint32_t*,
                                      const StackMaps::Record&, int64_t magic);
  static bool IsMove(uint32_t code, uint32_t* location, uint32_t* imm);

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
  int slot_count_ = 0;
  bool needs_frame_ = false;
};

CodeGeneratorLLVM::CodeGeneratorLLVM(Isolate* isolate)
    : isolate_(isolate),
      zone_(isolate->allocator(), "llvm"),
      masm_(isolate, nullptr, 0, CodeObjectRequired::kYes),
      safepoint_table_builder_(&zone_) {}

int CodeGeneratorLLVM::HandleHeapConstant(const HeapConstantInfo* heap_info,
                                          const StackMaps::Record& record) {
  Handle<HeapObject> heap_object =
      handle(reinterpret_cast<HeapObject*>(heap_info->magic()), isolate_);
  CHECK(record.locations.size() == 1);
  auto& location = record.locations[0];
  CHECK(location.kind == StackMaps::Location::Register);
  masm_.mov(Register::from_code(location.dwarfReg), Operand(heap_object));
  return 1;
}

int CodeGeneratorLLVM::HandleExternalReference(
    const ExternalReferenceInfo* external_info,
    const StackMaps::Record& record) {
  ExternalReference* external_reference =
      reinterpret_cast<ExternalReference*>(external_info->magic());
  CHECK(record.locations.size() == 1);
  auto& location = record.locations[0];
  CHECK(location.kind == StackMaps::Location::Register);
  masm_.mov(Register::from_code(location.dwarfReg),
            Operand(*external_reference));
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

int CodeGeneratorLLVM::HandleStackMapInfo(const StackMapInfo* stack_map_info,
                                          const StackMaps::Record& record) {
  switch (stack_map_info->GetType()) {
    case StackMapInfoType::kHeapConstant:
      return HandleHeapConstant(
          static_cast<const HeapConstantInfo*>(stack_map_info), record);
    case StackMapInfoType::kExternalReference:
      return HandleExternalReference(
          static_cast<const ExternalReferenceInfo*>(stack_map_info), record);
    case StackMapInfoType::kCallInfo:
      return HandleCall(static_cast<const CallInfo*>(stack_map_info), record);
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

  const uint32_t* BytesP = reinterpret_cast<const uint32_t*>(code.data());
  unsigned NumBytes = code.size();
  ProcessRecordMap(rm, state, BytesP);
  int spill_slot_count = state.spill_slot_count_;

  slot_count_ = spill_slot_count + 3;  // marker, fp, pc
  int incremental = 0;
  if (needs_frame_) masm_.StubPrologue(StackFrame::STUB);
  int base_offset = masm_.pc_offset();
  {
    Assembler::BlockConstPoolScope block_const_pool(&masm_);

    for (; NumBytes;
         NumBytes -= incremental * sizeof(uint32_t), BytesP += incremental) {
      int pc_offset = masm_.pc_offset() - base_offset;
      CHECK((pc_offset + NumBytes) == code.size());
      auto found = record_reference_map_.find(pc_offset);
      if (found != record_reference_map_.end()) {
        auto& record_reference = found->second;
        incremental =
            HandleStackMapInfo(record_reference.info, *record_reference.record);
        continue;
      }
      incremental = 1;
      uint32_t instruction = *BytesP;
      masm_.dd(instruction);
    }
  }
  masm_.CheckConstPool(true, false);
  record_reference_map_.clear();
  safepoint_table_builder_.Emit(&masm_, slot_count_);
  CodeDesc desc;
  masm_.GetCode(isolate_, &desc);
  Handle<Code> new_object = isolate_->factory()->NewCode(
      desc, Code::BYTECODE_HANDLER, masm_.CodeObject());
  new_object->set_stack_slots(slot_count_);
  new_object->set_safepoint_table_offset(
      safepoint_table_builder_.GetCodeOffset());
  new_object->set_is_turbofanned(true);
  new_object->Print();
  return new_object;
}

void CodeGeneratorLLVM::ProcessRecordMap(const StackMaps::RecordMap& rm,
                                         const CompilerState& state,
                                         const uint32_t* code_start) {
  for (auto& item : rm) {
    CHECK(item.second.size() == 1);
    auto& record = item.second.front();
    uint32_t instruction_offset = item.first;
    auto stack_map_info_found =
        state.stack_map_info_map.find(record.patchpointID);
    CHECK(stack_map_info_found != state.stack_map_info_map.end());
    const StackMapInfo* stack_map_info = stack_map_info_found->second.get();
    switch (stack_map_info->GetType()) {
      case StackMapInfoType::kHeapConstant:
        AdjustInstructionOffset(
            &instruction_offset, code_start, record,
            static_cast<const HeapConstantInfo*>(stack_map_info)->magic());
        break;
      case StackMapInfoType::kExternalReference:
        AdjustInstructionOffset(
            &instruction_offset, code_start, record,
            static_cast<const ExternalReferenceInfo*>(stack_map_info)->magic());
        break;
      case StackMapInfoType::kCallInfo:
        break;
      default:
        UNREACHABLE();
    }
    record_reference_map_.emplace(instruction_offset,
                                  RecordReference(&record, stack_map_info));
  }
}

void CodeGeneratorLLVM::AdjustInstructionOffset(uint32_t* instruction_offset,
                                                const uint32_t* code_start,
                                                const StackMaps::Record& record,
                                                int64_t magic) {
  uint32_t current_offset = *instruction_offset;
  const int kMaxTries = 2000;
  uint32_t target_reg = record.locations[0].dwarfReg;
  for (int i = 0; i < kMaxTries; ++i, current_offset -= sizeof(uint32_t)) {
    uint32_t code = code_start[current_offset / sizeof(uint32_t)];
    uint32_t location;
    uint32_t imm;
    if (IsMove(code, &location, &imm) && (location == target_reg) &&
        (imm == (magic & 0xff))) {
      *instruction_offset = current_offset;
      return;
    }
  }
  UNREACHABLE();
}

bool CodeGeneratorLLVM::IsMove(uint32_t code, uint32_t* location,
                               uint32_t* imm) {
  if (((code & ~0xffff) ^ 0xe3a00000) == 0) {
    *location = (code >> 12) & 0xf;
    *imm = code & 0xfff;
    return true;
  }
  return false;
}

CodeGeneratorLLVM::RecordReference::RecordReference(
    const StackMaps::Record* _record, const StackMapInfo* _info)
    : record(_record), info(_info) {}
}

Handle<Code> GenerateCode(Isolate* isolate, const CompilerState& state) {
  HandleScope handle_scope(isolate);
  CodeGeneratorLLVM code_generator(isolate);
  return handle_scope.CloseAndEscape(code_generator.Generate(state));
}
}
}
}

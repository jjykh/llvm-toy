// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/stack-maps.h"
#include <sstream>

namespace v8 {
namespace internal {
namespace tf_llvm {

template <typename T>
T readObject(StackMaps::ParseContext& context) {
  T result;
  result.parse(context);
  return result;
}

void StackMaps::Constant::parse(StackMaps::ParseContext& context) {
  integer = context.view->read<int64_t>(context.offset, true);
}

void StackMaps::StackSize::parse(StackMaps::ParseContext& context) {
  switch (context.version) {
    case 0:
      functionOffset = context.view->read<uint32_t>(context.offset, true);
      size = context.view->read<uint32_t>(context.offset, true);
      callSiteCount = -1;
      break;

    case 1:
    case 2:
      functionOffset = context.view->read<uintptr_t>(context.offset, true);
      size = context.view->read<uint64_t>(context.offset, true);
      callSiteCount = -1;
      break;
    case 3:
      functionOffset = context.view->read<uintptr_t>(context.offset, true);
      size = context.view->read<uint64_t>(context.offset, true);
      callSiteCount = context.view->read<uint64_t>(context.offset, true);
      break;
  }
}

void StackMaps::Location::parse(StackMaps::ParseContext& context) {
  kind = static_cast<Kind>(context.view->read<uint8_t>(context.offset, true));
  context.view->read<uint8_t>(context.offset, true);  // reserved
  size = context.view->read<uint16_t>(context.offset, true);
  dwarfReg = DWARFRegister(context.view->read<uint16_t>(context.offset, true));
  context.view->read<uint16_t>(context.offset, true);  // reserved
  this->offset = context.view->read<int32_t>(context.offset, true);
}

void StackMaps::LiveOut::parse(StackMaps::ParseContext& context) {
  dwarfReg = DWARFRegister(
      context.view->read<uint16_t>(context.offset, true));   // regnum
  context.view->read<uint8_t>(context.offset, true);         // reserved
  size = context.view->read<uint8_t>(context.offset, true);  // size in bytes
}

bool StackMaps::Record::parse(StackMaps::ParseContext& context) {
  int64_t id = context.view->read<int64_t>(context.offset, true);
  EMASSERT(static_cast<int32_t>(id) == id);
  patchpointID = static_cast<uint32_t>(id);
  if (static_cast<int32_t>(patchpointID) < 0) return false;

  instructionOffset = context.view->read<uint32_t>(context.offset, true);
  flags = context.view->read<uint16_t>(context.offset, true);

  unsigned length = context.view->read<uint16_t>(context.offset, true);
  while (length--) locations.push_back(readObject<Location>(context));

  if (context.version >= 1) {
    while (context.offset & 7)
      context.view->read<uint16_t>(context.offset, true);  // padding
  }
  // liveout padding
  context.view->read<uint16_t>(context.offset, true);
  unsigned numLiveOuts = context.view->read<uint16_t>(context.offset, true);
  while (numLiveOuts--) liveOuts.push_back(readObject<LiveOut>(context));

  if (context.version >= 1) {
    while (context.offset & 7) {
      context.view->read<uint16_t>(context.offset, true);  // padding
    }
  }

  return true;
}

RegisterSet StackMaps::Record::locationSet() const {
  RegisterSet result;
  for (unsigned i = locations.size(); i--;) {
    DWARFRegister reg = locations[i].dwarfReg;
    result.set(reg);
  }
  return result;
}

RegisterSet StackMaps::Record::liveOutsSet() const {
  RegisterSet result;
  for (unsigned i = liveOuts.size(); i--;) {
    LiveOut liveOut = liveOuts[i];
    DWARFRegister reg = liveOut.dwarfReg;
    result.set(reg);
  }
  return result;
}

static void merge(RegisterSet& dst, const RegisterSet& input) {
  for (size_t i = 0; i < input.size(); ++i) {
    dst.set(i, dst.test(i) ^ input.test(i) ^ dst.test(i));
  }
}

RegisterSet StackMaps::Record::usedRegisterSet() const {
  RegisterSet result;
  merge(result, locationSet());
  merge(result, liveOutsSet());
  return result;
}

bool StackMaps::parse(DataView* view) {
  ParseContext context;
  context.offset = 0;
  context.view = view;

  version = context.version = context.view->read<uint8_t>(context.offset, true);

  context.view->read<uint8_t>(context.offset, true);  // Reserved
  context.view->read<uint8_t>(context.offset, true);  // Reserved
  context.view->read<uint8_t>(context.offset, true);  // Reserved

  uint32_t numFunctions;
  uint32_t numConstants;
  uint32_t numRecords;

  numFunctions = context.view->read<uint32_t>(context.offset, true);
  if (context.version >= 1) {
    numConstants = context.view->read<uint32_t>(context.offset, true);
    numRecords = context.view->read<uint32_t>(context.offset, true);
  }
  while (numFunctions--) stackSizes.push_back(readObject<StackSize>(context));

  if (!context.version)
    numConstants = context.view->read<uint32_t>(context.offset, true);
  while (numConstants--) constants.push_back(readObject<Constant>(context));

  if (!context.version)
    numRecords = context.view->read<uint32_t>(context.offset, true);
  while (numRecords--) {
    Record record;
    if (!record.parse(context)) return false;
    records.push_back(record);
  }

  return true;
}

StackMaps::RecordMap StackMaps::computeRecordMap() const {
  RecordMap result;
  for (unsigned i = records.size(); i--;)
    result
        .insert(
            std::make_pair(records[i].instructionOffset, std::vector<Record>()))
        .first->second.push_back(records[i]);
  return result;
}

unsigned StackMaps::stackSize() const {
  EMASSERT(stackSizes.size() == 1);

  return stackSizes[0].size;
}

static std::string LocationKind(const StackMaps::Location& location) {
  switch (location.kind) {
    case StackMaps::Location::Register:
      return "Register";
    case StackMaps::Location::Direct:
      return "Direct";
    case StackMaps::Location::Indirect:
      return "Indirect";
    case StackMaps::Location::Constant:
      return "Constant";
    case StackMaps::Location::ConstantIndex:
      return "ConstantIndex";
  }
  __builtin_trap();
}

std::string StackMaps::RecordMapToString(const RecordMap& rm) {
  std::ostringstream oss;
  for (auto& item : rm) {
    auto& records = item.second;
    for (auto& r : records) {
      oss << "Patchpoint ID: " << r.patchpointID << "\n"
          << "Instruction Offset: " << r.instructionOffset << "\n";
      for (auto& location : r.locations) {
        oss << "Location: " << LocationKind(location) << "\n"
            << "Size: " << static_cast<int>(location.size) << "\n"
            << "Reg: " << location.dwarfReg << "\n"
            << "Offset: " << location.offset << "\n";
      }
    }
  }
  return oss.str();
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

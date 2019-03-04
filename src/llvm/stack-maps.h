#ifndef STACK_MAPS_H
#define STACK_MAPS_H
#include <assert.h>
#include <stdint.h>
#include <bitset>
#include <unordered_map>
#include <vector>
#include "src/llvm/log.h"
namespace v8 {
namespace internal {
namespace tf_llvm {
typedef uint16_t DWARFRegister;

class DataView {
 public:
  explicit DataView(const uint8_t* data) : data_(data) {}
  template <typename T>
  T read(unsigned& off, bool littenEndian) {
    EMASSERT(littenEndian == true);
    T t = *reinterpret_cast<const T*>(data_ + off);
    off += sizeof(T);
    return t;
  }

 protected:
  const uint8_t* data_;
};

// lower 32 for gerneral purpose register
// upper 32 for fp register
typedef std::bitset<64> RegisterSet;

struct StackMaps {
  struct ParseContext {
    unsigned version;
    DataView* view;
    unsigned offset;
  };

  struct Constant {
    int64_t integer;

    void parse(ParseContext&);
  };

  struct StackSize {
    uint64_t functionOffset;
    uint64_t size;
    uint64_t callSiteCount;

    void parse(ParseContext&);
  };

  struct Location {
    enum Kind : int8_t {
      Unprocessed,
      Register,
      Direct,
      Indirect,
      Constant,
      ConstantIndex
    };

    DWARFRegister dwarfReg;
    uint8_t size;
    Kind kind;
    int32_t offset;

    void parse(ParseContext&);
  };

  // FIXME: Investigate how much memory this takes and possibly prune it from
  // the format we keep around in FTL::JITCode. I suspect that it would be most
  // awesome to have a CompactStackMaps struct that lossily stores only that
  // subset of StackMaps and Record that we actually need for OSR exit.
  // https://bugs.webkit.org/show_bug.cgi?id=130802
  struct LiveOut {
    DWARFRegister dwarfReg;
    uint8_t size;

    void parse(ParseContext&);
  };

  struct Record {
    uint32_t patchpointID;
    uint32_t instructionOffset;
    uint16_t flags;

    std::vector<Location> locations;
    std::vector<LiveOut> liveOuts;

    bool parse(ParseContext&);
    RegisterSet liveOutsSet() const;
    RegisterSet locationSet() const;
    RegisterSet usedRegisterSet() const;
  };

  unsigned version;
  std::vector<StackSize> stackSizes;
  std::vector<Constant> constants;
  std::vector<Record> records;

  bool parse(
      DataView*);  // Returns true on parse success, false on failure. Failure
                   // means that LLVM is signaling compile failure to us.

  typedef std::unordered_map<uint32_t, std::vector<Record> > RecordMap;

  RecordMap computeRecordMap() const;
  static std::string RecordMapToString(const RecordMap& rm);

  unsigned stackSize() const;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // STACK_MAPS_H

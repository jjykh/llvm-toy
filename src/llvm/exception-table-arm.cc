// Copyright 2019 UCWeb Co., Ltd.

#include "src/llvm/exception-table-arm.h"

#include "src/llvm/llvm-log.h"
#include "src/llvm/stack-maps.h"
namespace v8 {
namespace internal {
namespace tf_llvm {
namespace {
/// Utility function to decode a ULEB128 value.
inline uint64_t decodeULEB128(const uint8_t* p, unsigned* n = nullptr,
                              const uint8_t* end = nullptr,
                              const char** error = nullptr) {
  const uint8_t* orig_p = p;
  uint64_t Value = 0;
  unsigned Shift = 0;
  if (error) *error = nullptr;
  do {
    if (end && p == end) {
      if (error) *error = "malformed uleb128, extends past end";
      if (n) *n = (unsigned)(p - orig_p);
      return 0;
    }
    uint64_t Slice = *p & 0x7f;
    if (Shift >= 64 || Slice << Shift >> Shift != Slice) {
      if (error) *error = "uleb128 too big for uint64";
      if (n) *n = (unsigned)(p - orig_p);
      return 0;
    }
    Value += uint64_t(*p & 0x7f) << Shift;
    Shift += 7;
  } while (*p++ >= 128);
  if (n) *n = (unsigned)(p - orig_p);
  return Value;
}

class DataViewULEB128 : public DataView {
 public:
  DataViewULEB128(const uint8_t* data) : DataView(data) {}
  ~DataViewULEB128() = default;
  uint64_t ReadULEB128(unsigned& offset, const uint8_t* end) {
    const char* error;
    unsigned n;
    uint64_t result = decodeULEB128(data_ + offset, &n, end, &error);
    if (error) {
      LOGE("decodeULEB128: %s\n", error);
      EMASSERT(!error);
    }
    offset += n;
    return result;
  }
};
}  // namespace
ExceptionTableARM::ExceptionTableARM(const uint8_t* content, size_t length) {
  unsigned offset = 0;
  const uint8_t* end = content + length;
  DataViewULEB128 view(content);
  // omit first two words
  view.read<int32_t>(offset, true);
  view.read<int32_t>(offset, true);
  static const uint8_t kDW_EH_PE_omit = 0xff;
  static const uint8_t kDW_EH_PE_uleb128 = 0x01;
  uint8_t lp_start = view.read<uint8_t>(offset, true);
  uint8_t ttype = view.read<uint8_t>(offset, true);
  uint8_t callsite_encoding = view.read<uint8_t>(offset, true);
  EMASSERT(lp_start == kDW_EH_PE_omit);
  EMASSERT(ttype == kDW_EH_PE_omit);
  EMASSERT(callsite_encoding == kDW_EH_PE_uleb128);
  uint64_t landing_pad_size = view.ReadULEB128(offset, end);
  EMASSERT(offset + landing_pad_size <= length);
  // Update end.
  end = content + offset + landing_pad_size;
  while (content + offset < end) {
    uint64_t call_begin = view.ReadULEB128(offset, end);
    uint64_t call_length = view.ReadULEB128(offset, end);
    uint64_t landing_pad = view.ReadULEB128(offset, end);
    uint64_t action = view.ReadULEB128(offset, end);
    EMASSERT(action == 0);  // Only allows cleanup.
    if (landing_pad) {
      records_.emplace_back(call_begin + call_length, landing_pad);
    }
  }
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

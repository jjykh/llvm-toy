#include "src/llvm/tf/tf-parser.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

namespace v8 {
namespace internal {
namespace tf_llvm {
#define HANDLER_INITIALIZE(name, ignored) {#name, &TFParser::Handle##name},

TFParser::TFParser(TFVisitor* visitor)
    : visitor_(visitor),
      state_(State::ParsingBlockHeader),
      handlers_map_({INSTRUCTIONS(HANDLER_INITIALIZE)}),
      rep_map_({
          {"kMachNone", MachineRepresentation::kNone},
          {"kRepBit", MachineRepresentation::kBit},
          {"kRepWord8", MachineRepresentation::kWord8},
          {"kRepWord16", MachineRepresentation::kWord16},
          {"kRepWord32", MachineRepresentation::kWord32},
          {"kRepWord64", MachineRepresentation::kWord64},
          {"kRepFloat32", MachineRepresentation::kFloat32},
          {"kRepFloat64", MachineRepresentation::kFloat64},
          {"kRepSimd128", MachineRepresentation::kSimd128},
          {"kRepTaggedSigned", MachineRepresentation::kTaggedSigned},
          {"kRepTaggedPointer", MachineRepresentation::kTaggedPointer},
          {"kRepTagged", MachineRepresentation::kTagged},
      }),
      semantic_map_({
          {"kMachNone", MachineSemantic::kNone},
          {"kTypeBool", MachineSemantic::kBool},
          {"kTypeInt32", MachineSemantic::kInt32},
          {"kTypeUint32", MachineSemantic::kUint32},
          {"kTypeInt64", MachineSemantic::kInt64},
          {"kTypeUint64", MachineSemantic::kUint64},
          {"kTypeNumber", MachineSemantic::kNumber},
          {"kTypeAny", MachineSemantic::kAny},
      }),
      write_barrier_map_({
          {"NoWriteBarrier", kNoWriteBarrier},
          {"MapWriteBarrier", kMapWriteBarrier},
          {"PointerWriteBarrier", kPointerWriteBarrier},
          {"FullWriteBarrier", kFullWriteBarrier},
      }) {}
#undef HANDLER_INITIALIZE

TFParser::~TFParser() {}

void TFParser::Parse(FILE* f) {
  char buf[256];
  line_no_ = 0;
  while (fgets(buf, 256, f)) {
    line_no_++;
    switch (state_) {
      case State::ParsingBlockHeader:
        ParseBlockHeader(buf);
        break;
      case State::ParsingInstructions:
        ParseInstructions(buf);
        break;
    }
  }
}

static bool SkipPattern(const char*& input, const char* pattern,
                        size_t pattern_size) {
  if (!memcmp(input, pattern, pattern_size)) {
    input += pattern_size;
    return true;
  }
  return false;
}

void TFParser::ParseBlockHeader(const char* line) {
  OperandsVector predecessors;
  int scanned;
  int bid;
  if (1 != sscanf(line, "--- BLOCK B%d%n", &bid, &scanned))
    ParserError("Block Header bid");
  line += scanned;
  // parse for predecessors
  static const char kDeferred[] = " (deferred)";
  bool is_deferred = false;
  if (SkipPattern(line, kDeferred, sizeof(kDeferred) - 1)) is_deferred = true;
  static const char kArrow[] = " <- ";
  if (SkipPattern(line, kArrow, sizeof(kArrow) - 1)) {
    do {
      int predecessor;
      if (1 != sscanf(line, "B%d%n\n", &predecessor, &scanned))
        ParserError("Block predecessor");
      predecessors.push_back(predecessor);
      line += scanned;
      if (line[0] == ',' && line[1] == ' ' && line[2] == 'B')
        line += 2;
      else
        break;
    } while (true);
  }
  visitor_->VisitBlock(bid, is_deferred, predecessors);
  state_ = State::ParsingInstructions;
}

void TFParser::ParseInstructions(const char* line) {
  // the end of '\n' will not count
  const char* line_end = line + strlen(line) - 1;
  while (isspace(*line)) ++line;
  if (line[0] == 'G') {
    int bid;
    if (!sscanf(line, "Goto -> B%d", &bid)) ParserError("Goto");
    visitor_->VisitGoto(bid);
    state_ = State::ParsingBlockHeader;
    return;
  }
  int id;
  int scanned;
  if (1 != sscanf(line, "%d:%n", &id, &scanned))
    ParserError("ParseInstructions id");
  // one more for the space
  line += scanned + 1;
  std::string mnemonic;
  std::string properties;
  OperandsVector operands;
  for (; isalnum(*line); ++line) {
    mnemonic.append(1, *line);
  }
  if (*line == '[') {
    // properties
    line++;
    int recursive = 1;
    for (;; ++line) {
      if (*line == '[') recursive++;
      if (*line == ']') recursive--;
      if (recursive == 0) break;
      properties.append(1, *line);
    }
    line++;
  }
  if (line == line_end) goto FindHandler;
  if (*line != '(') ParserError("expecting inputs");
  line++;
  do {
    int input;
    if (1 != sscanf(line, "%d%n", &input, &scanned))
      ParserError("unexpected for input");
    operands.push_back(input);
    line += scanned;
    if (*line == ')') {
      ++line;
      break;
    }
    if (line[0] != ',' || line[1] != ' ') ParserError("unexpected for input");
    line += 2;
  } while (true);
  if (line != line_end && mnemonic != "TailCall") {
    // try branch
    static const char kBranchTag[] = " -> ";
    if (memcmp(line, kBranchTag, sizeof(kBranchTag) - 1))
      ParserError("unexpected for branch");
    line += sizeof(kBranchTag) - 1;
    int btrue, bfalse;
    if (2 != sscanf(line, "B%d, B%d", &btrue, &bfalse))
      ParserError("expect 2 element for a branch");
    visitor_->VisitBranch(id, operands[0], btrue, bfalse);
    state_ = State::ParsingBlockHeader;
    return;
  }
FindHandler:
  auto handler_found = handlers_map_.find(mnemonic);
  if (handler_found == handlers_map_.end())
    ParserError("unknown handler_found for mnemonic %s", mnemonic.c_str());
  (this->*(handler_found->second))(id, properties, operands);
}

void TFParser::HandleParameter(int id, const std::string& properties,
                               const OperandsVector& operands) {
  int where;
  if (1 != sscanf(properties.c_str(), "%d", &where)) {
    ParserError("failed to extract parameter location");
  }
  visitor_->VisitParameter(id, where);
}

void TFParser::HandleLoadParentFramePointer(int id,
                                            const std::string& properties,
                                            const OperandsVector& operands) {
  visitor_->VisitLoadParentFramePointer(id);
}

void TFParser::HandleLoadFramePointer(int id, const std::string& properties,
                                      const OperandsVector& operands) {
  visitor_->VisitLoadFramePointer(id);
}

void TFParser::HandleLoadStackPointer(int id, const std::string& properties,
                                      const OperandsVector& operands) {
  visitor_->VisitLoadStackPointer(id);
}

void TFParser::HandleDebugBreak(int id, const std::string& properties,
                                const OperandsVector& operands) {
  visitor_->VisitDebugBreak(id);
}

void TFParser::HandleInt32Constant(int id, const std::string& properties,
                                   const OperandsVector& operands) {
  int constant;
  if (1 != sscanf(properties.c_str(), "%d", &constant)) {
    ParserError("failed to extract int32 constant");
  }
  visitor_->VisitInt32Constant(id, constant);
}

void TFParser::HandleFloat64SilenceNaN(int id, const std::string& properties,
                                       const OperandsVector& operands) {
  visitor_->VisitInt32Constant(id, operands[0]);
}

void TFParser::HandleIdentity(int id, const std::string& properties,
                              const OperandsVector& operands) {
  visitor_->VisitIdentity(id, operands[0]);
}

std::tuple<MachineRepresentation, MachineSemantic>
TFParser::ParseAccessProperties(const std::string& properties) {
  const std::unordered_map<std::string, MachineRepresentation>& rep_map =
      rep_map_;
  const std::unordered_map<std::string, MachineSemantic> semantic_map =
      semantic_map_;
  MachineRepresentation rep = MachineRepresentation::kNone;
  MachineSemantic semantic = MachineSemantic::kNone;
  // at most two elements
  auto iterator = properties.begin();
  for (int i = 0; i < 2 && iterator != properties.end(); ++i) {
    auto end = iterator;
    std::string word;
    while (isalnum(*end) && (end != properties.end())) ++end;
    word.assign(iterator, end);
    if (end != properties.end()) {
      if (*end != '|') ParserError("expected '|' for access property");
      ++end;
    }
    auto rep_found = rep_map.find(word);
    if (rep_found != rep_map.end()) {
      rep = rep_found->second;
    } else {
      auto semantic_found = semantic_map.find(word);
      if (semantic_found == semantic_map.end())
        ParserError("can not identify the word %s for access property",
                    properties.c_str());
      semantic = semantic_found->second;
    }
    if (end == properties.end()) break;
    iterator = end;
  }
  return std::make_tuple(rep, semantic);
}

void TFParser::HandleLoad(int id, const std::string& properties,
                          const OperandsVector& operands) {
  MachineRepresentation rep;
  MachineSemantic sematic;
  std::tie(rep, sematic) = ParseAccessProperties(properties);
  if (2 != operands.size()) ParserError("wrong operands number for Load");
  visitor_->VisitLoad(id, rep, sematic, operands[0], operands[1]);
}

void TFParser::HandleStore(int id, const std::string& properties,
                           const OperandsVector& operands) {
  if (3 != operands.size()) ParserError("wrong operands number for Load");
  char* rep_string;
  char* write_barrier_string;
  if (2 != sscanf(properties.c_str(), "(%ms : %ms)", &rep_string,
                  &write_barrier_string))
    ParserError("unexpected store properties %s", properties.c_str());
  char* maybe_parenthesis = strstr(write_barrier_string, ")");
  if (maybe_parenthesis) *maybe_parenthesis = '\0';
  MachineRepresentation rep;
  WriteBarrierKind write_barrier;
  auto rep_found = rep_map_.find(rep_string);
  if (rep_found == rep_map_.end())
    ParserError("unexpected rep for store: %s\n", rep_string);
  rep = rep_found->second;
  auto write_barrier_found = write_barrier_map_.find(write_barrier_string);
  if (write_barrier_found == write_barrier_map_.end())
    ParserError("unexpected write_barrier for store: %s\n",
                write_barrier_string);
  write_barrier = write_barrier_found->second;
  free(rep_string);
  free(write_barrier_string);
  visitor_->VisitStore(id, rep, write_barrier, operands[0], operands[1],
                       operands[2]);
}

void TFParser::HandleBitcastWordToTagged(int id, const std::string& properties,
                                         const OperandsVector& operands) {
  if (1 != operands.size())
    ParserError("wrong operands number for BitcastWordToTagged");
  visitor_->VisitBitcastWordToTagged(id, operands[0]);
}

void TFParser::HandleChangeInt32ToFloat64(int id, const std::string& properties,
                                          const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleChangeFloat32ToFloat64(int id,
                                            const std::string& properties,
                                            const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleChangeUint32ToFloat64(int id,
                                           const std::string& properties,
                                           const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleTruncateFloat64ToWord32(int id,
                                             const std::string& properties,
                                             const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleTruncateFloat64ToFloat32(int id,
                                              const std::string& properties,
                                              const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleRoundFloat64ToInt32(int id, const std::string& properties,
                                         const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64ExtractHighWord32(int id,
                                              const std::string& properties,
                                              const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleRoundInt32ToFloat32(int id, const std::string& properties,
                                         const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleInt32Add(int id, const std::string& properties,
                              const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Int32Add");
  visitor_->VisitInt32Add(id, operands[0], operands[1]);
}

void TFParser::HandleFloat64Add(int id, const std::string& properties,
                                const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Float64Add");
  visitor_->VisitFloat64Add(id, operands[0], operands[1]);
}

void TFParser::HandleFloat64Sub(int id, const std::string& properties,
                                const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Float64Sub");
  visitor_->VisitFloat64Sub(id, operands[0], operands[1]);
}

void TFParser::HandleFloat64Mul(int id, const std::string& properties,
                                const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Float64Mul");
  visitor_->VisitFloat64Mul(id, operands[0], operands[1]);
}

void TFParser::HandleInt32AddWithOverflow(int id, const std::string& properties,
                                          const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Int32Add");
  visitor_->VisitInt32AddWithOverflow(id, operands[0], operands[1]);
}

void TFParser::HandleInt32SubWithOverflow(int id, const std::string& properties,
                                          const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Int32Sub");
  visitor_->VisitInt32SubWithOverflow(id, operands[0], operands[1]);
}

void TFParser::HandleInt32MulWithOverflow(int id, const std::string& properties,
                                          const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Int32Mul");
  visitor_->VisitInt32MulWithOverflow(id, operands[0], operands[1]);
}

void TFParser::HandleInt32Mul(int id, const std::string& properties,
                              const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Int32Mul");
  visitor_->VisitInt32Mul(id, operands[0], operands[1]);
}

void TFParser::HandleInt32Sub(int id, const std::string& properties,
                              const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Int32Sub");
  visitor_->VisitInt32Sub(id, operands[0], operands[1]);
}

void TFParser::HandleWord32Shl(int id, const std::string& properties,
                               const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Word32Shl");
  visitor_->VisitWord32Shl(id, operands[0], operands[1]);
}

void TFParser::HandleWord32Xor(int id, const std::string& properties,
                               const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Word32Xor");
  visitor_->VisitWord32Xor(id, operands[0], operands[1]);
}

void TFParser::HandleWord32Shr(int id, const std::string& properties,
                               const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Word32Shr");
  visitor_->VisitWord32Shr(id, operands[0], operands[1]);
}

void TFParser::HandleWord32Sar(int id, const std::string& properties,
                               const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Word32Sar");
  visitor_->VisitWord32Sar(id, operands[0], operands[1]);
}

void TFParser::HandleWord32Mul(int id, const std::string& properties,
                               const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Word32Mul");
  visitor_->VisitWord32Mul(id, operands[0], operands[1]);
}

void TFParser::HandleWord32And(int id, const std::string& properties,
                               const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Word32And");
  visitor_->VisitWord32And(id, operands[0], operands[1]);
}

void TFParser::HandleWord32Or(int id, const std::string& properties,
                              const OperandsVector& operands) {
  if (2 != operands.size()) ParserError("wrong operands number for Word32Or");
  visitor_->VisitWord32Or(id, operands[0], operands[1]);
}

void TFParser::HandleWord32Equal(int id, const std::string& properties,
                                 const OperandsVector& operands) {
  if (2 != operands.size())
    ParserError("wrong operands number for Word32Equal");
  visitor_->VisitWord32Equal(id, operands[0], operands[1]);
}

void TFParser::HandleInt32LessThanOrEqual(int id, const std::string& properties,
                                          const OperandsVector& operands) {
  if (2 != operands.size())
    ParserError("wrong operands number for Int32LessThanOrEqual");
  visitor_->VisitInt32LessThanOrEqual(id, operands[0], operands[1]);
}

void TFParser::HandleInt32LessThan(int id, const std::string& properties,
                                   const OperandsVector& operands) {
  if (2 != operands.size())
    ParserError("wrong operands number for Int32LessThan");
  visitor_->VisitInt32LessThan(id, operands[0], operands[1]);
}

void TFParser::HandleUint32LessThanOrEqual(int id,
                                           const std::string& properties,
                                           const OperandsVector& operands) {
  if (2 != operands.size())
    ParserError("wrong operands number for Uint32LessThanOrEqual");
  visitor_->VisitUint32LessThanOrEqual(id, operands[0], operands[1]);
}

void TFParser::HandleUint32LessThan(int id, const std::string& properties,
                                    const OperandsVector& operands) {
  if (2 != operands.size())
    ParserError("wrong operands number for Uint32LessThan");
  visitor_->VisitUint32LessThan(id, operands[0], operands[1]);
}

void TFParser::HandleBranch(int id, const std::string& properties,
                            const OperandsVector& operands) {
  // handle by other path.
  __builtin_unreachable();
}

void TFParser::HandleSwitch(int id, const std::string& properties,
                            const OperandsVector& operands) {
  // handle by other path.
  __builtin_unreachable();
}

void TFParser::HandleIfValue(int id, const std::string& properties,
                             const OperandsVector& operands) {
  // handle by other path.
  __builtin_unreachable();
}

void TFParser::HandleIfDefault(int id, const std::string& properties,
                               const OperandsVector& operands) {
  // handle by other path.
  __builtin_unreachable();
}

void TFParser::HandleHeapConstant(int id, const std::string& properties,
                                  const OperandsVector& operands) {
  int64_t magic;
  if (1 != sscanf(properties.c_str(), "0x%" SCNx64, &magic))
    ParserError("unexpected properties for heap constant magic: %s",
                properties.c_str());
  visitor_->VisitHeapConstant(id, magic);
}

void TFParser::HandleExternalConstant(int id, const std::string& properties,
                                      const OperandsVector& operands) {
  int64_t magic;
  if (1 != sscanf(properties.c_str(), "0x%" SCNx64, &magic))
    ParserError("unexpected properties for heap constant magic: %s",
                properties.c_str());
  visitor_->VisitExternalConstant(id, magic);
}

void TFParser::HandlePhi(int id, const std::string& properties,
                         const OperandsVector& operands) {
  MachineRepresentation rep;
  auto rep_found = rep_map_.find(properties);
  if (rep_found == rep_map_.end())
    ParserError("unexpected MachineRepresentation %s when handle phi",
                properties.c_str());
  rep = rep_found->second;
  visitor_->VisitPhi(id, rep, operands);
}

void TFParser::HandleCall(int id, const std::string& properties,
                          const OperandsVector& operands) {
  CallDescriptor calldesc;
  static const char kCode[] = "Code";
  static const char kAddr[] = "Addr";
  bool is_code = false;
  if (!memcmp(properties.c_str(), kCode, sizeof(kCode) - 1))
    is_code = true;
  else if (memcmp(properties.c_str(), kAddr, sizeof(kAddr) - 1))
    ParserError("unexpected property for call: %s\n", properties.c_str());
  visitor_->VisitCall(id, is_code, calldesc, operands);
}

void TFParser::HandleCallWithCallerSavedRegisters(
    int id, const OperandsVector& operands) {
  __builtin_unreachable();
}

void TFParser::HandleTailCall(int id, const std::string& properties,
                              const OperandsVector& operands) {
  CallDescriptor calldesc;
  static const char kCode[] = "Code";
  static const char kAddr[] = "Addr";
  bool is_code = false;
  if (!memcmp(properties.c_str(), kCode, sizeof(kCode) - 1)) is_code = true;
  if (memcmp(properties.c_str(), kAddr, sizeof(kAddr) - 1))
    ParserError("unexpected property for call: %s\n", properties.c_str());
  visitor_->VisitTailCall(id, is_code, calldesc, operands);
  state_ = State::ParsingBlockHeader;
}

void TFParser::HandleRoot(int, const std::string&, const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleCodeForCall(int, const std::string&,
                                 const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleSmiConstant(int, const std::string&,
                                 const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleReturn(int, const std::string&, const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleProjection(int, const std::string&,
                                const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleInt32Div(int, const std::string&, const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleInt32Mod(int, const std::string&, const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64Div(int, const std::string&,
                                const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64Mod(int, const std::string&,
                                const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64LessThan(int, const std::string&,
                                     const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64LessThanOrEqual(int, const std::string&,
                                            const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64Equal(int, const std::string&,
                                  const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64Neg(int, const std::string&,
                                const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64Abs(int, const std::string&,
                                const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::HandleFloat64Constant(int, const std::string&,
                                     const OperandsVector&) {
  __builtin_unreachable();
}

void TFParser::ParserError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int bytes = vsnprintf(nullptr, 0, fmt, ap);
  va_end(ap);
  char* msg = static_cast<char*>(alloca(bytes + 1));
  va_start(ap, fmt);
  vsnprintf(msg, bytes + 1, fmt, ap);
  va_end(ap);
  fprintf(stderr, "%d:%s\n", line_no_, msg);
  _exit(1);
}
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8

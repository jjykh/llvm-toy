#ifndef TFPARSER_H
#define TFPARSER_H
#include <stdio.h>
#include <string>
#include <unordered_map>
#include "tf-visitor.h"
class TFParser {
 public:
  TFParser(TFVisitor*);
  ~TFParser();
  void Parse(FILE* f);

 private:
  enum class State {
    ParsingBlockHeader,
    ParsingInstructions,
  };
  typedef void (TFParser::*PFNHandler)(int, const std::string&,
                                       const OperandsVector& operands);

  void ParseBlockHeader(const char*);
  void ParseInstructions(const char*);
  PFNHandler FindHandler(const std::string& mnemonic);
  void __attribute__((noreturn, __format__(__printf__, 2, 3)))
  ParserError(const char* fmt, ...);
  std::tuple<MachineRepresentation, MachineSemantic> ParseAccessProperties(
      const std::string&);
#define DECL_HANDLER(name, ignored) \
  void Handle##name(int, const std::string&, const OperandsVector&);
  INSTRUCTIONS(DECL_HANDLER)
#undef DECL_HANDLER
  TFVisitor* visitor_;
  State state_;
  std::unordered_map<std::string, PFNHandler> handlers_map_;
  std::unordered_map<std::string, MachineRepresentation> rep_map_;
  std::unordered_map<std::string, MachineSemantic> semantic_map_;
  std::unordered_map<std::string, WriteBarrierKind> write_barrier_map_;
  int line_no_;
};
#endif  // TFPARSER_H

#ifndef REGISTERS_H
#define REGISTERS_H
namespace jit {
enum AMD64 {
  RAX = 0,
  RCX = 1,
  RDX = 2,
  RSP = 4,
  RBP = 5,
  R11 = 11,
  RSI = 6,
  RDI = 7,
  R8 = 8,
  R9 = 9,
  R12 = 12,
  R13 = 13,
  R14 = 14,
  R15 = 15,
  RBX = 3,
};

class Reg {
 public:
  Reg(void) : val_(invalid()) {}
  Reg(int val) : val_(val) {}
  Reg(AMD64 val) : val_(val) {}
  int val() const { return val_; }
  bool isFloat() const { return isFloat_; }
  static inline int invalid() { return -1; }

 private:
  int val_;

 protected:
  bool isFloat_ = false;
};

class FPRReg : public Reg {
 public:
  FPRReg(int val) : Reg(val) { isFloat_ = true; }
};

class DWARFRegister {
 public:
  DWARFRegister() : dwarfRegNum_(-1) {}

  explicit DWARFRegister(int16_t dwarfRegNum) : dwarfRegNum_(dwarfRegNum) {}

  int16_t dwarfRegNum() const { return dwarfRegNum_; }
  Reg reg() const;

 private:
  int16_t dwarfRegNum_;
};
}  // namespace jit
#endif /* REGISTERS_H */

#ifndef BASIC_BLOCK_H
#define BASIC_BLOCK_H
#include <unordered_map>
#include <vector>
#include "output.h"
namespace jit {
class BasicBlock {
 public:
  explicit BasicBlock(int id, Output& output);
  ~BasicBlock();
  void startBuild(Output& output);
  void endBuild();
  void addPredecessor(BasicBlock* pred);
  inline bool started() const { return m_started; }
  inline bool ended() const { return m_ended; }
  inline LBasicBlock nativeBB() { return m_bb; }
  inline void setValue(int nid, LValue value) { m_values[nid] = value; }
  inline LValue value(int nid) {
    auto found = m_values.find(nid);
    assert(found != m_values.end());
    return found->second;
  }
  inline const std::vector<BasicBlock*>& predecessors() const {
    return m_predecessors;
  }
  inline std::vector<BasicBlock*>& predecessors() { return m_predecessors; }

 private:
  void mergePredecessors(Output* output);
  std::vector<BasicBlock*> m_predecessors;
  std::unordered_map<int, LValue> m_values;
  std::vector<int> m_liveins;
  LBasicBlock m_bb;
  int m_id;
  bool m_started;
  bool m_ended;
};
}  // namespace jit
#endif  // BASIC_BLOCK_H

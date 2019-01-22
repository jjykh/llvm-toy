#ifndef SCHEDULE_EMITTER_H
#define SCHEDULE_EMITTER_H
#include "src/handles.h"
#include "src/heap/heap.h"
namespace v8 {
namespace internal {
class Isolate;
namespace compiler {
class Schedule;
class BasicBlock;
class CallDescriptor;
class Node;
}  // namespace compiler
namespace tf_llvm {
class TFVisitor;
class ScheduleEmitter final {
 public:
  explicit ScheduleEmitter(Isolate* isolate, compiler::Schedule*,
                           compiler::CallDescriptor*);
  ~ScheduleEmitter();
  void Visit(TFVisitor* visitor);
  void VisitBlock(compiler::BasicBlock*, TFVisitor*);
  void VisitBlockControl(compiler::BasicBlock*, TFVisitor*);
  void VisitNode(compiler::Node*, TFVisitor*);
  void VisitCall(compiler::Node*, TFVisitor*, bool tail);
  void VisitCCall(compiler::Node*, TFVisitor*);

 private:
  void DoVisit(TFVisitor* visitor);
  compiler::Schedule* schedule() { return schedule_; }
  compiler::CallDescriptor* incoming_descriptor() {
    return incoming_descriptor_;
  }
  Isolate* isolate() { return isolate_; }

  bool IsMaterializableFromRoot(Handle<HeapObject> object,
                                Heap::RootListIndex* index_return);

  Isolate* isolate_;
  compiler::Schedule* schedule_;
  compiler::CallDescriptor* incoming_descriptor_;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // SCHEDULE_EMITTER_H

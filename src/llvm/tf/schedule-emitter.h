// Copyright 2019 UCWeb Co., Ltd.

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
                           compiler::CallDescriptor*, int32_t builtin_index);
  ~ScheduleEmitter();
  void Visit(TFVisitor* visitor);
  void VisitBlock(compiler::BasicBlock*, TFVisitor*);
  void VisitBlockControl(compiler::BasicBlock*, TFVisitor*);
  void VisitNode(compiler::Node*, TFVisitor*);
  void VisitCall(compiler::Node*, TFVisitor*, bool tail, int successor_bid = -1,
                 int exception_bid = -1);
  void VisitCCall(compiler::Node*, TFVisitor*, int operands_count,
                  bool save_fp);

 private:
  void DoVisit(TFVisitor* visitor);
  compiler::Schedule* schedule() { return schedule_; }
  compiler::CallDescriptor* incoming_descriptor() {
    return incoming_descriptor_;
  }
  Isolate* isolate() { return isolate_; }

  bool IsMaterializableFromRoot(Handle<HeapObject> object,
                                Heap::RootListIndex* index_return);
  bool ShouldEmitCall(compiler::Node* node);
  bool HandleCodeForCall(compiler::Node*, Handle<HeapObject>, TFVisitor*,
                         bool /*relative_call*/);
  bool TryLoadFromConstantTable(compiler::Node*, Handle<HeapObject>,
                                TFVisitor*);
  bool HandleIsolateIndependentBuiltin(compiler::Node* node, Handle<Code> code,
                                       TFVisitor* visitor, int builtin_index);
  bool ShouldUseRelativeBranchOrLoadFromConstant();

  Isolate* isolate_;
  compiler::Schedule* schedule_;
  compiler::CallDescriptor* incoming_descriptor_;
  compiler::BasicBlock* current_block_;
  int32_t builtin_index_;
};
}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif  // SCHEDULE_EMITTER_H

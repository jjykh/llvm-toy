#ifndef COMPILE_H
#define COMPILE_H

namespace v8 {
namespace internal {
namespace tf_llvm {
struct CompilerState;
void compile(CompilerState& state);

}  // namespace tf_llvm
}  // namespace internal
}  // namespace v8
#endif /* COMPILE_H */

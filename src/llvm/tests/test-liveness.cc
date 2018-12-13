#include <iostream>
#include "src/llvm/basic-block-manager.h"
#include "src/llvm/basic-block.h"
#include "src/llvm/liveness-analysis-visitor.h"
#include "src/llvm/tf/tf-parser.h"
using namespace v8::internal::tf_llvm;

int main() {
  FILE* f = fopen("src/llvm/tf/tests/scheduled.txt", "r");
  if (!f) {
    fprintf(stderr, "fails to open src/llvm/tf/tests/scheduled.txt\n");
    exit(1);
  }

  BasicBlockManager BBM;
  LivenessAnalysisVisitor lav(BBM);
  TFParser tfparser(&lav);
  tfparser.Parse(f);
  fclose(f);
  lav.CalculateLivesIns();
  for (auto& item : BBM) {
    BasicBlock* bb = item.second.get();
    using namespace std;

    cout << "BasicBlock " << bb->id() << ": lives:";
    for (int live : bb->liveins()) {
      cout << " " << live;
    }
    cout << endl;
  }
  return 0;
}

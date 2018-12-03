#include <iostream>
#include "basic-block-manager.h"
#include "basic-block.h"
#include "liveness-analysis-visitor.h"
#include "tf-parser.h"
using namespace jit;

int main() {
  FILE* f = fopen("tf/tests/scheduled.txt", "r");
  if (!f) {
    fprintf(stderr, "fails to open tf/tests/scheduled.txt\n");
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

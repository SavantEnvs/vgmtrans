// File-input driver: open a file from disk through VGMRoot::openRawFile — the path the GUI/shell take —
// which reaches the loaders that need a real path (RAR archives via unarr, ZIP/MAME sets).
#include "fuzz_common.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    return 2;
  }
  fuzzInit();
  auto& root = fuzzRoot();
  root.openRawFile(argv[1]);
  convertAll(root);
  root.removeAllFilesAndCollections();
  return 0;
}

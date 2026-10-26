// In-process libFuzzer harness: run VGMTrans' loaders + every scanner over a raw buffer.
// The first input byte selects the file extension (VGMRoot::loadRawFile narrows the scanner set by
// extension), the rest is the file content. Built twice: scan only, and -DFUZZ_CONVERT to also
// convert whatever was found to MIDI/SF2/DLS in memory.
#include "fuzz_common.h"

#include <array>
#include <cstddef>
#include <cstring>

static constexpr std::array<const char*, 16> kExtensions = {
    "",    "spc", "gba", "gsf", "minigsf", "nds", "sdat", "2sf",
    "tsq", "tvb", "sq",  "hd",  "bd",      "psf", "bin",  "psf2"};

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  fuzzInit();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2 || size > (1u << 20)) {
    return -1;
  }
  // The RSN loader re-opens the archive by path, which an in-memory file lacks; fuzz_open covers it.
  if (size >= 8 && memcmp(data + 1, "Rar!\x1A\x07\x00", 7) == 0) {
    return -1;
  }
  std::string name = "fuzz";
  if (const char* ext = kExtensions[data[0] % kExtensions.size()]; *ext) {
    name += '.';
    name += ext;
  }

  auto& root = fuzzRoot();
  root.loadRawFile(std::make_unique<VirtFile>(data + 1, static_cast<u32>(size - 1), name));
#ifdef FUZZ_CONVERT
  convertAll(root);
#endif
  root.removeAllFilesAndCollections();
  return 0;
}

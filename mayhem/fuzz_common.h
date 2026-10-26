#pragma once

#include "Root.h"
#include "ConversionContext.h"
#include "ConversionTypes.h"
#include "DLSConversion.h"
#include "DLSFile.h"
#include "MidiFile.h"
#include "Options.h"
#include "RawFile.h"
#include "SF2Conversion.h"
#include "SF2File.h"
#include "VGMColl.h"
#include "VGMFile.h"
#include "VGMInstrSet.h"
#include "VGMMiscFile.h"
#include "VGMSampColl.h"
#include "VGMSeq.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class FuzzRoot : public VGMRoot {
 public:
  void UI_setRootPtr(VGMRoot** root) override { *root = this; }
  std::filesystem::path UI_getSaveFilePath(const std::string&, const std::string& = "") override { return {}; }
  std::filesystem::path UI_getSaveDirPath(const std::filesystem::path& = {}) override { return {}; }
  bool UI_writeBufferToFile(const std::filesystem::path&, u8*, size_t) override { return true; }
};

inline FuzzRoot& fuzzRoot() {
  static FuzzRoot root;
  return root;
}

inline void fuzzInit() {
  fuzzRoot().init();
}

// Converts everything the scanners found to MIDI / SF2 / DLS in memory, exactly like VGMExport's
// saveAs<>() does, minus the disk writes.
inline void convertAll(VGMRoot& root) {
  const auto sf2Ctx = ConversionContext::fromOptions(ConversionOptions::the(), SynthTarget::SoundFont);
  const auto dlsCtx = ConversionContext::fromOptions(ConversionOptions::the(), SynthTarget::DLS);

  for (auto* coll : root.vgmColls()) {
    if (auto* seq = coll->seq()) {
      if (auto midi = seq->convertToMidi(coll, sf2Ctx)) {
        std::vector<u8> buf;
        midi->writeMidiToBuffer(buf);
      }
    }
    if (auto sf2 = conversion::createSF2File(*coll, sf2Ctx)) {
      sf2->saveToMem();
    }
    DLSFile dls;
    if (conversion::createDLSFile(dls, *coll, dlsCtx)) {
      std::vector<u8> buf;
      dls.writeDLSToBuffer(buf);
    }
  }

  for (auto variant : root.vgmFiles()) {
    if (auto* seq = variantToType<VGMSeq>(variant)) {
      if (auto midi = seq->convertToMidi(nullptr, sf2Ctx)) {
        std::vector<u8> buf;
        midi->writeMidiToBuffer(buf);
      }
    }
  }
}

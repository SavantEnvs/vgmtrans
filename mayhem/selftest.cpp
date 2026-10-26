// Known-answer test for VGMTrans: build small TamSoft PS1 ".tsq" songs byte by byte, push them through
// the real scan + MIDI conversion pipeline, and check the Standard MIDI File that comes out (PPQN, tempo,
// exact note-on/note-off ticks, keys, velocities, controllers, program changes). Prints PASS/FAIL lines.
#include "fuzz_common.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const std::string& what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
  (ok ? g_pass : g_fail)++;
}

struct MidiEv {
  u32 tick;
  u8 status;
  u8 d1;
  u8 d2;
};

struct ParsedMidi {
  bool valid = false;
  u16 ppqn = 0;
  u16 numTracks = 0;
  std::vector<MidiEv> channelEvents;
  std::vector<u32> tempos;  // microseconds per quarter note
};

u32 readVar(const std::vector<u8>& b, size_t& p) {
  u32 v = 0;
  while (p < b.size()) {
    u8 c = b[p++];
    v = (v << 7) | (c & 0x7F);
    if (!(c & 0x80)) break;
  }
  return v;
}

ParsedMidi parseMidi(const std::vector<u8>& b) {
  ParsedMidi m;
  if (b.size() < 14 || std::memcmp(b.data(), "MThd", 4) != 0) return m;
  m.numTracks = (b[10] << 8) | b[11];
  m.ppqn = (b[12] << 8) | b[13];
  size_t p = 14;
  while (p + 8 <= b.size()) {
    if (std::memcmp(&b[p], "MTrk", 4) != 0) return m;
    u32 len = (b[p + 4] << 24) | (b[p + 5] << 16) | (b[p + 6] << 8) | b[p + 7];
    p += 8;
    size_t end = p + len;
    if (end > b.size()) return m;
    u32 tick = 0;
    u8 running = 0;
    while (p < end) {
      tick += readVar(b, p);
      u8 st = b[p];
      if (st == 0xFF) {
        u8 type = b[p + 1];
        p += 2;
        u32 mlen = readVar(b, p);
        if (type == 0x51 && mlen == 3) {
          m.tempos.push_back((b[p] << 16) | (b[p + 1] << 8) | b[p + 2]);
        }
        p += mlen;
      } else if (st == 0xF0 || st == 0xF7) {
        p++;
        p += readVar(b, p);
      } else {
        if (st & 0x80) {
          running = st;
          p++;
        }
        u8 hi = running & 0xF0;
        u8 d1 = b[p++];
        u8 d2 = (hi == 0xC0 || hi == 0xD0) ? 0 : b[p++];
        m.channelEvents.push_back({tick, running, d1, d2});
      }
    }
  }
  m.valid = true;
  return m;
}

// TamSoft PS1 ".tsq" layout: song table {type(u16) headerOffset(u16)}, then 24 track headers
// {live(u8) pad(u8) trackOffset(u16)} relative to the header, then the track bytecode.
std::vector<u8> tsqFile(const std::vector<u8>& track0, const std::vector<u8>& track1 = {}) {
  std::vector<u8> f = {0, 0, 4, 0};  // song 0: BGM, header at +4
  const u16 hdr = 96;                // 24 track headers
  for (int i = 0; i < 24; i++) {
    if (i == 0 || (i == 1 && !track1.empty())) {
      u16 rel = hdr + (i == 0 ? 0 : static_cast<u16>(track0.size()));
      f.push_back(1);
      f.push_back(0);
      f.push_back(u8(rel));
      f.push_back(u8(rel >> 8));
    } else {
      f.insert(f.end(), {0, 0, 0, 0});
    }
  }
  f.insert(f.end(), track0.begin(), track0.end());
  f.insert(f.end(), track1.begin(), track1.end());
  return f;
}

// Scans `file` (as an anonymous raw file) and returns the first loaded sequence converted to a parsed SMF.
ParsedMidi convert(const std::vector<u8>& file, size_t* seqCount = nullptr) {
  auto& root = fuzzRoot();
  root.loadRawFile(std::make_unique<VirtFile>(file.data(), static_cast<u32>(file.size()), "test.tsq"));
  ParsedMidi out;
  size_t count = 0;
  for (auto variant : root.vgmFiles()) {
    if (auto* seq = variantToType<VGMSeq>(variant)) {
      count++;
      if (count == 1) {
        auto midi = seq->convertToMidi(nullptr);
        if (midi) {
          std::vector<u8> buf;
          midi->writeMidiToBuffer(buf);
          out = parseMidi(buf);
        }
      }
    }
  }
  if (seqCount) *seqCount = count;
  root.removeAllFilesAndCollections();
  return out;
}

std::vector<MidiEv> noteOns(const ParsedMidi& m) {
  std::vector<MidiEv> v;
  for (auto& e : m.channelEvents)
    if ((e.status & 0xF0) == 0x90 && e.d2 > 0) v.push_back(e);
  return v;
}

std::vector<MidiEv> noteOffs(const ParsedMidi& m) {
  std::vector<MidiEv> v;
  for (auto& e : m.channelEvents)
    if ((e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.d2 == 0)) v.push_back(e);
  return v;
}

bool hasEv(const ParsedMidi& m, u32 tick, u8 status, u8 d1, int d2 = -1) {
  for (auto& e : m.channelEvents)
    if (e.tick == tick && e.status == status && e.d1 == d1 && (d2 < 0 || e.d2 == d2)) return true;
  return false;
}

void testSong() {
  // Track 0 (MIDI channel 0): program 5, volume byte 0x80, key 60 for 10 ticks, rest 5, key 62 for 8
  // ticks, rest 5. Track 1 (channel 1): key 48 for 12 ticks. Delta bytes are ticks; 0x80+key starts a
  // note, 0xF0 ends it, 0xFF ends the track.
  const std::vector<u8> t0 = {0xE2, 0x05, 0xE0, 0x80, 0xBC, 0x0A, 0xF0, 0x05,
                              0xBE, 0x08, 0xF0, 0x05, 0xF0, 0xFF};
  const std::vector<u8> t1 = {0xB0, 0x0C, 0xF0, 0x05, 0xF0, 0xFF};
  size_t count = 0;
  auto m = convert(tsqFile(t0, t1), &count);
  check(count == 1, "TamSoft song is detected as exactly one sequence");
  check(m.valid, "MIDI output is a well-formed SMF");
  check(m.ppqn == 24, "MIDI PPQN is 24");
  check(!m.tempos.empty() && m.tempos[0] > 404700 && m.tempos[0] < 404800,
        "initial tempo is 24 ticks per PSX frame (~404762 us/quarter)");
  check(hasEv(m, 0, 0xC0, 5), "program change 5 on channel 0 at tick 0");
  // The song volume byte 0x80 is halved to 64 and then mapped through the linear-amplitude scale:
  // round(sqrt(64/127) * 127) = 90.
  check(hasEv(m, 0, 0xB0, 7, 90), "volume 0x80 becomes controller 7 = 90 on channel 0");
  // Note velocity is the fixed 100 mapped the same way: round(sqrt(100/127) * 127) = 113.
  check(hasEv(m, 0, 0x90, 60, 113), "ch0 note-on key 60 velocity 113 at tick 0");
  check(hasEv(m, 10, 0x80, 60), "ch0 note-off key 60 at tick 10");
  check(hasEv(m, 15, 0x90, 62, 113), "ch0 note-on key 62 velocity 113 at tick 15");
  check(hasEv(m, 23, 0x80, 62), "ch0 note-off key 62 at tick 23");
  check(hasEv(m, 0, 0x91, 48, 113), "ch1 note-on key 48 velocity 113 at tick 0");
  check(hasEv(m, 12, 0x81, 48), "ch1 note-off key 48 at tick 12");
}

void testRejects() {
  size_t count = 1;
  auto tracks = tsqFile({0xBC, 0x0A, 0xF0, 0x05, 0xF0, 0xFF});
  tracks[6] = 0xFF;  // track 0 offset points far past the end of the file
  tracks[7] = 0xFF;
  convert(tracks, &count);
  check(count == 0, "TamSoft song with a track offset beyond the file is rejected");

  convert(std::vector<u8>(64, 0), &count);
  check(count == 0, "TamSoft song table without entries yields no sequence");

  std::vector<u8> terminated(64, 0);
  terminated[0] = 0xF0;  // song table terminator 0x00FFFFF0
  terminated[1] = 0xFF;
  terminated[2] = 0xFF;
  convert(terminated, &count);
  check(count == 0, "terminated TamSoft song table yields no sequence");
}

}  // namespace

int main() {
  fuzzInit();
  testSong();
  testRejects();
  std::printf("SELFTEST passed=%d failed=%d\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

// VT raw-API probe (ObjC++): exact engine recipe — param sets -> format desc ->
// VTDecompressionSessionCreate(x420) -> per-AU length-prefixed feed, async flags.
#import <VideoToolbox/VideoToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

static int g_ok = 0;
static std::map<OSStatus, int> g_bad;

static void OnDecode(void *refCon, void *, OSStatus status, VTDecodeInfoFlags,
                     CVImageBufferRef img, CMTime, CMTime) {
  if (status == noErr && img) {
    g_ok++;
  } else {
    static_cast<std::map<OSStatus, int> *>(refCon)->operator[](status)++;
  }
}

int main(int argc, char **argv) {
  if (argc < 2) { printf("usage: vtprobe <raw265>\n"); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> bytes(sz);
  if (fread(bytes.data(), 1, sz, f) != (size_t)sz) { fclose(f); return 2; }
  fclose(f);

  // split annex b (or avcc mode: concatenated [len32][NAL] packets, one packet = one AU)
  bool avccMode = argc > 2 && (!strcmp(argv[2], "avcc") || !strcmp(argv[2], "frames"));
  std::vector<std::pair<int, std::vector<uint8_t>>> nals;  // (type, payload)
  std::vector<std::vector<std::vector<uint8_t>>> ausPre;
  if (avccMode) {
    size_t p = 0;
    while (p + 4 <= bytes.size()) {
      uint32_t n = ((uint32_t)bytes[p] << 24) | ((uint32_t)bytes[p + 1] << 16) |
                   ((uint32_t)bytes[p + 2] << 8) | bytes[p + 3];
      p += 4;
      if (n == 0 || p + n > bytes.size()) break;
      std::vector<uint8_t> nal(bytes.begin() + p, bytes.begin() + p + n);
      p += n;
      if (nal.size() < 2) continue;
      nals.push_back({(nal[0] >> 1) & 0x3F, nal});
      ausPre.push_back({nal});
    }
    printf("avcc chunks: %zu\n", ausPre.size());
  }
  for (long i = 0; i + 3 <= sz;) {
    if (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1) {
      long s = i + 3;
      long e = s;
      while (e + 2 < sz && !(bytes[e] == 0 && bytes[e + 1] == 0 && bytes[e + 2] == 1)) e++;
      while (e > s && bytes[e - 1] == 0) e--;  // strip trailing zeros
      if (e - s >= 2) nals.push_back({(bytes[s] >> 1) & 0x3F, std::vector<uint8_t>(bytes.begin() + s, bytes.begin() + e)});
      i = e;
    } else {
      i++;
    }
  }
  printf("nals: %zu\n", nals.size());
  std::vector<uint8_t> vps, sps, pps;
  for (auto &n : nals) {
    if (n.first == 32 && vps.empty()) vps = n.second;
    if (n.first == 33 && sps.empty()) sps = n.second;
    if (n.first == 34 && pps.empty()) pps = n.second;
  }
  if (vps.empty() || sps.empty() || pps.empty()) { printf("missing param sets\n"); return 2; }
  printf("vps: %zu sps: %zu pps: %zu\n", vps.size(), sps.size(), pps.size());

  // group AUs: VCL with first_slice_segment_in_pic_flag=1 opens new AU
  // (avcc mode keeps demuxer packet boundaries: one packet = one AU)
  // ("frames" mode: avcc input, flag-grouped, non-VCL stripped = engine frame packets)
  bool framesMode = argc > 2 && !strcmp(argv[2], "frames");
  std::vector<std::vector<std::vector<uint8_t>>> aus;
  if (framesMode) {
    std::vector<std::vector<uint8_t>> pending, cur;
    for (auto &n : nals) {
      if (n.first <= 21) {
        bool first = (n.second[2] >> 7) == 1;
        if (first && !cur.empty()) { aus.push_back(cur); cur.clear(); }
        cur.push_back(n.second);
      }
      // non-VCL dropped entirely
    }
    if (!cur.empty()) aus.push_back(cur);
  } else if (avccMode) {
    aus = ausPre;
  } else {
    std::vector<std::vector<uint8_t>> pending, cur;
    for (auto &n : nals) {
      if (n.first <= 21) {
        bool first = (n.second[2] >> 7) == 1;
        if (first && !cur.empty()) { aus.push_back(pending), pending.clear(); aus.back().insert(aus.back().end(), cur.begin(), cur.end()); cur.clear(); }
        cur.push_back(n.second);
      } else {
        pending.push_back(n.second);
      }
    }
    if (!cur.empty()) { aus.push_back(pending), pending.clear(); aus.back().insert(aus.back().end(), cur.begin(), cur.end()); }
  }
  printf("aus: %zu\n", aus.size());

  const uint8_t *sets[3] = {vps.data(), sps.data(), pps.data()};
  size_t sizes[3] = {vps.size(), sps.size(), pps.size()};
  CMVideoFormatDescriptionRef fmt = nullptr;
  OSStatus st = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
      kCFAllocatorDefault, 3, sets, sizes, 4, nullptr, &fmt);
  printf("format create: %d\n", (int)st);
  if (st != noErr || !fmt) return 2;

  static std::map<OSStatus, int> tally;
  VTDecompressionOutputCallbackRecord cb = {OnDecode, &tally};
  int32_t pixfmt = 'x420';
  CFNumberRef pixnum = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixfmt);
  const void *keys[] = {kCVPixelBufferPixelFormatTypeKey};
  const void *vals[] = {pixnum};
  CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFRelease(pixnum);
  VTDecompressionSessionRef sess = nullptr;
  st = VTDecompressionSessionCreate(kCFAllocatorDefault, fmt, nullptr, attrs, &cb, &sess);
  printf("session create: %d\n", (int)st);
  if (st != noErr || !sess) return 2;

  int n = 0;
  for (auto &au : aus) {
    if (n >= 30) break;
    std::vector<uint8_t> buf;
    for (auto &nal : au) {
      uint32_t len = (uint32_t)nal.size();
      uint8_t lb[4] = {(uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len};
      buf.insert(buf.end(), lb, lb + 4);
      buf.insert(buf.end(), nal.begin(), nal.end());
    }
    CMBlockBufferRef blk = nullptr;
    st = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, buf.size(),
        kCFAllocatorDefault, nullptr, 0, buf.size(), 0, &blk);
    if (st != noErr) { printf("au %d block fail %d\n", n, (int)st); n++; continue; }
    CMBlockBufferReplaceDataBytes(buf.data(), blk, 0, buf.size());
    CMSampleTimingInfo ti;
    ti.duration = CMTimeMake(1001, 24000);
    ti.presentationTimeStamp = CMTimeMake(n * 1001, 24000);
    ti.decodeTimeStamp = CMTimeMake(n * 1001, 24000);
    size_t ssize = buf.size();
    CMSampleBufferRef sb = nullptr;
    st = CMSampleBufferCreate(kCFAllocatorDefault, blk, true, nullptr, nullptr, fmt,
        1, 1, &ti, 1, &ssize, &sb);
    if (st != noErr) { printf("au %d sample fail %d\n", n, (int)st); CFRelease(blk); n++; continue; }
    VTDecodeInfoFlags fo = 0;
    VTDecodeFrameFlags dflags = kVTDecodeFrame_EnableAsynchronousDecompression;
    if (getenv("VT_TEMPORAL")) dflags |= kVTDecodeFrame_EnableTemporalProcessing;
    st = VTDecompressionSessionDecodeFrame(sess, sb, dflags, nullptr, &fo);
    if (st != noErr) printf("au %d submit fail %d\n", n, (int)st);
    CFRelease(sb);
    CFRelease(blk);
    n++;
  }
  VTDecompressionSessionWaitForAsynchronousFrames(sess);
  printf("fed %d AUs -> decoded ok: %d errors:", n, g_ok);
  for (auto &kv : tally) printf(" {%d: %d}", (int)kv.first, kv.second);
  printf("\n");
  return 0;
}

#include <iostream>
#include <string>
#include <thread>

#include "avox/AvoxMuxer.h"

using namespace avox;

class TranscodeRecorderOb : public IRecorderOb {
 public:
  virtual void onStateChange(RecorderState preState,
                             RecorderState state) override {
    std::cout << "transcode state: " << (int32_t)preState << " -> "
              << (int32_t)state << std::endl;
  }
  virtual void onIoError(AVError error, const char* msg) override {
    std::cout << "transcode recorder error: " << (int32_t)error << " " << msg
              << std::endl;
    hasError = true;
  }
  virtual void onProgress(const RecorderProgress& progress) override {
    if (progress.totalTimeMs > 0) {
      int percent = (int)(progress.currentTimeMs * 100 / progress.totalTimeMs);
      std::cout << "progress: " << percent << "% (" << progress.currentTimeMs
                << "/" << progress.totalTimeMs << "ms)" << std::endl;
    }
  }
  virtual void onComplete() override {
    std::cout << "transcode recorder completed" << std::endl;
  }
  bool hasError = false;
};

int main(int argc, char* argv[]) {
  // a1.aac 美好.mp4
  const char* inputUrl = "D://Back/美好.mp4";
  if (argc > 1) {
    inputUrl = argv[1];
  }
  const char* outputFile = "D://transcode_output1.mp4";
  if (argc > 2) {
    outputFile = argv[2];
  }
  int recordSeconds = 10;
  if (argc > 3) {
    recordSeconds = std::atoi(argv[3]);
    if (recordSeconds <= 0) {
      recordSeconds = 10;
    }
  }
  TranscodeRecorderOb ob = {};
  // createRecorder createTranscodeRecorder
  IRecorder* recorder = createRecorder(true);
  // none=纯音频 seek 测试;验证视频 seek/首帧 IDR 时改为 h265 或注释掉此行
  recorder->setVideoCodec(VCodecId::h264);
  // 默认硬编; Windows 下 h264_mf 可能因 profile=main 选项打不开, 传 soft 走 FFmpeg 软编
  if (argc > 4 && std::string(argv[4]) == "soft") {
    recorder->getOption()->setBool("rec.hard.encode", false);
  }
  if (!recorder) {
    std::cout << "failed to create transcode recorder" << std::endl;
    return 1;
  }
  addRecorderOb(recorder, &ob);
  std::cout << "start transcode recording..." << std::endl;
  std::cout << "  input:  " << inputUrl << std::endl;
  std::cout << "  output: " << outputFile << std::endl;
  std::cout << "  seconds: " << recordSeconds << std::endl;
  // recorder->setIoPlan(IoPlan::zlmediakit);
  // recorder->setMuxerType(MuxerType::ffmpeg);
  if (!recorder->open(inputUrl, outputFile)) {
    std::cout << "failed to open transcode recorder" << std::endl;
    removeRecorderOb(recorder, &ob);
    delete recorder;
    return 1;
  }
  std::cout << "recording for " << recordSeconds << " seconds..." << std::endl;
  bool bSeekDone = false;
  for (int i = 0; i < recordSeconds; i++) {
    RecorderState state = recorder->getState();
    if (state == RecorderState::completed) {
      std::cout << "transcode recorder already closed" << std::endl;
      break;
    }
    // 进入 recording 后 seek 到总时长一半,验证 seek + 输出 PTS 单调(+视频首帧 IDR)
    if (!bSeekDone && state == RecorderState::recording) {
      int64_t duration = recorder->getDuration();
      int64_t pos = duration > 0 ? duration / 2 : 0;
      std::cout << "seek to " << pos << "ms (duration " << duration << "ms)"
                << std::endl;
      std::cout << "seek result: " << (recorder->seek(pos) ? "true" : "false")
                << std::endl;
      bSeekDone = true;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "recording... " << (i + 1) << "s, state=" << (int32_t)state
              << std::endl;
  }
  std::cout << "stopping transcode recorder..." << std::endl;
  recorder->close();
  removeRecorderOb(recorder, &ob);
  delete recorder;
  std::cout << "done" << std::endl;
  return 0;
}

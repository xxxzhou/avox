#include <iostream>
#include <thread>

#include "avox/AvoxMuxer.h"

using namespace avox;

class RecorderOb : public IRecorderOb {
 public:
  virtual void onStateChange(RecorderState preState,
                             RecorderState state) override {
    std::cout << "recorder state: " << (int32_t)preState << " -> "
              << (int32_t)state << std::endl;
  }
  virtual void onIoError(AVError error, const char* msg) override {
    std::cout << "recorder error: " << (int32_t)error << " " << msg
              << std::endl;
    hasError = true;
  }
  virtual void onComplete() override {
    std::cout << "recorder completed" << std::endl;
  }
  bool hasError = false;
};

int main(int argc, char* argv[]) {
  const char* inputUrl = "D://Back/美好.mp4";
  if (argc > 1) {
    inputUrl = argv[1];
  }
  const char* outputFile = "D://output.mp4";
  if (argc > 2) {
    inputUrl = argv[2];
  }
  RecorderOb ob = {};
  IRecorder* recorder = createRecorder(true);
  addRecorderOb(recorder, &ob);
  std::cout << "start recording..." << std::endl;
  std::cout << "  input:  " << inputUrl << std::endl;
  std::cout << "  output: " << outputFile << std::endl;
  recorder->setIoPlan(IoPlan::zlmediakit);
  recorder->getOption()->setNumber("io.rtsp.speed", 4.0);
  // recorder->setIoPlan(IoPlan::zlmediakit);
  if (!recorder->open(inputUrl, outputFile)) {
    std::cout << "failed to open recorder" << std::endl;
    return 1;
  }
  // 录制10秒
  std::cout << "recording for 10 seconds..." << std::endl;
  for (int i = 0; i < 10; i++) {
    RecorderState state = recorder->getState();
    if (state == RecorderState::completed) {
      std::cout << "recorder already closed" << std::endl;
      break;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "recording... " << (i + 1) << "s" << std::endl;
  }
  std::cout << "stopping recorder..." << std::endl;
  recorder->close();
  removeRecorderOb(recorder, &ob);
  delete recorder;
  std::cout << "done" << std::endl;
  return 0;
}

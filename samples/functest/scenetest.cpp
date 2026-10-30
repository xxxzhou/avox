// 编辑场景导出探针(录屏方案 P1-a):
//   scene    = 静态样式导出(背景/圆角/阴影/padding/比例), 源 16:10 → 画布 16:9
//   fps      = X1 帧率门(60fps 源 → 声明 30, 输出须 30fps)
//   negative = X2 负例(场景画布与声明输出尺寸不一致须拒绝, 无输出帧)
//   detach   = ① 场景生命周期: 录制中 detach+destroy 场景, 剩余帧须继续渲染不崩
//   recover  = ④ 尺寸拒绝后可恢复: 只改画布应靠每帧重试自动出帧(管道不粘死)
// 用法: scenetest <mode> <input> <output>
#include <chrono>
#include <thread>

#include "avox/AvoxCapture.h"
#include "avox/AvoxMuxer.h"
#include "avox/module/LogHelper.hpp"

using namespace avox;

namespace {

class RecOb : public IRecorderOb {
 public:
  void onStateChange(RecorderState pre, RecorderState cur) override {
    LOGFLF(LogLevel::info, "[rec] state:", (int32_t)pre, "->", (int32_t)cur);
  }
  void onIoError(AVError err, const char* msg) override {
    LOGFLF(LogLevel::error, "[rec] error:", (int32_t)err, " ", msg ? msg : "");
  }
  void onProgress(const RecorderProgress& p) override {
    lastMs = p.currentTimeMs;
    LOGFLF(LogLevel::info, "[rec] progress:", p.currentTimeMs, "/",
           p.totalTimeMs, "ms");
  }
  int64_t lastMs = -1;
  void onComplete() override {
    LOGFLF(LogLevel::info, "[rec] complete");
    bDone = true;
  }
  bool bDone = false;
};

bool waitDone(RecOb& ob, int32_t timeoutMs) {
  for (int32_t i = 0; i < timeoutMs / 100 && !ob.bDone; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return ob.bDone;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    LOGFLF(LogLevel::error, "usage: scenetest <scene|fps|trim|negative|detach|recover> <input> <output>");
    return 1;
  }
  std::string mode = argv[1];
  const char* input = argv[2];
  const char* output = argv[3];

  IRecorder* rec = createRecorder(true);
  if (!rec) {
    LOGFLF(LogLevel::error, "createRecorder failed");
    return 1;
  }
  RecOb ob;
  addRecorderOb(rec, &ob);
  rec->setVideoCodec(VCodecId::h264);
  rec->setAudioCodec(ACodecId::none);

  if (mode == "scene") {
    // 源 16:10(1280x800) → 画布 16:9(1280x720): 内容按比例适配+居中,
    // 四周背景/圆角/阴影可像素验证
    VideoDesc desc = {};
    desc.width = 1280;
    desc.height = 720;
    desc.fps = 30.0;
    rec->setVideoDesc(desc);
    IEditorScene* scene = createEditorScene();
    scene->setCanvas(1280, 720);
    SceneStyle style = {};
    style.bgColorTop[0] = 0.10f;
    style.bgColorTop[1] = 0.12f;
    style.bgColorTop[2] = 0.16f;
    style.bgColorBottom[0] = 0.02f;
    style.bgColorBottom[1] = 0.03f;
    style.bgColorBottom[2] = 0.05f;
    style.padding = 0.05f;      // 64px
    style.cornerRadius = 0.02f; // 25.6px
    style.shadowRadius = 0.03f;
    style.shadowAlpha = 0.5f;
    style.shadowOffsetY = 0.01f;
    scene->setStyle(style);
    if (!attachEditorScene(rec->getSurfaceRender(), scene)) {
      LOGFLF(LogLevel::error, "attachEditorScene failed");
      return 1;
    }
    if (!rec->open(input, output)) {
      LOGFLF(LogLevel::error, "open failed");
      return 1;
    }
    if (!waitDone(ob, 60000)) {
      LOGFLF(LogLevel::error, "scene export timeout");
      rec->close();
      return 1;
    }
    detachEditorScene(rec->getSurfaceRender());
    destroyEditorScene(scene);
    LOGFLF(LogLevel::info, "scene mode done:", output);
  } else if (mode == "fps") {
    // X1: 只声明帧率 30(尺寸跟源), 60fps 源须被门到 30(验证用 ffprobe 数帧)
    VideoDesc desc = {};
    desc.fps = 30.0;
    rec->setVideoDesc(desc);
    if (!rec->open(input, output)) {
      LOGFLF(LogLevel::error, "open failed");
      return 1;
    }
    if (!waitDone(ob, 60000)) {
      LOGFLF(LogLevel::error, "fps export timeout");
      rec->close();
      return 1;
    }
    LOGFLF(LogLevel::info, "fps mode done:", output);
  } else if (mode == "trim") {
    // P1-b X3 引擎侧: 场景挂着 + seek 入点 + 到出点主动 close
    // (中间剪切 cuts 的前置链路; 时间映射 v1 = 场景时间基即录制文件 pts 基)
    VideoDesc desc = {};
    desc.fps = 30.0;
    rec->setVideoDesc(desc);
    IEditorScene* scene = createEditorScene();
    scene->setCanvas(1280, 720);
    SceneStyle style = {};
    style.bgColorTop[0] = 0.10f;
    style.bgColorTop[1] = 0.12f;
    style.bgColorTop[2] = 0.16f;
    style.bgColorBottom[0] = 0.02f;
    style.bgColorBottom[1] = 0.03f;
    style.bgColorBottom[2] = 0.05f;
    style.padding = 0.05f;
    style.cornerRadius = 0.02f;
    scene->setStyle(style);
    if (!attachEditorScene(rec->getSurfaceRender(), scene)) {
      LOGFLF(LogLevel::error, "attachEditorScene failed");
      return 1;
    }
    if (!rec->open(input, output)) {
      LOGFLF(LogLevel::error, "open failed");
      return 1;
    }
    for (int32_t i = 0; i < 200 && rec->getState() != RecorderState::recording;
         i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (rec->getState() != RecorderState::recording) {
      LOGFLF(LogLevel::error, "not recording, trim aborted");
      return 1;
    }
    if (!rec->seek(2000)) {
      LOGFLF(LogLevel::error, "seek failed");
      return 1;
    }
    // 出点: 源位置 5s, 到点主动 close(导出应覆盖 ~2s..5s)
    for (int32_t i = 0; i < 400; i++) {
      if (ob.lastMs >= 5000) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    rec->close();
    detachEditorScene(rec->getSurfaceRender());
    destroyEditorScene(scene);
    LOGFLF(LogLevel::info, "trim mode done, lastProgress:", ob.lastMs);
  } else if (mode == "detach") {
    // ① 生命周期回归: 录制中 detach 后立即 destroy 场景, 之后的帧继续渲染。
    // 修前场景层成员不摘、每帧仍 evaluate 宿主裸指针 → 野指针(常崩);
    // 修后 detach 返回即可安全销毁, 剩余帧照常出画面
    VideoDesc desc = {};
    desc.width = 1280;
    desc.height = 720;
    desc.fps = 30.0;
    rec->setVideoDesc(desc);
    IEditorScene* scene = createEditorScene();
    scene->setCanvas(1280, 720);
    SceneStyle style = {};
    style.padding = 0.02f;
    scene->setStyle(style);
    if (!attachEditorScene(rec->getSurfaceRender(), scene)) {
      LOGFLF(LogLevel::error, "attachEditorScene failed");
      return 1;
    }
    if (!rec->open(input, output)) {
      LOGFLF(LogLevel::error, "open failed");
      return 1;
    }
    // 等场景层真跑起来(首帧已建图且有进度)再摘, 否则测不到渲染侧求值
    for (int32_t i = 0; i < 200 && ob.lastMs < 500; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    LOGFLF(LogLevel::info, "detach mode: detach + destroy at progress:",
           ob.lastMs, "ms");
    detachEditorScene(rec->getSurfaceRender());
    destroyEditorScene(scene);
    // 剩余帧继续渲染: 修前这里会打到已释放场景
    if (!waitDone(ob, 60000)) {
      LOGFLF(LogLevel::error, "detach export timeout");
      rec->close();
      return 1;
    }
    rec->close();
    LOGFLF(LogLevel::info, "detach mode done (survived scene destroy)");
  } else if (mode == "recover") {
    // ④ 回归: 画布与声明尺寸不一致 → 建图被拒, 但管道不能进死状态。
    // 之后只改画布(不重挂/不重开), 靠每帧重试应自动恢复出帧
    VideoDesc desc = {};
    desc.width = 960;
    desc.height = 540;
    desc.fps = 30.0;
    rec->setVideoDesc(desc);
    IEditorScene* scene = createEditorScene();
    scene->setCanvas(1280, 720);  // 故意与声明不一致 → X2 拒绝建图
    if (!attachEditorScene(rec->getSurfaceRender(), scene)) {
      LOGFLF(LogLevel::error, "attachEditorScene failed");
      return 1;
    }
    if (!rec->open(input, output)) {
      LOGFLF(LogLevel::error, "open failed");
      return 1;
    }
    // 拒绝期: 不解析画布, 建图每帧被拒
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    // 文件源转码比实时快, 小文件可能在拒绝窗口内就跑完(那时已无帧可等),
    // 这种输入测不到「改画布后恢复」, 明确跳过而不是误报 stuck
    if (ob.bDone) {
      LOGFLF(LogLevel::warn,
             "recover mode skipped: input finished inside reject window, "
             "use a slower/larger source");
      rec->close();
      detachEditorScene(rec->getSurfaceRender());
      destroyEditorScene(scene);
      return 0;
    }
    const int64_t rejectedMs = ob.lastMs;
    LOGFLF(LogLevel::info, "recover mode: rejected phase progress:", rejectedMs);
    // 只改画布(不触发重建): 修前 bRebuilding 已粘死, 永远出不了帧
    scene->setCanvas(960, 540);
    bool bRecovered = false;
    for (int32_t i = 0; i < 100 && !bRecovered; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      bRecovered = ob.lastMs > rejectedMs;
    }
    if (!bRecovered) {
      LOGFLF(LogLevel::error, "recover mode failed: pipeline stuck after reject");
      rec->close();
      return 1;
    }
    LOGFLF(LogLevel::info, "recover mode: recovered at progress:", ob.lastMs);
    if (!waitDone(ob, 60000)) {
      LOGFLF(LogLevel::error, "recover export timeout");
      rec->close();
      return 1;
    }
    rec->close();
    detachEditorScene(rec->getSurfaceRender());
    destroyEditorScene(scene);
    LOGFLF(LogLevel::info, "recover mode done");
  } else if (mode == "negative") {
    // X2 负例: 画布 1280x720 vs 声明 960x540 —— 建图须失败, 无有效输出
    VideoDesc desc = {};
    desc.width = 960;
    desc.height = 540;
    rec->setVideoDesc(desc);
    IEditorScene* scene = createEditorScene();
    scene->setCanvas(1280, 720);
    if (!attachEditorScene(rec->getSurfaceRender(), scene)) {
      LOGFLF(LogLevel::error, "attachEditorScene failed (unexpected)");
      return 1;
    }
    if (!rec->open(input, output)) {
      LOGFLF(LogLevel::error, "open failed (unexpected)");
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(3));
    rec->close();
    destroyEditorScene(scene);
    LOGFLF(LogLevel::info, "negative mode done (check log for canvas mismatch)");
  } else {
    LOGFLF(LogLevel::error, "unknown mode:", mode);
    return 1;
  }
  delete rec;
  return 0;
}

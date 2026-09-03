// bodyposetest: 视频驱动身体姿态 (MediaPipe Pose) 可视化测试。
// 放视频 (MediaPlayer + Vulkan SurfaceRender) + enableImage 回读每帧 rgba8 ->
//   feed IBody (avox_avatar 的 mediapipe_body 后端: BlazePose 检测 -> 人体对齐 warp ->
//   pose_landmarker 33 点) -> 用 ISurfaceRender 几何渲染 (IGeometryLayer)
//   把检测到的 33 个身体 landmark 点叠加到画面上看点位是否正确。
//
// 用法: bodyposetest [url] [WxH]
//   url : 视频路径 (默认 test_clips/face-demographics-walking.mp4)
//   WxH : enableImage 回读分辨率 (默认 640x360; 须与源同比例以使叠加对齐)
// 依赖: avox_avatar + avox_onnx 插件 (createBody -> ensureStarted 自动扫描 plugins/ 加载)
// 键: ESC 退出; S 存当前帧 PNG
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "avox/Avox.hpp"
#include "avox/AvoxAvatar.h"     // createBody / IBody / IBodyOb
#include "avox/AvoxLayer.h"      // ISurfaceRender / ISurfaceRenderOb / addSurfaceRenderOb
#include "avox/AvoxPlayer.h"     // createMediaPlayer / IMediaPlayer / IoPlan
#include "avox/AvoxBase.h"       // IOption (mp.synctype -> 视频主时钟)
#include "avox/AvoxVideo.h"      // createImageBuffer / IImageBuffer / ImageFormat / ImageType
#include "avox_vulkan/VkExport.h"  // enableRenderGeometry / IGeometryLayer / canVulkan / saveImagePath

#ifdef _WIN32
#include <windows.h>
#endif

using namespace avox;

static IMediaPlayer* gMp = nullptr;
static IImageBuffer* gImgBuf = nullptr;
static IBody* gBody = nullptr;
static IGeometryLayer* gGeo = nullptr;
static int32_t gOutW = 640;
static int32_t gOutH = 360;
// feed 节流: 每 N 个渲染帧喂一帧。2 个 ONNX(det+lm) 较重, 喂满会饿死渲染线程。
static int gFeedEvery = 4;

// 最新 body landmarks: worker 线程 (MediaPipeBody::onRunTask) 写, 渲染线程读。
static struct LatestBody {
  std::mutex mtx;
  std::vector<float> data;  // count*3 交错 (x,y,z), x/y 像素坐标在 feed 帧空间, z 相对深度
  int32_t count = 0;
  int32_t imgW = 0;
  int32_t imgH = 0;
  int64_t ptsMs = 0;
} gLm;

// IBody 观察者
class BodyOb : public IBodyOb {
 public:
  void onBodyDesc(const BodyDesc& desc) override {
    printf("[body] 就绪: lm=%d\n", desc.landmarkCount);
  }
  void onBodyLandmarks(const AvoxData& rawPts, int32_t count,
                       int32_t imgW, int32_t imgH, int64_t pts) override {
    static std::atomic<int> calls{0};
    static std::atomic<int> hitCalls{0};
    int c = ++calls;
    bool bPrint = (c <= 3);
    if (count > 0) {
      int h = ++hitCalls;
      if (h <= 3) {
        bPrint = true;
      }
    }
    if (bPrint) {
      const float* p = (count > 0 && rawPts.data) ? reinterpret_cast<const float*>(rawPts.data) : nullptr;
      printf("[body-lm] call#%d count=%d %dx%d%s\n",
             c, count, imgW, imgH, (p && count > 0) ? "" : " (no-person)");
      if (p && count > 0) {
        // 打印关键点: 0=nose 11/12=shoulder 13/14=elbow 15/16=wrist 23/24=hip 25/26=knee 27/28=ankle
        const int keys[] = {0, 11, 12, 13, 14, 23, 24, 25, 26, 27, 28};
        for (int k : keys) {
          printf("   lm[%d]=%.1f,%.1f (z=%.2f conf=%.2f)\n", k, p[k*4], p[k*4+1], p[k*4+2], p[k*4+3]);
        }
      }
    }
    std::lock_guard<std::mutex> lock(gLm.mtx);
    if (count > 0 && rawPts.data && rawPts.size >= count * 4 * int32_t(sizeof(float))) {
      const float* p = reinterpret_cast<const float*>(rawPts.data);
      gLm.data.assign(p, p + count * 4);
    } else {
      gLm.data.clear();
    }
    gLm.count = count;
    gLm.imgW = imgW;
    gLm.imgH = imgH;
    gLm.ptsMs = pts;
  }
  void onBodyError(const char* err) override {
    printf("[body][err] %s\n", err ? err : "(null)");
  }
};

// ISurfaceRender 观察者
class RenderOb : public ISurfaceRenderOb {
 public:
  std::atomic<int> frameCount{0};
  std::atomic<int> lastLmCount{0};
  int feedCounter = 0;
  int saveCount = 0;
  void onRender() override {
    int n = ++frameCount;
    if (n <= 5 || n % 300 == 0) {
      printf("[render] frame %d, landmarks=%d\n", n, lastLmCount.load());
    }
    if (gBody && gImgBuf && (++feedCounter % gFeedEvery == 0)) {
      gBody->feed(gImgBuf, 0);
    }
    if (gGeo) {
      gGeo->clear();
      std::lock_guard<std::mutex> lock(gLm.mtx);
      lastLmCount.store(gLm.count);
      if (gLm.count > 0 && gLm.imgW > 0 && gLm.imgH > 0 && !gLm.data.empty()) {
        float iw = float(gLm.imgW), ih = float(gLm.imgH);
        int cnt = int(gLm.data.size() / 4);
        for (int i = 0; i < cnt; ++i) {
          float nx = gLm.data[i * 4 + 0] / iw;
          float ny = gLm.data[i * 4 + 1] / ih;
          gGeo->drawPoint(nx, ny, 6.0f);
        }
        // 骨段连线 (MediaPipe Pose 标准连接), 便于看骨架是否贴合人体
        static const int bones[][2] = {
            {0, 2}, {2, 5}, {5, 7}, {7, 9}, {9, 11}, {11, 13}, {13, 15},   // 右臂
            {0, 1}, {1, 3}, {3, 6}, {6, 8}, {8, 10}, {10, 12}, {12, 14}, {14, 16},  // 左臂
            {5, 6}, {5, 11}, {6, 12}, {11, 12},                              // 躯干
            {11, 23}, {12, 24}, {23, 24},                                    // 髋
            {23, 25}, {25, 27}, {27, 29}, {29, 31},                          // 左腿
            {24, 26}, {26, 28}, {28, 30}, {30, 32},                          // 右腿
        };
        for (const auto& b : bones) {
          if (b[0] < cnt && b[1] < cnt) {
            gGeo->drawLine(gLm.data[b[0] * 4 + 0] / iw, gLm.data[b[0] * 4 + 1] / ih,
                           gLm.data[b[1] * 4 + 0] / iw, gLm.data[b[1] * 4 + 1] / ih);
          }
        }
      }
    }
    if (gImgBuf && lastLmCount.load() > 0 && !getenv("BPT_NOSAVE") && saveCount < 6) {
      ++saveCount;
      char path[128];
      std::snprintf(path, sizeof(path), "D:/Work/github/avox/tmp/bpt_overlay_%d.png", n);
      saveImagePath(path, gImgBuf);
      printf("[save] %s (landmarks=%d)\n", path, lastLmCount.load());
    }
  }
};

int main(int argc, char* argv[]) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* url = (argc > 1) ? argv[1]
      : "D:/Work/github/avox/platform/godot/tools/src/avatar/test_clips/face-demographics-walking.mp4";
  if (argc > 2) {
    if (std::sscanf(argv[2], "%dx%d", &gOutW, &gOutH) != 2 || gOutW <= 0 || gOutH <= 0) {
      printf("bad WxH '%s', use default %dx%d\n", argv[2], gOutW, gOutH);
      gOutW = 640;
      gOutH = 360;
    }
  }
  if (!canVulkan()) {
    printf("[err] Vulkan 不可用, 退出\n");
    return 1;
  }
  const char* fe = getenv("BPT_FEED_EVERY");
  if (fe && fe[0]) {
    int v = std::atoi(fe);
    if (v > 0) {
      gFeedEvery = v;
    }
  }
  printf("[init] url=%s readback=%dx%d feed-every=%d\n", url, gOutW, gOutH, gFeedEvery);

  gMp = createMediaPlayer();
  gMp->getOption()->setInt("mp.synctype", 2);
  gImgBuf = createImageBuffer();
  ImageFormat fmt = {};
  fmt.width = gOutW;
  fmt.height = gOutH;
  fmt.imageType = ImageType::rgba8;
  gImgBuf->setImageFormat(fmt);

  // 视频->身体 推理器 (mediapipe_body)
  gBody = createBody("mediapipe_body");
  if (!gBody) {
    printf("[err] createBody 返回 nullptr (avox_avatar 插件未加载? bodyHub 未注册?)\n");
  }

  ISurfaceRender* render = gMp->getSurfaceRender();
  render->setSurface(nullptr);
  static RenderOb renderOb;
  addSurfaceRenderOb(render, &renderOb);
  render->enableImage(gImgBuf);

  const char* noGeo = getenv("BPT_NO_GEO");
  if (!noGeo || noGeo[0] == '0') {
    gGeo = enableRenderGeometry(render);
    if (!gGeo) {
      printf("[warn] enableRenderGeometry 返回 nullptr\n");
    } else {
      gGeo->setColor(0.0f, 1.0f, 0.0f);
      gGeo->setThreshold(0.5f);
    }
  } else {
    printf("[geo] BPT_NO_GEO=1, 几何叠加关闭\n");
  }

  if (gBody) {
    static BodyOb bodyOb;
    addBodyOb(gBody, &bodyOb);
    gBody->start();
  }

  gMp->setIoPlan(IoPlan::ffmpeg);
  gMp->open(url);

  int frameCap = 0;
  const char* fc = getenv("BPT_FRAMES");
  if (fc && fc[0]) {
    frameCap = std::atoi(fc);
  }
  printf("[geo] overlay=%s (ESC 退出, S 存帧)\n", gGeo ? "on" : "off");
  bool running = true;
  MSG msg;
  while (running) {
    if (frameCap > 0 && renderOb.frameCount.load() > frameCap) {
      printf("[diag] reached %d render frames, exiting\n", frameCap);
      running = false;
      break;
    }
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = false;
        break;
      }
      if (msg.message == WM_KEYDOWN) {
        if (msg.wParam == VK_ESCAPE) {
          running = false;
        } else if (msg.wParam == 'S') {
          saveImagePath("bodypose_frame.png", gImgBuf);
          printf("[save] bodypose_frame.png\n");
        }
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  if (gBody) {
    gBody->stop();
    removeBodyOb(gBody, nullptr);
  }
  render->disableImage();
  removeSurfaceRenderOb(render, &renderOb);
  if (gGeo) {
    disableRenderGeometry(render);
  }
  gMp->close();
  delete gImgBuf;
  return 0;
}

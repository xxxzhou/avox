// facelandmarktest: 视频驱动 avatar 可视化测试。
// 放视频 (MediaPlayer + Vulkan SurfaceRender) + enableImage 回读每帧 rgba8 ->
//   feed IVideoFace (avox_avatar 的 mediapipe 后端: BlazeFace 检测 -> 眼角对齐 warp ->
//   landmarker 478 点 -> blendshape) -> 用 ISurfaceRender 几何渲染 (IGeometryLayer)
//   把检测到的 478 个 landmark 点叠加到画面上看效果。
//
// 用法: facelandmarktest [url] [WxH]
//   url : 视频路径 (默认 tmp/0202_novoice.mp4, 见下)
//   WxH : enableImage 回读分辨率 (默认 640x360; 须与源同比例以使叠加对齐)
// 依赖: avox_avatar + avox_onnx 插件 (createVideoFace -> ensureStarted 自动扫描 plugins/ 加载)
// 源说明: 原 0202.mp4 带 5.1 声道 AAC, 其 ASC 配置 fdk-aac 拒收(解码失败)并阻塞 IO 线程,
//   导致视频卡死(非音频主时钟问题, 视频主时钟也救不了 —— IO 被音频队列顶住)。临时解法:
//   ffmpeg -i 0202.mp4 -c:v copy -an tmp/0202_novoice.mp4 (抽掉音轨, 视频流照搬)。
// 键: ESC 退出; S 存当前帧 PNG。
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "avox/Avox.hpp"
#include "avox/AvoxAvatar.h"     // createVideoFace / IVideoFace / IVideoFaceOb
#include "avox/AvoxLayer.h"      // ISurfaceRender / ISurfaceRenderOb / addSurfaceRenderOb
#include "avox/AvoxPlayer.h"     // createMediaPlayer / IMediaPlayer / IoPlan
#include "avox/AvoxBase.h"     // IOption (mp.synctype -> 视频主时钟)
#include "avox/AvoxVideo.h"      // createImageBuffer / IImageBuffer / ImageFormat / ImageType
#include "avox_vulkan/VkExport.h"  // enableRenderGeometry / IGeometryLayer / canVulkan

#ifdef _WIN32
#include <windows.h>
#endif

using namespace avox;

static IMediaPlayer* gMp = nullptr;
static IImageBuffer* gImgBuf = nullptr;
static IVideoFace* gFace = nullptr;
static IGeometryLayer* gGeo = nullptr;
static int32_t gOutW = 640;
static int32_t gOutH = 360;
// feed 节流: 每 N 个渲染帧喂一帧给脸。3 个 ONNX(det+lm+bs) 各占 4 CPU 线程,
// 喂满每帧会饿死渲染线程(Vulkan/DX11 interop 也要 CPU); RingBuffer cap8 丢老帧,
// 跳帧只降 landmark 刷新率不累积延迟。FLT_FEED_EVERY 可覆盖。
static int gFeedEvery = 4;

// 最新 landmarks: worker 线程 (MediaPipeFace::onRunTask) 经 onFaceLandmarks 写,
// 渲染线程 (onRender) 读 -> 画点。双线程, 加锁。
static struct LatestLandmarks {
  std::mutex mtx;
  std::vector<float> data;  // count*2 交错 (x,y), 像素坐标在 feed 帧空间
  int32_t count = 0;
  int32_t imgW = 0;
  int32_t imgH = 0;
  int64_t ptsMs = 0;
} gLm;

// IVideoFace 观察者: 收 landmarks 入缓存
class FaceOb : public IVideoFaceOb {
 public:
  void onFaceDesc(const VideoFaceDesc& desc) override {
    printf("[face] 就绪: bs=%d lm=%d\n", desc.blendshapeCount, desc.landmarkCount);
  }
  void onFaceLandmarks(const AvoxData& rawPts, int32_t count,
                       int32_t imgW, int32_t imgH, int64_t pts) override {
    static std::atomic<int> calls{0};
    static std::atomic<int> hitCalls{0};
    int c = ++calls;
    // 前 3 次 + 首次检出 3 次打印 (确认管线跑通 + 坐标范围), 之后静默
    bool bPrint = (c <= 3);
    if (count > 0) {
      int h = ++hitCalls;
      if (h <= 3) {
        bPrint = true;
      }
    }
    if (bPrint) {
      const float* p = (count > 0 && rawPts.data) ? reinterpret_cast<const float*>(rawPts.data) : nullptr;
      printf("[face-lm] call#%d count=%d %dx%d%s\n",
             c, count, imgW, imgH, (p && count > 0) ? "" : " (no-face)");
      if (p && count > 0) {
        printf("   lm[0]=(%.1f,%.1f) lm[10]=(%.1f,%.1f) lm[478]=(%.1f,%.1f)\n",
               p[0], p[1], p[20], p[21], p[954], p[955]);
      }
    }
    std::lock_guard<std::mutex> lock(gLm.mtx);
    if (count > 0 && rawPts.data && rawPts.size >= count * 2 * int32_t(sizeof(float))) {
      const float* p = reinterpret_cast<const float*>(rawPts.data);
      gLm.data.assign(p, p + count * 2);
    } else {
      gLm.data.clear();
    }
    gLm.count = count;
    gLm.imgW = imgW;
    gLm.imgH = imgH;
    gLm.ptsMs = pts;
  }
  void onFaceError(const char* err) override {
    printf("[face][err] %s\n", err ? err : "(null)");
  }
};

// ISurfaceRender 观察者: 每帧 (1) feed 回读帧给脸 (2) 用最新 landmarks 画点
class RenderOb : public ISurfaceRenderOb {
 public:
  std::atomic<int> frameCount{0};
  std::atomic<int> lastLmCount{0};
  int feedCounter = 0;
  int saveCount = 0;
  void onRender() override {
    int n = ++frameCount;
    // 心跳: 前 5 帧 + 每 300 帧 (确认 onRender 持续触发 + landmark 刷新)
    if (n <= 5 || n % 300 == 0) {
      printf("[render] frame %d, landmarks=%d\n", n, lastLmCount.load());
    }
    // (1) 回读帧 -> feed (节流: feed 仅深拷贝入队非阻塞, 但 worker 跑 3 ONNX 很重,
    // 喂太密会饿死渲染线程)。绝不在 onRender 调 setScale —— 它触发 resetGraph 重建
    // pipegraph 会挂死渲染线程 (前次卡死根因)。
    if (gFace && gImgBuf && (++feedCounter % gFeedEvery == 0)) {
      gFace->feed(gImgBuf, 0);
    }
    // (2) 叠加最新 landmarks。每帧只 clear+drawPoint (仅置 dirty, 廉价)。
    if (gGeo) {
      gGeo->clear();
      std::lock_guard<std::mutex> lock(gLm.mtx);
      lastLmCount.store(gLm.count);
      if (gLm.count > 0 && gLm.imgW > 0 && gLm.imgH > 0 && !gLm.data.empty()) {
        float iw = float(gLm.imgW), ih = float(gLm.imgH);
        int cnt = int(gLm.data.size() / 2);
        for (int i = 0; i < cnt; ++i) {
          float nx = gLm.data[i * 2 + 0] / iw;  // 归一化 [0,1] (geometry 层坐标空间)
          float ny = gLm.data[i * 2 + 1] / ih;
          gGeo->drawPoint(nx, ny, 2.0f);          // radius (1080p 基准帧像素)
        }
      }
    }
    // 存带叠加的回读图 (回读 buf 含几何叠加: imageResizeLayer 在 geometryLayer 之后)。
    // 仅在检出脸时存前 4 帧, 供目视核对绿点是否对齐人脸。FLT_NOSAVE=1 跳过。
    if (gImgBuf && lastLmCount.load() > 0 && !getenv("FLT_NOSAVE") && saveCount < 4) {
      ++saveCount;
      char path[128];
      std::snprintf(path, sizeof(path), "D:/Work/github/avox/tmp/flt_overlay_%d.png", n);
      saveImagePath(path, gImgBuf);
      printf("[save] %s (landmarks=%d)\n", path, lastLmCount.load());
    }
  }
};

int main(int argc, char* argv[]) {
  setvbuf(stdout, nullptr, _IONBF, 0);  // 重定向到文件时也即时 flush (诊断插件/模型加载)
  const char* url = (argc > 1) ? argv[1] : "D:/Work/github/avox/tmp/0202_novoice.mp4";
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
  // feed 节流可经 FLT_FEED_EVERY 覆盖 (默认 4)
  const char* fe = getenv("FLT_FEED_EVERY");
  if (fe && fe[0]) {
    int v = std::atoi(fe);
    if (v > 0) {
      gFeedEvery = v;
    }
  }
  printf("[init] url=%s readback=%dx%d feed-every=%d\n", url, gOutW, gOutH, gFeedEvery);

  gMp = createMediaPlayer();
  // 视频主时钟: 某些源(如 0202.mp4)AAC ASC 非法 -> fdk-aac 解码失败, 默认 audio-master
  // 时钟因音频不推进而卡死视频(WindowRender 不再 tick)。切视频主时钟, 视频自走帧。
  gMp->getOption()->setInt("mp.synctype", 2);
  // enableImage 回读缓冲 (调用方持有, 生命周期到 disableImage 之后)
  gImgBuf = createImageBuffer();
  ImageFormat fmt = {};
  fmt.width = gOutW;
  fmt.height = gOutH;
  fmt.imageType = ImageType::rgba8;  // enableImage 仅支持 rgba8
  gImgBuf->setImageFormat(fmt);

  // 视频->面部 推理器 (createVideoFace -> ensureStarted 自动扫描 plugins/ 加载 avox_avatar + avox_onnx)
  gFace = createVideoFace(VideoFaceType::mediapipe);
  if (!gFace) {
    printf("[err] createVideoFace 返回 nullptr (avox_avatar 插件未加载? plugins/avox_avatar.dll 缺失?)\n");
  }

  // 渲染 (Vulkan 后端 setSurface(nullptr) 自建窗口) + 几何叠加
  ISurfaceRender* render = gMp->getSurfaceRender();
  render->setSurface(nullptr);
  static RenderOb renderOb;
  addSurfaceRenderOb(render, &renderOb);
  render->enableImage(gImgBuf);  // 每帧 GPU 缩放零拷贝写 buf, onRender 内同帧可读
  // 几何叠加可经 FLT_NO_GEO=1 关闭 (隔离"检测链路"与"叠加渲染"两件事)。
  // 注: 开启几何时前 ~30 帧(enableImage 回读图初始化期间)会拿到均匀灰, 之后变真实。
  const char* noGeo = getenv("FLT_NO_GEO");
  if (!noGeo || noGeo[0] == '0') {
    gGeo = enableRenderGeometry(render);
    if (!gGeo) {
      printf("[warn] enableRenderGeometry 返回 nullptr (几何叠加不可用)\n");
    } else {
      // 颜色/阈值一次性设好 (仅 paramDirty, 廉价, 不重建 graph)。
      // setScale 会触发 resetGraph, 留给默认值, 不在 setup/onRender 调。
      gGeo->setColor(0.0f, 1.0f, 0.0f);  // 绿点
      gGeo->setThreshold(0.5f);
    }
  } else {
    printf("[geo] FLT_NO_GEO=1, 几何叠加关闭\n");
  }

  if (gFace) {
    static FaceOb faceOb;
    addVideoFaceOb(gFace, &faceOb);
    gFace->start();
  }

  gMp->setIoPlan(IoPlan::ffmpeg);
  gMp->open(url);

  // Win32 消息循环 (Vulkan 后端自拥 HWND, 宿主只需泵消息)
  // 默认跑到 ESC; FLT_FRAMES=N 限时自退 (诊断用, 干净退出保 stdout pipe 缓冲)
  int frameCap = 0;
  const char* fc = getenv("FLT_FRAMES");
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
          saveImagePath("facelandmark_frame.png", gImgBuf);
          printf("[save] facelandmark_frame.png\n");
        }
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // teardown (顺序: 先停推理/解绑, 再关渲染)
  if (gFace) {
    gFace->stop();
    removeVideoFaceOb(gFace, nullptr);  // ob 是 static, 进程将退, 省略精确移除
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

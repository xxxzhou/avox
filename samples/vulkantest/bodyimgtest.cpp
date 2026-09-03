// bodyimgtest: 用单张人体图验证 MediaPipeBody 的 33 点点位 (避开 enableImage 回读图不一致)。
// 加载 jpg -> rgba8 IImageBuffer -> body->feed -> 收集 33 点 -> ImageRender 显示原图 + IGeometryLayer 画点。
// 若此处点位贴合人体, 则问题在 bodyposetest 的 enableImage 回读图; 若仍乱, 则 pipeline 本身有误。
//
// 用法: bodyimgtest [image_path]
// 键: ESC 退出
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "avox/Avox.hpp"
#include "avox/AvoxAvatar.h"     // createBody / IBody / IBodyOb
#include "avox/AvoxLayer.h"      // IImageRender / ISurfaceRender / IGeometryLayer
#include "avox/AvoxVideo.h"      // createImageBuffer / IImageBuffer / ImageFormat / ImageType
#include "avox/AvoxImage.h"      // saveImagePath
#include "avox_vulkan/VkExport.h"  // canVulkan / enableRenderGeometry

#ifdef _WIN32
#include <windows.h>
#endif

using namespace avox;

static std::mutex gMtx;
static std::vector<float> gLm;  // 33*3
static std::atomic<int> gCount{0};
static int gW = 0, gH = 0;

class BodyOb : public IBodyOb {
 public:
  void onBodyDesc(const BodyDesc& desc) override {
    printf("[body] 就绪 lm=%d\n", desc.landmarkCount);
  }
  void onBodyLandmarks(const AvoxData& raw, int32_t count,
                       int32_t imgW, int32_t imgH, int64_t) override {
    std::lock_guard<std::mutex> lock(gMtx);
    gW = imgW;
    gH = imgH;
    gCount.store(count);
    gLm.clear();
    if (count > 0 && raw.data && raw.size >= count * 4 * int32_t(sizeof(float))) {
      const float* p = reinterpret_cast<const float*>(raw.data);
      gLm.assign(p, p + count * 4);
      const int keys[] = {0, 11, 12, 13, 14, 23, 24, 25, 27};
      printf("[body-lm] count=%d %dx%d\n", count, imgW, imgH);
      for (int k : keys) {
        printf("   lm[%d]=%.1f,%.1f (conf=%.2f)\n", k, p[k*4], p[k*4+1], p[k*4+3]);
      }
    } else {
      printf("[body-lm] no-person\n");
    }
  }
  void onBodyError(const char* err) override {
    printf("[body][err] %s\n", err ? err : "(null)");
  }
};

int main(int argc, char* argv[]) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* path = (argc > 1) ? argv[1]
      : "D:/Work/github/avox/platform/godot/tools/src/avatar/test_clips/body_frame.png";
  cv::Mat bgr = cv::imread(path);
  if (bgr.empty()) {
    printf("[err] 无法加载图 %s\n", path);
    return 1;
  }
  cv::Mat rgba;
  cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
  int W = rgba.cols, H = rgba.rows;
  printf("[init] %s %dx%d\n", path, W, H);

  // rgba8 IImageBuffer
  IImageBuffer* buf = createImageBuffer();
  ImageFormat fmt = {};
  fmt.width = W;
  fmt.height = H;
  fmt.imageType = ImageType::rgba8;
  fmt.rowPitch = W * 4;
  buf->setImageFormat(fmt);
  uint8_t* dst = buf->getPointer();
  if (dst) {
    memcpy(dst, rgba.data, size_t(W) * H * 4);
  }

  // 推理
  IBody* body = createBody("mediapipe_body");
  if (!body) {
    printf("[err] createBody nullptr\n");
    delete buf;
    return 1;
  }
  static BodyOb ob;
  addBodyOb(body, &ob);
  body->start();
  body->feed(buf, 1);
  // 等待回调 (最多 10s)
  auto t0 = std::chrono::steady_clock::now();
  while (gCount.load() == 0 &&
         std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (gCount.load() <= 0) {
    printf("[err] 未收到 landmark\n");
  }

  // ImageRender 显示原图 + 画点
  IImageRender* ir = createImageRender();
  ISurfaceRender* wr = ir->getSurfaceRender();
  wr->setSurface(nullptr);
  IGeometryLayer* geo = enableRenderGeometry(wr);
  if (geo) {
    geo->setColor(0.0f, 1.0f, 0.0f);
    geo->setThreshold(0.5f);
    geo->clear();
    {
      std::lock_guard<std::mutex> lock(gMtx);
      if (gCount.load() > 0 && !gLm.empty()) {
        int n = int(gLm.size() / 4);
        for (int i = 0; i < n; ++i) {
          float nx = gLm[i * 4 + 0] / float(gW ? gW : W);
          float ny = gLm[i * 4 + 1] / float(gH ? gH : H);
          geo->drawPoint(nx, ny, 8.0f);
        }
      }
    }
  }
  ir->render(buf);
  printf("[img] render, 2s 后自动退出 (ESC 也可)\n");

  // 用 OpenCV 直接在源图上画点 (不依赖 GPU screenShot), 保存成 PNG 供查看点位
  {
    cv::Mat bgr_draw = bgr.clone();
    std::lock_guard<std::mutex> lock(gMtx);
    int iw = (gW ? gW : W), ih = (gH ? gH : H);
    if (gCount.load() > 0 && !gLm.empty()) {
      int n = int(gLm.size() / 4);
      for (int i = 0; i < n; ++i) {
        cv::circle(bgr_draw, cv::Point(int(gLm[i*4+0]), int(gLm[i*4+1])), 6,
                   cv::Scalar(0, 255, 0), -1);
      }
      // 骨段连线
      static const int bones[][2] = {
          {0,2},{2,5},{5,7},{7,9},{9,11},{11,13},{13,15},
          {0,1},{1,3},{3,6},{6,8},{8,10},{10,12},{12,14},{14,16},
          {5,6},{5,11},{6,12},{11,12},{11,23},{12,24},{23,24},
          {23,25},{25,27},{27,29},{29,31},{24,26},{26,28},{28,30},{30,32}};
      for (const auto& b : bones) {
        cv::line(bgr_draw,
                 cv::Point(int(gLm[b[0]*4+0]), int(gLm[b[0]*4+1])),
                 cv::Point(int(gLm[b[1]*4+0]), int(gLm[b[1]*4+1])),
                 cv::Scalar(0, 255, 255), 2);
      }
      const char* out = "D:/Work/github/avox/tmp/bodyimg_overlay.png";
      cv::imwrite(out, bgr_draw);
      printf("[save] %s (iw=%d ih=%d)\n", out, iw, ih);
    } else {
      printf("[err] 无 landmark 可画\n");
    }
  }

  bool running = true;
  MSG msg;
  auto tStart = std::chrono::steady_clock::now();
  while (running) {
    if (std::chrono::steady_clock::now() - tStart > std::chrono::seconds(3)) running = false;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) running = false;
      if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) running = false;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  body->stop();
  removeBodyOb(body, nullptr);
  disableRenderGeometry(wr);
  delete buf;
  return 0;
}

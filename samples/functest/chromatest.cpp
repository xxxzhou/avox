// 色度升采样探针(画质深化方案 §5 T1 验收):
//   同素材、同参数, 分两次进程分别走「平台原生腿」(本机 mac=Metal, 已是线性采样)
//   与「-vk 强制 VK 腿」(MoltenVK), 各截图 PNG 并逐像素打印指定行的 RGB ——
//   用于 1) 跨腿一致性比对(判据 1) 2) 最近邻 vs 双线性的边沿剖面判别。
// 内置判据: 彩边素材在 2:1 色度子采样下, 双线性升采样在色度样本边界两侧给出
//   25%/75% 与 75%/25% 的混合(RGB 通道落在两端点间 ~25% 处), 最近邻则整块跳变。
// 用法: chromatest <url> [-vk] [-hard] [-row N] [-x0 A] [-x1 B] [-col X] [-prefix P]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "avox/AvoxPlayer.h"

using namespace avox;

namespace {

int g_frames = 0;

}  // namespace

class ChromaOb : public ISurfaceRenderOb {
 public:
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    if (buf && buf->getPointer()) {
      g_frames++;
    }
  }
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::printf("usage: chromatest <url> [-vk] [-hard] [-row N] [-x0 A] [-x1 B]"
                " [-col X] [-cy0 A] [-cy1 B] [-prefix P]\n");
    return 2;
  }
  const char* url = argv[1];
  bool vk = false;
  bool hard = false;
  int row = 60;
  int x0 = 280;
  int x1 = 360;
  int col = -1;
  int cy0 = 170;
  int cy1 = 192;
  std::string prefix = "chroma_";
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-vk") {
      vk = true;
    } else if (a == "-hard") {
      hard = true;
    } else if (a == "-row" && i + 1 < argc) {
      row = atoi(argv[++i]);
    } else if (a == "-x0" && i + 1 < argc) {
      x0 = atoi(argv[++i]);
    } else if (a == "-x1" && i + 1 < argc) {
      x1 = atoi(argv[++i]);
    } else if (a == "-col" && i + 1 < argc) {
      col = atoi(argv[++i]);
    } else if (a == "-cy0" && i + 1 < argc) {
      cy0 = atoi(argv[++i]);
    } else if (a == "-cy1" && i + 1 < argc) {
      cy1 = atoi(argv[++i]);
    } else if (a == "-prefix" && i + 1 < argc) {
      prefix = argv[++i];
    }
  }
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    std::printf("[FAIL] createMediaPlayer null\n");
    return 1;
  }
  ISurfaceRender* sr = player->getSurfaceRender();
  // 腿选择: 不设=平台默认(本机 mac=Metal); -vk 显式走 Vulkan(MoltenVK)
  sr->setVulkan(vk);
  // 解码档与腿强相关(两平台要求相反), 默认软解:
  //   mac 测 VK 腿(默认): 必须**软解** —— 硬解(VT)出 CVPixelBuffer 恒经 Metal 腿, 到不了
  //     VK 的 yuv2rgba 色度车道(V1/V5/V6);
  //   Windows 测 DX11 CS: 必须 `-hard`(硬解) —— CS 的软解上传路(initGraphCpu)要求
  //     targetWindow 的 DX11 设备, 无窗口宿主直接 "no window device, skip"(实测踩过:
  //     软解下 CS 全程 skip, 出图来自别的腿, 剖面仍是最近邻, 会误判成"改动没生效");
  //     硬解走 renderToTexture(解码器设备), 不需要窗口。
  player->setHardDecode(hard);
  sr->setOffSurface(YuvType::yuv420P);
  ChromaOb ob;
  addSurfaceRenderOb(sr, &ob);
  // enableImage 订阅是帧流驱动口: 不订阅则该腿不出帧(screenShot 也会超时), 与 hdrtest 同款
  IImageBuffer* sutImg = createImageBuffer();
  ImageFormat ifmt = {};
  ifmt.width = 640;
  ifmt.height = 360;
  ifmt.imageType = ImageType::rgba8;
  sutImg->setImageFormat(ifmt);
  sr->enableImage(sutImg);
  player->open(url);
  for (int i = 0; i < 60 && g_frames < 20; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  const char* leg = vk ? "vk" : "native";
  std::printf("[chroma] leg=%s frames=%d\n", leg, g_frames);
  // 取渲染腿输出口像素(与 hdrtest 同口径: onFrame 的 CPU-YUV 通道在 mac 实测是
  // 解码直出, 到不了色彩/色度处理输出; screenShot 走真输出口)
  IImageBuffer* shot = createImageBuffer();
  ImageFormat sf = {};
  sf.width = 640;
  sf.height = 360;
  sf.imageType = ImageType::rgba8;
  shot->setImageFormat(sf);
  int rc = 1;
  if (sr->screenShot(shot)) {
    std::string path = prefix + leg + ".png";
    saveImagePath(path.c_str(), shot);
    const unsigned char* p = (const unsigned char*)shot->getPointer();
    ImageFormat f2 = shot->getImageFormat();
    if (p && f2.width > 0 && f2.height > 0) {
      int pitch = f2.rowPitch > 0 ? f2.rowPitch : f2.width * 4;
      int r = row < f2.height ? row : f2.height / 2;
      const unsigned char* rp = p + (size_t)r * pitch;
      std::printf("[chroma] shot=%s %dx%d row=%d\n", path.c_str(), f2.width,
                  f2.height, r);
      for (int c = x0; c < x1 && c < f2.width; c++) {
        std::printf("%d:(%d,%d,%d)%s", c, rp[c * 4], rp[c * 4 + 1],
                    rp[c * 4 + 2], (c % 8 == 7) ? "\n" : " ");
      }
      std::printf("\n");
      // 纵向剖面(4:2:0 垂直样本边界): 同一列逐行打印
      if (col >= 0 && col < f2.width) {
        std::printf("[chroma] col=%d\n", col);
        for (int yy = cy0; yy < cy1 && yy < f2.height; yy++) {
          const unsigned char* q = p + (size_t)yy * pitch;
          std::printf("%d:(%d,%d,%d)%s", yy, q[col * 4], q[col * 4 + 1],
                      q[col * 4 + 2], (yy % 8 == 7) ? "\n" : " ");
        }
        std::printf("\n");
      }
      rc = 0;
    }
  } else {
    std::printf("[chroma] screenShot failed (leg=%s)\n", leg);
  }
  delete shot;
  sr->disableImage();
  removeSurfaceRenderOb(sr, &ob);
  delete sutImg;
  delete player;
  std::printf("case=chromatest %s leg=%s frames=%d\n", rc == 0 ? "PASS" : "FAIL",
              leg, g_frames);
  return rc;
}

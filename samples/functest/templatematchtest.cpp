/**
 * @file templatematchtest.cpp
 * @brief 模板匹配测试 (ITemplateMatcher)
 *
 * 在场景图中定位模板/图标位置 (供 Agent 点击)。
 * 无参运行: 使用合成测试图自测 (800x600 场景, 目标 60x40 红块 @ (200,150))
 *
 * 用法:
 *   templatematchtest                                  # 合成图自测
 *   templatematchtest -s scene.png -t icon.png
 *   templatematchtest -s scene.png -t icon.png -e 0.8 -m 3
 *   templatematchtest -s scene.png -t icon.png -g      # green_mask
 *   templatematchtest -s scene.png -t icon.png -r 100,100,400,300
 */

#include <iostream>
#include <string>
#include <cstring>
#include <cstdio>
#include <memory>

#include "avox/AvoxVision.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/AvoxVideo.h"

using namespace avox;

struct Opts {
  std::string scene;
  std::string tmpl;
  double threshold = 0.7;
  int method = 0;  // 0=none(默认 ccoeffNormed), 1=sqdiffNormed, 2=ccorrNormed, 3=ccoeffNormed
  bool green = false;
  int roi[4] = {0, 0, 0, 0};
  bool useRoi = false;
};

static void printHelp(const char* prog) {
  std::cout << "用法: " << prog << " [选项]\n\n"
            << "选项:\n"
            << "  -s <path>      场景图路径 (省略则用合成图自测)\n"
            << "  -t <path>      模板图路径 (省略则用合成图自测)\n"
            << "  -e <double>    阈值 [0,1], 越大越严 (默认 0.7)\n"
            << "  -m <int>       方法: 0=none(默认) 1=sqdiffNormed 2=ccorrNormed 3=ccoeffNormed\n"
            << "  -g             开启 green_mask (模板纯绿区域当透明)\n"
            << "  -r x,y,w,h     感兴趣区域\n"
            << "  -h             帮助\n"
            << std::endl;
}

static bool parseArgs(int argc, char* argv[], Opts& opts) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h") {
      printHelp(argv[0]);
      return false;
    }
    if (a == "-s" && i + 1 < argc) { opts.scene = argv[++i]; continue; }
    if (a == "-t" && i + 1 < argc) { opts.tmpl = argv[++i]; continue; }
    if (a == "-e" && i + 1 < argc) { opts.threshold = std::stod(argv[++i]); continue; }
    if (a == "-m" && i + 1 < argc) { opts.method = std::stoi(argv[++i]); continue; }
    if (a == "-g") { opts.green = true; continue; }
    if (a == "-r" && i + 1 < argc) {
      std::string r = argv[++i];
      if (sscanf(r.c_str(), "%d,%d,%d,%d", &opts.roi[0], &opts.roi[1], &opts.roi[2],
                 &opts.roi[3]) == 4)
        opts.useRoi = true;
      continue;
    }
    std::cerr << "未知参数: " << a << "\n";
    printHelp(argv[0]);
    return false;
  }
  return true;
}

// 在 buffer 的 (x0,y0) 处绘制 60x40 纹理目标: 填充色块(fr,fg,fb) + 内部矩形(ir,ig,ib)
// (非对称, 模板匹配可精确定位)
static void drawPattern(IImageBuffer* buf, int x0, int y0, uint8_t fr, uint8_t fg,
                        uint8_t fb, uint8_t ir, uint8_t ig, uint8_t ib) {
  ImageFormat fmt = buf->getImageFormat();
  int rowBytes = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * 4;  // rgba8 = 4 字节/像素
  uint8_t* p = buf->getPointer();
  for (int dy = 0; dy < 40; ++dy)
    for (int dx = 0; dx < 60; ++dx) {
      uint8_t* px = p + (y0 + dy) * rowBytes + (x0 + dx) * 4;
      px[0] = fr;
      px[1] = fg;
      px[2] = fb;
      px[3] = 255;
    }
  for (int dy = 10; dy < 25; ++dy)
    for (int dx = 10; dx < 30; ++dx) {
      uint8_t* px = p + (y0 + dy) * rowBytes + (x0 + dx) * 4;
      px[0] = ir;
      px[1] = ig;
      px[2] = ib;
      px[3] = 255;
    }
}

// 合成场景 (800x600 深灰), 两个不同纹理目标: 图案A(红块+蓝内) @ (200,150), 图案B(绿块+红内) @ (500,400)
// 模板A = 图案A, 模板B = 图案B (用于验证多模板时 templateIndex 区分来源)
static void buildSynthetic(IImageBuffer* scene, IImageBuffer* tmplA, IImageBuffer* tmplB) {
  ImageFormat sfmt;
  sfmt.width = 800;
  sfmt.height = 600;
  sfmt.imageType = ImageType::rgba8;
  scene->setImageFormat(sfmt);
  uint8_t* p = scene->getPointer();
  for (int i = 0; i < 800 * 600 * 4; i += 4) {
    p[i] = 40;
    p[i + 1] = 40;
    p[i + 2] = 40;
    p[i + 3] = 255;
  }
  // 图案A: 红块(255,0,0)+蓝内(0,0,255); 图案B: 绿块(0,255,0)+红内(255,0,0)
  drawPattern(scene, 200, 150, 255, 0, 0, 0, 0, 255);
  drawPattern(scene, 500, 400, 0, 255, 0, 255, 0, 0);
  ImageFormat tfmt;
  tfmt.width = 60;
  tfmt.height = 40;
  tfmt.imageType = ImageType::rgba8;
  tmplA->setImageFormat(tfmt);
  drawPattern(tmplA, 0, 0, 255, 0, 0, 0, 0, 255);
  tmplB->setImageFormat(tfmt);
  drawPattern(tmplB, 0, 0, 0, 255, 0, 255, 0, 0);
}

int main(int argc, char* argv[]) {
  std::cout << "=== 模板匹配测试 (ITemplateMatcher) ===\n";
  Opts opts;
  if (!parseArgs(argc, argv, opts)) return 0;

  // 查表拿 matcher (create 内部 ensureStarted 触发 avox_opencv 加载; OpenCV 缺失时返回 nullptr)
  std::unique_ptr<ITemplateMatcher> matcher(AvoxManager::Get().templateMatcherHub.create("opencv"));
  if (!matcher) {
    std::cerr << "创建 TemplateMatcher 失败 (avox_opencv 插件未加载?)\n";
    return 1;
  }

  // 配置
  if (opts.method >= 1 && opts.method <= 3)
    matcher->setMethod(static_cast<TemplateMatchMethod>(opts.method));
  matcher->setGreenMask(opts.green);
  if (opts.useRoi) matcher->setRoi(opts.roi[0], opts.roi[1], opts.roi[2], opts.roi[3]);

  // 加载图
  std::unique_ptr<IImageBuffer> scene(createImageBuffer());
  std::unique_ptr<IImageBuffer> tmplA(createImageBuffer());
  std::unique_ptr<IImageBuffer> tmplB(createImageBuffer());
  bool synthetic = opts.scene.empty() || opts.tmpl.empty();
  if (synthetic) {
    buildSynthetic(scene.get(), tmplA.get(), tmplB.get());
    std::cout << "(无参: 合成测试图 800x600, 两个不同纹理目标 @ (200,150) 与 (500,400))\n";
  } else {
    if (!loadImagePath(opts.scene.c_str(), scene.get())) {
      std::cerr << "加载场景图失败: " << opts.scene << "\n";
      return 1;
    }
    if (!loadImagePath(opts.tmpl.c_str(), tmplA.get())) {
      std::cerr << "加载模板图失败: " << opts.tmpl << "\n";
      return 1;
    }
  }

  // 加模板 (合成图加两个不同模板, 验证 templateIndex 区分来源; 真实图只加一个)
  matcher->addTemplate(tmplA.get(), opts.threshold);           // idx 0
  if (synthetic) matcher->addTemplate(tmplB.get(), opts.threshold);  // idx 1
  int n = matcher->match(scene.get());

  std::cout << "命中数量: " << n << "\n";
  std::cout << "耗时: " << matcher->getMatchTimeMs() << " ms\n";
  const char* err = matcher->getLastError();
  if (err && err[0]) std::cout << "提示: " << err << "\n";
  for (int i = 0; i < n; ++i) {
    MatchResult r;
    matcher->getMatch(i, &r);
    vec2i center = getMatchCenter(r);
    std::cout << "  [" << i << "] 框(" << r.x << "," << r.y << "," << r.w << "," << r.h
              << ") score=" << r.score << " tmpl=" << r.templateIndex
              << " 中心(" << center.x << "," << center.y << ")\n";
  }
  return 0;
}

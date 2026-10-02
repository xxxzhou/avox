

#include "Dx11CSVideoRender.hpp"

#include <cstdlib>
#include <string>

#include "Dx11ShaderCache.hpp"
#include "Dx11Window.hpp"
#include "DxCanvasLayer.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/video/ColorSpace.hpp"

#if AVOX_ENABLE_FFMPEG
#include "avox_ffmpeg/FFCommon.hpp"
#endif

#pragma comment(lib, "D3DCompiler.lib")

namespace avox {

// YUV->RGBA 计算着色器: 基础/DV 两变体由同一份头+体拼装。DV 整形数学让 FXC
// 冷编译从 ~100ms 涨到 ~1.5s(9/29 实测), 拆出后非 DV 内容首开不付这笔;
// DV 变体在首帧 DV 内容时惰性编译(selectShader), 嵌入分支经 %%DV_BRANCH%% 注入
static const char* kShaderHead = R"(
Texture2D yTex: register(t0);
Texture2D uvTex: register(t1);
RWTexture2D<float4> outTex: register(u0);

// 常量区与 ColorYuvUBO 同布局(96B 头 + DV 区 2848B), 数组为 vec4 展平
cbuffer CBParameters : register(b0)
{
    int   width;         // 输出RGBA宽
    int   height;        // 输出RGBA高
    int   yuvType;       // 0=nv12 1=p010
    int   transfer;      // YuvTransfer 声明序: 0=gamma 1=linear 2=pq 3=hlg
    float4x4 colorMat;   // offset 16: YUV(limited)->RGB, setColorSpace 注入
    float maxLuminance;  // 内容峰值亮度 nits
    float sdrWhiteNits;  // SDR 白点 nits
    int   hdrMode;       // HdrMode 声明序: 0=follow 1=forceSDR 2=forceHDR
    int   _pad;
    int   doviEnable;    // 1=走 DV 整形链
    int   _dv0;
    int   _dv1;
    int   _dv2;
    float4 dvPivots[7];
    float4 dvPoly[18];
    float4 dvMmr[132];
    int4   dvIdc[6];
    float4 dvNumPivots;
    float4 dvNl[3];
    float4 dvNlOff;
    float4 dvLm[3];
};

// YUV 转 RGB: 用 setColorSpace 注入的矩阵(标准 + limited 量程), 与 Vulkan V1/V5 同源
float4 yuv2Rgb(float y, float u, float v, float a) {
    float4 rgb = mul(float4(y, u, v, 1.0f), colorMat);
    return float4(clamp(rgb.rgb, 0.0f, 1.0f), a);
}

float3 pqToLinear(float3 n);
float3 linearToPq(float3 lin);
)";

// DV 变体附加段(整形链语义对齐 libplacebo/V5, 输出 PQ BT.2020)
static const char* kShaderDv = R"(
// UBO 的 dvNl/dvLm 是列主序(dvNl[c][r] = M[r][c], 与 GLSL mat3(列构造)×v = M×v
// 对齐): M×v 的第 r 个分量 = 第 r 行 (c0[r],c1[r],c2[r]) 与 v 的点积。
// 旧实现 dot(c_r, v) 算成了 Mᵀ×v —— 色度行/列互换, G 通道被 U 系数(m01≈0)吞掉,
// 实测硬解 DV 画面变品红(255,0,255), 亮度均值被误读为"整体变暗"。
float3 dvApplyCols(float3 v, float4 c0, float4 c1, float4 c2) {
    return float3(dot(float3(c0[0], c1[0], c2[0]), v),
                  dot(float3(c0[1], c1[1], c2[1]), v),
                  dot(float3(c0[2], c1[2], c2[2]), v));
}

float dvPivotAt(int idx) { return dvPivots[idx >> 2][idx & 3]; }
float dvPolyAt(int idx)  { return dvPoly[idx >> 2][idx & 3]; }
float dvMmrAt(int idx)   { return dvMmr[idx >> 2][idx & 3]; }
int   dvIdcAt(int idx)  { return dvIdc[idx >> 2][idx & 3]; }

float dvReshapeComp(int c, float3 sig, float s) {
    int base = c * 8;
    int sel = -1;
    for (int i = 0; i < 8; i++) {
        if (dvIdcAt(base + i) == 0) break;
        if (i == 7 || s < dvPivotAt(c * 9 + i + 1)) { sel = i; break; }
    }
    if (sel < 0) {
        return s;
    }
    int idc = dvIdcAt(base + sel);
    if (idc == 1) {
        int pf = (base + sel) * 3;
        return (dvPolyAt(pf + 2) * s + dvPolyAt(pf + 1)) * s + dvPolyAt(pf);
    }
    int order = idc - 16;
    int p = (base + sel) * 22;
    int np = (int)dvNumPivots[c];
    float acc = dvMmrAt(p);
    float4 sigX = float4(sig.x * sig.y, sig.x * sig.z, sig.y * sig.z,
                         sig.x * sig.y * sig.z);
    acc += dot(float3(dvMmrAt(p + 1), dvMmrAt(p + 2), dvMmrAt(p + 3)), sig);
    acc += dot(float4(dvMmrAt(p + 4), dvMmrAt(p + 5), dvMmrAt(p + 6), dvMmrAt(p + 7)), sigX);
    if (order >= 2) {
        float3 sig2 = sig * sig;
        float4 sigX2 = sigX * sigX;
        acc += dot(float3(dvMmrAt(p + 8), dvMmrAt(p + 9), dvMmrAt(p + 10)), sig2);
        acc += dot(float4(dvMmrAt(p + 11), dvMmrAt(p + 12), dvMmrAt(p + 13), dvMmrAt(p + 14)), sigX2);
        if (order >= 3) {
            acc += dot(float3(dvMmrAt(p + 15), dvMmrAt(p + 16), dvMmrAt(p + 17)), sig2 * sig);
            acc += dot(float4(dvMmrAt(p + 18), dvMmrAt(p + 19), dvMmrAt(p + 20), dvMmrAt(p + 21)), sigX2 * sigX);
        }
    }
    return clamp(acc, dvPivotAt(c * 9), dvPivotAt(c * 9 + np - 1));
}

// DV 全链: reshape → ycc_to_rgb(PQ域) → PQ线性 → LMS合成阵 → 回编码 PQ BT.2020
// 偏移=输入侧中性值(limited 黑位+chroma 0.5): 先减再进矩阵(同 GLSL/Metal 腿)
float3 dvProcess(float3 yuv) {
    float3 sig = clamp(yuv, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
    sig = float3(dvReshapeComp(0, sig, sig.r), dvReshapeComp(1, sig, sig.g),
                 dvReshapeComp(2, sig, sig.b));
    float3 rgb = dvApplyCols(sig - dvNlOff.xyz, dvNl[0], dvNl[1], dvNl[2]);
    float3 lin = pqToLinear(rgb);
    return linearToPq(dvApplyCols(lin, dvLm[0], dvLm[1], dvLm[2]));
}
)";

// 两变体共用的体: HDR 处理 + 采样 + 入口(DV 分支经 %%DV_BRANCH%% 注入)
static const char* kShaderBody = R"(
// ---- HDR 处理: 与 glsl/yuv2rgbaV5.comp 同源 ----

// PQ EOTF (SMPTE ST2084): 编码值 -> 线性光, 1.0 = 10000 nits
float3 pqToLinear(float3 n) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 p = pow(clamp(n, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0)), 1.0 / m2);
    float3 num = max(p - c1, float3(0.0, 0.0, 0.0));
    return pow(num / (c2 - c3 * p), 1.0 / m1);
}

// PQ OETF (ST2084 逆过程): 线性光 -> 编码值(DV 链尾回编码用)
float3 linearToPq(float3 lin) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 p = pow(max(lin, float3(0.0, 0.0, 0.0)), m1);
    float3 e = (c1 + c2 * p) / (1.0f + c3 * p);
    return pow(e, m2);
}

// HLG 解码(BT.2100): OETF^-1 得场景线性, 再逆 OOTF(1.2, 1000nit 参考屏)
// 输出线性光 1.0 = 1000 nits
float3 hlgToLinear(float3 e) {
    float3 t = clamp(e, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
    float3 lo = t * t / 3.0;
    float3 hi = (exp((t - 0.55991073) / 0.17883277) + 0.28466892) / 12.0;
    float3 scene = lerp(lo, hi, step(0.5, t));
    float ys = dot(scene, float3(0.2627, 0.6780, 0.0593));
    return scene * pow(max(ys, 1e-6), 0.2);
}

// tone map: 线性光(10000nit 归一) -> 显示线性 [0,1]
// BT.2390 观感取向: 锚(sdrWhite)下近似线性透传, 超锚按内容峰值软压(Reinhard 扩展)
float3 toneMap(float3 lin) {
    float dstN = max(sdrWhiteNits, 1.0);
    float white = max(maxLuminance, dstN) / dstN;   // 白点(锚归一)
    float3 d = max(lin * 10000.0 / dstN, float3(0.0, 0.0, 0.0));
    float3 t = d * (1.0 + d / (white * white)) / (1.0 + d);
    return clamp(t, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
}

// BT.2020 -> BT.709 线性域 primaries 转换, 越界分量截断
// (GLSL mat3 列主序构造的转置即行主序三行)
float3 bt2020ToBt709(float3 c) {
    return max(mul(float3x3(
        1.6605, -0.5876, -0.0728,
        -0.1246, 1.1329, -0.1006,
        -0.0182, -0.1006, 1.1187), c), float3(0.0, 0.0, 0.0));
}

// 线性光 -> BT.709 OETF 编码
float3 linearToBt709(float3 c) {
    float3 lo = c * 4.5;
    float3 hi = 1.099 * pow(max(c, float3(0.0, 0.0, 0.0)), 0.45) - 0.099;
    return lerp(lo, hi, step(0.018, c));
}

// 色彩处理总入口: forceHDR 跳过全部处理; 否则仅 PQ/HLG 做变换
float3 processColor(float3 rgb) {
    if (hdrMode == 2) {
        return rgb;
    }
    // DV 链输出恒为 PQ BT.2020, 与容器标签无关(P5 无色彩标签时 transfer=0):
    // 不覆盖则走 SDR 直通, DV 输出被当 gamma 直显 → 偏暗欠饱和
    int xfer = (doviEnable == 1) ? 2 : transfer;
    if (xfer == 2) {
        float3 lin = pqToLinear(rgb);
        lin = toneMap(lin);
        lin = bt2020ToBt709(lin);
        return linearToBt709(lin);
    }
    if (xfer == 3) {
        float3 lin = hlgToLinear(rgb) * 0.1;
        lin = toneMap(lin);
        lin = bt2020ToBt709(lin);
        return linearToBt709(lin);
    }
    return rgb;
}

// P010: 16-bit 视图采样(R16_UNORM/R16G16_UNORM), 高 10 位有效, BT.2020 tv-range
float4 p010Point(uint2 pix, float a) {
    float k = 65535.0 / 64.0 / 1023.0;  // R16_UNORM 值 -> 10bit 归一
    float y = yTex.Load(int3(pix, 0)).r * k;
    float2 uvRaw = uvTex.Load(int3(pix.x / 2, pix.y / 2, 0)).rg;
    // DV 链吃原始 PQ 信号(不做 limited 展开, 偏移在 DV 矩阵里), 其余走原路
%%DV_BRANCH%%
    float u = uvRaw.x * k - 0.5;
    float v = uvRaw.y * k - 0.5;
    // BT.2020 tv-range 展开(Y 64..940, UV 512 居中) 后矩阵
    float yy = saturate((y - 64.0 / 1023.0) / (876.0 / 1023.0));
    float uu = u * (876.0 / 896.0);
    float vv = v * (876.0 / 896.0);
    float r = yy + 1.4746 * vv;
    float g = yy - 0.164553 * uu - 0.571353 * vv;
    float b = yy + 1.8814 * uu;
    return float4(saturate(processColor(float3(r, g, b))), a);
}

[numthreads(16, 16, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    uint2 size = uint2(width, height);
    if(DTid.x >= size.x/2 || DTid.y >= size.y/2){
        return;
    }
    if (yuvType == 1) {
        float4 o1 = p010Point(uint2(DTid.x*2, DTid.y*2), 1.0f);
        float4 o2 = p010Point(uint2(DTid.x*2+1, DTid.y*2), 1.0f);
        float4 o3 = p010Point(uint2(DTid.x*2, DTid.y*2+1), 1.0f);
        float4 o4 = p010Point(uint2(DTid.x*2+1, DTid.y*2+1), 1.0f);
        outTex[int2(DTid.x*2, DTid.y*2)] = o1;
        outTex[int2(DTid.x*2+1, DTid.y*2)] = o2;
        outTex[int2(DTid.x*2, DTid.y*2+1)] = o3;
        outTex[int2(DTid.x*2+1, DTid.y*2+1)] = o4;
        return;
    }
    float y1 = yTex.Load(int3(DTid.x*2,DTid.y*2, 0)).r;
    float y2 = yTex.Load(int3(DTid.x*2+1,DTid.y*2, 0)).r;
    float y3 = yTex.Load(int3(DTid.x*2,DTid.y*2+1, 0)).r;
    float y4 = yTex.Load(int3(DTid.x*2+1,DTid.y*2+1, 0)).r;
    // UV 不在此 -0.5: 居中偏移已折进 colorMat 第4列(与 Vulkan V1 一致)
    float2 uv = uvTex.Load(int3(DTid.x, DTid.y, 0)).rg;

    float4 rgba1 = yuv2Rgb(y1, uv.x, uv.y, 1.0f);
    float4 rgba2 = yuv2Rgb(y2, uv.x, uv.y, 1.0f);
    float4 rgba3 = yuv2Rgb(y3, uv.x, uv.y, 1.0f);
    float4 rgba4 = yuv2Rgb(y4, uv.x, uv.y, 1.0f);

    outTex[int2(DTid.x*2,DTid.y*2)] = rgba1;
    outTex[int2(DTid.x*2+1,DTid.y*2)] = rgba2;
    outTex[int2(DTid.x*2,DTid.y*2+1)] = rgba3;
    outTex[int2(DTid.x*2+1,DTid.y*2+1)] = rgba4;
}
)";

// DV 分支注入文本(p010Point 内; 基础变体注入空串)
static const char* kDvBranch = R"(    if (doviEnable == 1) {
        return float4(saturate(processColor(dvProcess(float3(y, uvRaw.x * k, uvRaw.y * k)))), a);
    }
)";

// canvas 变体附加段(字幕画布多后端渲染计划 §5.1): 混合码与输出域无关 ——
// HDR 直通域的画布已由 CPU 预编码为 PQ 码(DxCanvasLayer), SDR 域原样 gamma。
// 逐像素 gate-rect + 反算采样, 语义与 VK canvasBlend.comp/Metal canvasFragment
// 同源
static const char* kShaderCanvas = R"(
Texture2D<float4> canvasTex : register(t2);
SamplerState canvasSampler : register(s0);

cbuffer CanvasCbuf : register(b1)
{
    float4 canvasRect;   // center.xy, size.xy(帧归一化)
    float4 canvasXform;  // origin.xy, invScale, opacity
    float4 canvasMisc;   // canvasW, canvasH(补位)
};

// premultiplied source-over 叠加(写 UAV 前); opacity=0/出矩形/越采样域走原值
float4 applyCanvas(float4 base, uint2 pix) {
    if (canvasXform.w <= 0.0f) {
        return base;
    }
    float2 uv = (float2(pix) + 0.5f) / float2(width, height);
    float2 rmin = canvasRect.xy - canvasRect.zw * 0.5f;
    float2 rmax = canvasRect.xy + canvasRect.zw * 0.5f;
    if (any(uv < rmin) || any(uv >= rmax)) {
        return base;
    }
    float2 suv = canvasXform.xy + uv * canvasXform.z;
    if (any(suv < 0.0f) || any(suv > 1.0f)) {
        return base;
    }
    float4 o = canvasTex.SampleLevel(canvasSampler, suv, 0);
    float a = o.a * canvasXform.w;
    return float4(o.rgb * canvasXform.w + base.rgb * (1.0f - a), base.a);
}
)";

static std::string buildShaderSource(bool withDv, bool withCanvas) {
  std::string s = std::string(kShaderHead) + (withDv ? kShaderDv : "") +
                  (withCanvas ? kShaderCanvas : "") + kShaderBody;
  const std::string marker = "%%DV_BRANCH%%";
  auto p = s.find(marker);
  if (p != std::string::npos) {
    s.replace(p, marker.size(), withDv ? kDvBranch : "");
  }
  if (withCanvas) {
    // outTex 写入点接 applyCanvas(与 kShaderBody 内字面量严格同形, 替换唯一)
    struct Rep {
      const char* from;
      const char* to;
    };
    static const Rep reps[] = {
        {"= o1;", "= applyCanvas(o1, uint2(DTid.x*2, DTid.y*2));"},
        {"= o2;", "= applyCanvas(o2, uint2(DTid.x*2+1, DTid.y*2));"},
        {"= o3;", "= applyCanvas(o3, uint2(DTid.x*2, DTid.y*2+1));"},
        {"= o4;", "= applyCanvas(o4, uint2(DTid.x*2+1, DTid.y*2+1));"},
        {"= rgba1;", "= applyCanvas(rgba1, uint2(DTid.x*2,DTid.y*2));"},
        {"= rgba2;", "= applyCanvas(rgba2, uint2(DTid.x*2+1,DTid.y*2));"},
        {"= rgba3;", "= applyCanvas(rgba3, uint2(DTid.x*2,DTid.y*2+1));"},
        {"= rgba4;", "= applyCanvas(rgba4, uint2(DTid.x*2+1,DTid.y*2+1));"},
    };
    for (const auto& r : reps) {
      const auto pos = s.find(r.from);
      if (pos != std::string::npos) {
        s.replace(pos, strlen(r.from), r.to);
      }
    }
  }
  return s;
}

static std::string buildShaderSource(bool withDv) {
  return buildShaderSource(withDv, false);
}

static const std::string& shaderSource(bool withDv, bool withCanvas) {
  static const std::string baseSrc = buildShaderSource(false, false);
  static const std::string dvSrc = buildShaderSource(true, false);
  static const std::string canvasSrc = buildShaderSource(false, true);
  static const std::string canvasDvSrc = buildShaderSource(true, true);
  if (withCanvas) {
    return withDv ? canvasDvSrc : canvasSrc;
  }
  return withDv ? dvSrc : baseSrc;
}

static const std::string& shaderSource(bool withDv) {
  return shaderSource(withDv, false);
}

Dx11CSVideoRender::Dx11CSVideoRender() {
  renderType = RenderType::D3D11;
  canvasRender = std::make_unique<CanvasRender>();
}

// 字幕画布挂口(字幕画布多后端渲染计划 §5.1)
ICanvasLayer* Dx11CSVideoRender::enableRenderCanvas() {
  // lane=0 本腿输出是 VK 对接面: 禁挂(双重字幕+字幕被超分), 由 VK canvas 层负责
  if (bVkOutput) {
    LOGFLF(LogLevel::info, "canvas attach refused: vkOut lane");
    return nullptr;
  }
  LOGFLF(LogLevel::info, "canvas attach: dx11 leg");
  bCanvasWanted = true;
  return canvasRender.get();
}

void Dx11CSVideoRender::disableRenderCanvas() { bCanvasWanted = false; }

// 渲染线程: 挂/摘同步(宿主侧只置 wanted, 层实例归渲染线程)
void Dx11CSVideoRender::syncCanvasLayer() {
  if (bCanvasWanted && !canvasLayer) {
    LOGFLF(LogLevel::info, "dxcanvas layer create");
    canvasLayer = std::make_unique<DxCanvasLayer>();
    canvasRender->setCanvasLayer(canvasLayer.get());
  } else if (!bCanvasWanted && canvasLayer) {
    canvasRender->setCanvasLayer(nullptr);
    canvasLayer.reset();
  }
}

// canvas 变体惰性编译(与 DV 同策略)
bool Dx11CSVideoRender::ensureCanvasProgram(bool withDv) {
  MComPtr<ID3D11ComputeShader>& slot = withDv ? canvasDvShader : canvasShader;
  if (slot) {
    return true;
  }
  const bool& tried = withDv ? bCanvasDvProgramTried : bCanvasProgramTried;
  if (tried || !device || !d3dcontext) {
    return false;
  }
  if (withDv) {
    bCanvasDvProgramTried = true;
  } else {
    bCanvasProgramTried = true;
  }
  ID3DBlob* errorBlob = nullptr;
  ID3DBlob* shaderBlob = Dx11ShaderCache::get(shaderSource(withDv, true).c_str(),
                                              "main", "cs_5_0", &errorBlob);
  if (!shaderBlob) {
    if (errorBlob) {
      log(LogLevel::warn, "Dx11Graph canvas D3DCompile error: ",
          (char*)errorBlob->GetBufferPointer());
      errorBlob->Release();
    }
    return false;
  }
  if (FAILED(device->CreateComputeShader(shaderBlob->GetBufferPointer(),
                                         shaderBlob->GetBufferSize(), nullptr,
                                         &slot))) {
    slot.Reset();
    return false;
  }
  return true;
}

bool Dx11CSVideoRender::vaildAndInitGraph() {
  // 统一检查点(§4.2): 呈现面直通实态翻转 → 置 bResetFlag 重建输出端
  // (输出纹理格式须与交换链同翻, 否则中间帧值域错配发灰/过曝)
  if (checkTargetPassthrough()) {
    bResetFlag = true;
  }
  // CPU 帧腿(G9): 无解码纹理, 走自建上传纹理的独立建图分支
  if (cpuIn) {
    return initGraphCpu(yuvFrame);
  }
  if (!gpuFrame.buffer) {
    return false;
  }
  Dx11Context* context = static_cast<Dx11Context*>(gpuFrame.context);
  ID3D11Texture2D* yuvTexture = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  yuvTexture->GetDesc(&desc);
  // 原子读+清重置标志: 释放决策用捕获值, 只清本次读到的值 —— 读-清分离期间宿主
  // 新置的请求不会被盲写抹掉, 留到下一帧再重建一次(见 VideoRender.hpp 契约)
  const bool bNeedReset = bResetFlag.exchange(false);
  // 如果上下文或是大小变化，重新创建
  if (device != context->getDevice() || bNeedReset) {
    releaseGraph();
  }
  // NV12/P010 流切换: SRV 视图与着色器分支都不同, 必须重建
  if (yuvDesc.Format != desc.Format && desc.Format != DXGI_FORMAT_UNKNOWN) {
    releaseGraph();
  }
  // 输入从 CPU 腿切回硬解腿: CPU 自建纹理与设备须作废, 否则 inTexture 仍是
  // 上传纹理, CS 采样它拿到的是 CPU 旧帧
  if (cpuInFormat != DXGI_FORMAT_UNKNOWN) {
    releaseGraph();
  }
  // 早退/失败的重试由 computeShader 是否为空驱动, 不依赖本标志
  if (computeShader) {
    return true;
  }
  // 纹理可能无效，比如源纹理重建了，在渲染线程中指针还在
  if (desc.Width == 0 || desc.Height == 0) {
    return false;
  }
  // 使用解码的D3D11设备
  setDevice(context->getDevice());
  // 换分辨率要重传 constBuf: CS 按 inputSize 裁剪线程, 不重传会按旧尺寸只填左上角
  if (imageWidth != gpuFrame.format.width ||
      imageHeight != gpuFrame.format.height) {
    LOGFLF(LogLevel::info, "cs render size change, constBuf re-upload from:",
           imageWidth, "x", imageHeight, " to:", gpuFrame.format.width, "x",
           gpuFrame.format.height);
    bParamsDirty = true;
  }
  imageWidth = gpuFrame.format.width;
  imageHeight = gpuFrame.format.height;
  yuvDesc = desc;
  createProgram();
  return computeShader != nullptr;
}

void Dx11CSVideoRender::releaseGraph() {
  if (computeShader) {
    computeShader.Reset();
  }
  dvShader.Reset();
  boundShader = nullptr;
  bDvProgramTried = false;
  canvasShader.Reset();
  canvasDvShader.Reset();
  canvasSampler.Reset();
  bCanvasProgramTried = false;
  bCanvasDvProgramTried = false;
  // 画布纹理/参数常量随设备资源一起失效: 解绑层, contentStale 走宿主重传
  if (canvasLayer) {
    canvasRender->setCanvasLayer(nullptr);
    canvasLayer.reset();
  }
  // CPU 帧腿上游纹理随图释放(重建时按新尺寸/格式重造)
  inTexture.Reset();
  yView.Reset();
  uvView.Reset();
  cpuInFormat = DXGI_FORMAT_UNKNOWN;
  cpuDevice = nullptr;
  // 释放回读资源,映射指针一并失效
  if (bStagingMapped && d3dcontext) {
    d3dcontext->Unmap(stagingTexture.Get(), 0);
    bStagingMapped = false;
  }
  stagingTexture.Reset();
  stagingWidth = 0;
  stagingHeight = 0;
}

// CPU 帧腿(G9): CPU 无解码 D3D11 设备, 借用呈现窗口的设备建上传纹理。
// 输出共享纹理由该设备创建后交给同设备的 Dx11Window 开 NT 句柄(同适配器,
// 与硬解腿「解码设备→窗口设备」的跨设备句柄共享路径互不干扰)
bool Dx11CSVideoRender::initGraphCpu(const YUVFrame& frame) {
  const YuvType yuvType = frame.format.type;
  const bool b10 = yuvType == YuvType::yuv420P10;
  if ((yuvType != YuvType::yuv420P && !b10) || !frame.data[0]) {
    static std::once_flag once;
    std::call_once(once, [yuvType] {
      LOGFLF(LogLevel::warn, "dx11 cs cpu frame yuv type not support:",
             (int32_t)yuvType);
    });
    return false;
  }
  const int32_t width = frame.format.width;
  const int32_t height = frame.format.height;
  if (width <= 0 || height <= 0 || (height % 2) != 0 || (width % 2) != 0) {
    return false;
  }
  // 取呈现窗口的 D3D11 设备(与输出共享纹理/窗口 blit 同设备)
  ID3D11Device* wdDevice = nullptr;
  if (targetWindow) {
    IDx11Context* wdCtx =
        dynamic_cast<IDx11Context*>(targetWindow->getRenderContext());
    if (wdCtx) {
      wdDevice = wdCtx->getDevice();
    }
  }
  if (!wdDevice) {
    LOGFLF(LogLevel::warn, "dx11 cs cpu frame no window device, skip");
    return false;
  }
  const bool bNeedReset = bResetFlag.exchange(false);
  // 设备变了 / 宿主请求重建 / 输入格式换了 / 尺寸换了 → 全部重建图
  if (computeShader && (bNeedReset || cpuDevice != wdDevice ||
                        cpuInFormat != (b10 ? DXGI_FORMAT_P010
                                            : DXGI_FORMAT_NV12) ||
                        imageWidth != (uint32_t)width ||
                        imageHeight != (uint32_t)height)) {
    releaseGraph();
  }
  if (computeShader) {
    return true;
  }
  setDevice(wdDevice);
  cpuDevice = wdDevice;
  cpuInFormat = b10 ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12;
  // NV12/P010 上传纹理: 与解码纹理同为两平面视图(r8 / r16), CS 分支按格式选
  D3D11_TEXTURE2D_DESC tdesc = {};
  tdesc.Width = (UINT)width;
  tdesc.Height = (UINT)height;
  tdesc.MipLevels = 1;
  tdesc.ArraySize = 1;
  tdesc.Format = cpuInFormat;
  tdesc.SampleDesc.Count = 1;
  tdesc.Usage = D3D11_USAGE_DYNAMIC;
  tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  tdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device->CreateTexture2D(&tdesc, nullptr,
                                     inTexture.GetAddressOf()))) {
    LOGFLF(LogLevel::warn, "dx11 cs cpu upload texture create failed, dxgi:",
           (int32_t)cpuInFormat);
    cpuInFormat = DXGI_FORMAT_UNKNOWN;
    return false;
  }
  // P010 必须用 16bit 视图(类型不兼容 cast 会建 SRV 失败 → 静默全黑)
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Format = b10 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
  if (FAILED(device->CreateShaderResourceView(inTexture.Get(), &srvDesc,
                                              &yView))) {
    LOGFLF(LogLevel::warn, "dx11 cs cpu create y srv failed");
    return false;
  }
  srvDesc.Format = b10 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
  if (FAILED(device->CreateShaderResourceView(inTexture.Get(), &srvDesc,
                                              &uvView))) {
    LOGFLF(LogLevel::warn, "dx11 cs cpu create uv srv failed");
    return false;
  }
  // 走硬解同款 desc 语义: CS 按 yuvDesc.Format 选 P010 分支并做 tone map
  yuvDesc = {};
  yuvDesc.Width = (UINT)width;
  yuvDesc.Height = (UINT)height;
  yuvDesc.Format = cpuInFormat;
  if (imageWidth != (uint32_t)width || imageHeight != (uint32_t)height) {
    bParamsDirty = true;
  }
  imageWidth = (uint32_t)width;
  imageHeight = (uint32_t)height;
  createProgram();
  return computeShader != nullptr;
}

// 把 CPU 平面收进 inTexture: NV12 逐行直拷 Y + UV 交织; P010 逐样 <<6
// (yuv420P10 值在低位, P010 值在高位, 与 MetalRender.mm 的 <<6 同语义)
bool Dx11CSVideoRender::uploadCpuPlanes(const YUVFrame& frame) {
  if (!inTexture || !d3dcontext) {
    return false;
  }
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(d3dcontext->Map(inTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                             &mapped))) {
    LOGFLF(LogLevel::warn, "dx11 cs cpu upload map failed");
    return false;
  }
  const int32_t width = frame.format.width;
  const int32_t height = frame.format.height;
  const int32_t cw = width / 2;
  const int32_t ch = height / 2;
  const bool b10 = frame.format.type == YuvType::yuv420P10;
  uint8_t* dstY = (uint8_t*)mapped.pData;
  uint8_t* dstUV = dstY + (size_t)mapped.RowPitch * height;
  if (b10) {
    const size_t dstYW = mapped.RowPitch / 2;
    const size_t dstUVW = mapped.RowPitch / 2;
    for (int32_t r = 0; r < height; ++r) {
      const uint16_t* src =
          (const uint16_t*)(frame.data[0] + (size_t)r * frame.stride[0]);
      uint16_t* d = (uint16_t*)dstY + (size_t)r * dstYW;
      for (int32_t c = 0; c < width; ++c) {
        d[c] = (uint16_t)(src[c] << 6);
      }
    }
    for (int32_t r = 0; r < ch; ++r) {
      const uint16_t* u =
          (const uint16_t*)(frame.data[1] + (size_t)r * frame.stride[1]);
      const uint16_t* v =
          (const uint16_t*)(frame.data[2] + (size_t)r * frame.stride[2]);
      uint16_t* d = (uint16_t*)dstUV + (size_t)r * dstUVW;
      for (int32_t c = 0; c < cw; ++c) {
        d[2 * c] = (uint16_t)(u[c] << 6);
        d[2 * c + 1] = (uint16_t)(v[c] << 6);
      }
    }
  } else {
    for (int32_t r = 0; r < height; ++r) {
      memcpy(dstY + (size_t)r * mapped.RowPitch,
             frame.data[0] + (size_t)r * frame.stride[0], (size_t)width);
    }
    for (int32_t r = 0; r < ch; ++r) {
      const uint8_t* u = frame.data[1] + (size_t)r * frame.stride[1];
      const uint8_t* v = frame.data[2] + (size_t)r * frame.stride[2];
      uint8_t* d = dstUV + (size_t)r * mapped.RowPitch;
      for (int32_t c = 0; c < cw; ++c) {
        d[2 * c] = u[c];
        d[2 * c + 1] = v[c];
      }
    }
  }
  d3dcontext->Unmap(inTexture.Get(), 0);
  return true;
}

void Dx11CSVideoRender::renderCpuFrame(const YUVFrame& frame) {
  if (!uploadCpuPlanes(frame)) {
    return;
  }
  // 常量参数(尺寸/格式/transfer/hdrMode/峰值): 与硬解腿同一套上传口径
  if (bParamsDirty) {
    bParamsDirty = false;
    constData.width = (int32_t)imageWidth;
    constData.height = (int32_t)imageHeight;
    constData.yuvType = (yuvDesc.Format == DXGI_FORMAT_P010) ? 1 : 0;
    constData.transfer = (int32_t)cs.transfer;
    constData.maxLuminance = (float)hdrPeakNits(hdrMeta);
    constData.sdrWhiteNits = 100.0f;
    constData.hdrMode = (int32_t)hdrMode;
    Mat4x4f mat = buildYuvToRgb(cs);
    memcpy(&constData.colorMat, &mat, 16 * sizeof(float));
    constBuf->updateResource(d3dcontext.Get());
  }
  // 绑定输入 SRV / 输出 UAV / 着色器, 与 createProgram 尾段口径一致
  // (CPU 帧腿同样可 canvas 合成; allowDv=false 保持不吃 DV 链的既有行为)
  ID3D11ComputeShader* want = selectShader(false);
  d3dcontext->CSSetShader(want ? want : computeShader.Get(), nullptr, 0);
  boundShader = want ? want : computeShader.Get();
  ID3D11ShaderResourceView* srvArray[2] = {yView.Get(), uvView.Get()};
  d3dcontext->CSSetShaderResources(0, 2, srvArray);
  ID3D11UnorderedAccessView* uavArray[1] = {outTexture->uavView.Get()};
  d3dcontext->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
  uint32_t groupX = divUp(imageWidth / 2, 16);
  uint32_t groupY = divUp(imageHeight / 2, 16);
  d3dcontext->Dispatch(groupX, groupY, 1);
  outSharedTex->signalFence();
  texture = outTexture->texture.Get();
}

void Dx11CSVideoRender::renderGpuFrame(const GpuFrame& frame) {
  renderToTexture(frame);
}

void Dx11CSVideoRender::setColorSpace(const ColorSpaceDesc& c) {
  VideoRender::setColorSpace(c);  // 基类存档, checkShot CPU兜底转换取用
  if (c.standard == cs.standard && c.range == cs.range &&
      c.transfer == cs.transfer) {
    return;
  }
  cs = c;
  bParamsDirty = true;
}

void Dx11CSVideoRender::setHdrMeta(const HdrMeta& meta) {
  if (!meta.valid) {
    return;
  }
  hdrMeta = meta;
  bParamsDirty = true;
}

void Dx11CSVideoRender::setDoviMeta(const DoviMeta& meta) {
  // 探针走 stderr: playtest 环境 logTask 启动后不再排水, 引擎 info 日志不可见
  fprintf(stderr, "[dx11cs] setDoviMeta valid=%d pivots=%d/%d/%d\n",
          (int)meta.valid, (int)meta.comp[0].numPivots,
          (int)meta.comp[1].numPivots, (int)meta.comp[2].numPivots);
  doviMeta = meta;
  packDoviUbo(constData, doviMeta);
  bParamsDirty = true;
}

void Dx11CSVideoRender::setHdrMode(HdrMode mode) {
  if (mode == hdrMode) {
    return;
  }
  hdrMode = mode;
  bParamsDirty = true;
}

void Dx11CSVideoRender::createProgram() {
  if (!device || !d3dcontext) {
    return;
  }
  // 编译走进程内 DXBC 缓存: 源码是常量, 重建图直接命中, 省掉每次 ~100ms 的
  // D3DCompile; blob 所有权在缓存, 这里(以及任何调用点)都不要 Release
  ID3DBlob* errorBlob = nullptr;
  ID3DBlob* shaderBlob =
      Dx11ShaderCache::get(shaderSource(false).c_str(), "main", "cs_5_0",
                           &errorBlob);
  if (!shaderBlob) {
    if (errorBlob) {
      log(LogLevel::warn,
          "Dx11Graph D3DCompile error: ", (char*)errorBlob->GetBufferPointer());
      errorBlob->Release();
    }
    return;
  }
  // 创建计算着色器(设备相关, 每 device 一份; 首建含驱动侧 JIT, 计时以分辨慢源)
  auto tCreate = std::chrono::steady_clock::now();
  HRESULT hr = device->CreateComputeShader(shaderBlob->GetBufferPointer(),
                                           shaderBlob->GetBufferSize(), nullptr,
                                           &computeShader);
  auto msCreate = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - tCreate).count();
  fprintf(stderr, "Dx11Graph CreateComputeShader %lldms\n", (long long)msCreate);
  if (FAILED(hr)) {
    return;
  }
  // 创建常量缓冲区(ColorYuvUBO 全布局, 与 HLSL cbuffer 对齐)
  constBuf = std::make_unique<Dx11Constant>();
  constBuf->setBufferSize(sizeof(constData));
  constBuf->cpuData = (uint8_t*)&constData;
  constBuf->initResource(device);
  // 创建输出计算着色器资源
  // outTexture = std::make_unique<Dx11Texture>();
  // // outTexture->setOnlyUAV(true);
  // outTexture->setTextureSize(imageWidth, imageHeight,
  //                            DXGI_FORMAT_R8G8B8A8_UNORM);
  // outTexture->initResource(device);
  outSharedTex = std::make_unique<Dx11SharedTex>();
  outTexture = outSharedTex->getDx11Texture();
  // 输出格式按两轴定(§6.1 R2 修订):
  // ① 输出去向: 给 VK(链F/lane=0)恒 rgba8 —— VkInputLayer 对接面全平台统一,
  //    不给别平台加 rgba10 特例;
  // ② 直呈原生窗口(链A/lane=1)跟呈现面直通实态: 直通=R10G10B10A2(PQ 码原样写),
  //    否则=R8G8B8A8。两端必须同翻, 错配即发灰(SDR 码当 PQ 码上屏)
  DXGI_FORMAT outFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (!bVkOutput && bTargetPassthrough) {
    outFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
  }
  outTexture->setTextureSize(imageWidth, imageHeight, outFormat);
  LOGFLF(LogLevel::info, "cs output format, vkOut:", bVkOutput ? 1 : 0,
         " passthrough:", bTargetPassthrough ? 1 : 0, " dxgi:",
         (int32_t)outFormat);
  outSharedTex->initTexture(device);
  // 创建输入复制纹理
  // CPU 帧腿(G9): inTexture 与 y/uv SRV 已由 initGraphCpu 按 DYNAMIC 上传
  // 需求建好(硬解腿要的是 USAGE_DEFAULT 解码拷贝纹理), 此处跳过, 否则会把
  // 上传纹理覆盖成不可 Map 的 DEFAULT 纹理
  if (cpuInFormat == DXGI_FORMAT_UNKNOWN) {
    D3D11_TEXTURE2D_DESC inCopyDesc = yuvDesc;
    inCopyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    inCopyDesc.MipLevels = 1;
    inCopyDesc.ArraySize = 1;
    inCopyDesc.SampleDesc.Count = 1;
    inCopyDesc.Usage = D3D11_USAGE_DEFAULT;
    device->CreateTexture2D(&inCopyDesc, nullptr, &inTexture);
    // 创建输入纹理的SRV: NV12 用 8bit 视图; P010 必须用 16bit 视图
    // (类型不兼容的 cast 会创建失败且无 SRV -> 渲染静默全黑)
    bool bP010 = inCopyDesc.Format == DXGI_FORMAT_P010;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = inCopyDesc.MipLevels;
    srvDesc.Format = bP010 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    if (FAILED(device->CreateShaderResourceView(inTexture.Get(), &srvDesc,
                                                &yView))) {
      LOGFLF(LogLevel::warn, "create y srv failed, dxgi:",
             (int32_t)inCopyDesc.Format);
    }
    srvDesc.Format = bP010 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    if (FAILED(device->CreateShaderResourceView(inTexture.Get(), &srvDesc,
                                                &uvView))) {
      LOGFLF(LogLevel::warn, "create uv srv failed, dxgi:",
             (int32_t)inCopyDesc.Format);
    }
  }
  // 设置当前纹理
  texture = outTexture->texture.Get();
  // 设置常量缓冲区
  d3dcontext->CSSetConstantBuffers(0, 1, constBuf->buffer.GetAddressOf());
  // 设置计算着色器资源
  d3dcontext->CSSetShader(computeShader.Get(), nullptr, 0);
  boundShader = computeShader.Get();
  // 测试钩子: 置 AVOX_DX11_DV_SHADER=1 则建图时预编译 DV 变体(验编译/计时), 缺省惰性
  if (getenv("AVOX_DX11_DV_SHADER") != nullptr) {
    ensureDvProgram();
  }
  // 设置输入纹理
  ID3D11ShaderResourceView* srvArray[2] = {yView.Get(), uvView.Get()};
  d3dcontext->CSSetShaderResources(0, 2, srvArray);
  // 设置输出纹理的 UAV
  ID3D11UnorderedAccessView* uavArray[1] = {outTexture->uavView.Get()};
  d3dcontext->CSSetUnorderedAccessViews(0, 1, uavArray, nullptr);
  // constBuf 刚重建, 至少要上传一次, 否则 CS 的 inputSize 是旧值
  bParamsDirty = true;
}

bool Dx11CSVideoRender::ensureDvProgram() {
  if (dvShader) {
    return true;
  }
  if (bDvProgramTried || !device || !d3dcontext) {
    return false;
  }
  bDvProgramTried = true;
  ID3DBlob* errorBlob = nullptr;
  ID3DBlob* shaderBlob = Dx11ShaderCache::get(shaderSource(true).c_str(), "main",
                                              "cs_5_0", &errorBlob);
  if (!shaderBlob) {
    if (errorBlob) {
      log(LogLevel::warn, "Dx11Graph DV D3DCompile error: ",
          (char*)errorBlob->GetBufferPointer());
      errorBlob->Release();
    }
    return false;
  }
  auto tCreate = std::chrono::steady_clock::now();
  HRESULT hr = device->CreateComputeShader(shaderBlob->GetBufferPointer(),
                                           shaderBlob->GetBufferSize(), nullptr,
                                           &dvShader);
  auto msCreate = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - tCreate).count();
  fprintf(stderr, "Dx11Graph DV CreateComputeShader %lldms\n",
          (long long)msCreate);
  if (FAILED(hr)) {
    dvShader.Reset();
    return false;
  }
  return true;
}

ID3D11ComputeShader* Dx11CSVideoRender::selectShader() {
  return selectShader(true);
}

// 变体选择 + canvas 资源绑定(render 线程): canvas 活跃帧走 canvas 变体
// (prepareAndBind 同步建纹理/上传/刷参数), 空窗/编译失败回退非 canvas 变体
// (零字幕零影响); allowDv=false 供 CPU 帧腿(既有行为不吃 DV 链)
ID3D11ComputeShader* Dx11CSVideoRender::selectShader(bool allowDv) {
  syncCanvasLayer();
  const bool bDv = allowDv && doviMeta.valid;
  ID3D11ComputeShader* base = nullptr;
  if (!bDv) {
    base = computeShader.Get();
  } else {
    base = ensureDvProgram() ? dvShader.Get() : computeShader.Get();
  }
  // prepareAndBind 无条件调: 首调才完成画布 reset+建图前来件回灌(visible 依赖
  // 它, 门序反了互为前提内容永远进不来); 内部 !hasContent 即 false, 空窗/
  // 无字幕仍走 base 变体(零字幕零影响不受影响)
  const bool bCanvasFrame =
      canvasLayer && canvasLayer->prepareAndBind(device, d3dcontext.Get(),
                                                  imageWidth, imageHeight,
                                                  !bVkOutput &&
                                                      bTargetPassthrough);
  static bool bCanvasDiagLogged = false;
  if (!bCanvasFrame) {
    if (!bCanvasDiagLogged && canvasLayer && canvasLayer->visible()) {
      // visible 但 prepare 失败: 资源/编码层问题, 留一次性痕迹
      bCanvasDiagLogged = true;
      LOGFLF(LogLevel::warn, "canvas prepare failed, fallback (visible=1)");
    }
    return base;
  }
  if (!ensureCanvasProgram(bDv)) {
    if (!bCanvasDiagLogged) {
      bCanvasDiagLogged = true;
      LOGFLF(LogLevel::warn, "canvas program unavailable, fallback");
    }
    return base;
  }
  if (!bCanvasDiagLogged) {
    bCanvasDiagLogged = true;
    LOGFLF(LogLevel::info, "canvas variant on, dv:", bDv ? 1 : 0,
           " pq:", (!bVkOutput && bTargetPassthrough) ? 1 : 0);
  }
  ID3D11ComputeShader* want = bDv ? canvasDvShader.Get() : canvasShader.Get();
  if (want) {
    // canvas 线性采样器(SampleLevel 用 s0): 不绑则 release 下采样恒 0,
    // source-over 退化为直通(字幕不可见的静默失败)
    if (!canvasSampler) {
      D3D11_SAMPLER_DESC sd = {};
      sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
      sd.MaxLOD = D3D11_FLOAT32_MAX;
      device->CreateSamplerState(&sd, &canvasSampler);
    }
    if (canvasSampler) {
      ID3D11SamplerState* s = canvasSampler.Get();
      d3dcontext->CSSetSamplers(0, 1, &s);
    }
    ID3D11ShaderResourceView* cSrv = canvasLayer->srv();
    d3dcontext->CSSetShaderResources(2, 1, &cSrv);
    ID3D11Buffer* cBuf = canvasLayer->paramBuffer();
    d3dcontext->CSSetConstantBuffers(1, 1, &cBuf);
  }
  return want;
}

void Dx11CSVideoRender::renderToTexture(const GpuFrame& gpuFrame) {
  Dx11Context* context = static_cast<Dx11Context*>(gpuFrame.context);
  ID3D11Texture2D* yuvTexture = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  yuvTexture->GetDesc(&desc);
  if (!computeShader || !outTexture || !constBuf || !inTexture || !yView ||
      !uvView) {
    return;
  }
  // 常量参数(尺寸/格式/transfer/hdrMode/峰值)变化才上传
  if (bParamsDirty) {
    bParamsDirty = false;
    constData.width = (int32_t)imageWidth;
    constData.height = (int32_t)imageHeight;
    constData.yuvType = (yuvDesc.Format == DXGI_FORMAT_P010) ? 1 : 0;
    constData.transfer = (int32_t)cs.transfer;
    constData.maxLuminance = (float)hdrPeakNits(hdrMeta);
    constData.sdrWhiteNits = 100.0f;
    constData.hdrMode = (int32_t)hdrMode;
    // 颜色矩阵: buildYuvToRgb 已含标准系数 + limited 量程展开, 行优先 16 浮点,
    // 与 float4x4 列主序一致, mul(vec4, colorMat) 结果等同于 Vulkan V1/V5
    Mat4x4f mat = buildYuvToRgb(cs);
    memcpy(&constData.colorMat, &mat, 16 * sizeof(float));
    constBuf->updateResource(d3dcontext.Get());
  }
  if (desc.ArraySize > 1) {
    // 复制指定索引的切片到临时纹理
    d3dcontext->CopySubresourceRegion(
        inTexture.Get(), D3D11CalcSubresource(0, 0, desc.MipLevels), 0, 0, 0,
        yuvTexture,
        D3D11CalcSubresource(0, gpuFrame.queueIndex, desc.MipLevels), nullptr);
    // logTexture(device, inTexture.Get());
  } else {
    // 将 yuvTexture 的数据复制给 inTexture
    d3dcontext->CopyResource(inTexture.Get(), yuvTexture);
  }
  // DV 内容换程: 变体着色器变了才重绑(DV 变体在此首帧惰性编译)
  ID3D11ComputeShader* want = selectShader();
  if (want && want != boundShader) {
    d3dcontext->CSSetShader(want, nullptr, 0);
    boundShader = want;
  }
  // 执行计算着色器
  uint32_t groupX = divUp(imageWidth / 2, 16);
  uint32_t groupY = divUp(imageHeight / 2, 16);
  d3dcontext->Dispatch(groupX, groupY, 1);
  // 写方同步点: 告知读方(Vulkan 图直读本共享纹理)本帧已写入。缺这一步时
  // Vulkan 读队列与 DX11 写队列互不排序, 读方会读到未更新的旧帧 —— 图重算出
  // 同一张画面, 窗口连续 present 相同内容, 屏上每秒冻 3~4 帧(实测 115~133ms)
  outSharedTex->signalFence();
  // 解绑所有可能的冲突,outTexture在这做UAV，不解绑，后面不能做SRV
  // ID3D11ShaderResourceView* nullSRVs[2] = {nullptr, nullptr};
  // ID3D11UnorderedAccessView* nullUAVs[1] = {nullptr};
  // d3dcontext->CSSetShaderResources(0, 2, nullSRVs);
  // d3dcontext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
  // 准备输出到Vk上下文
  texture = outTexture->texture.Get();
}

bool Dx11CSVideoRender::fetchFrame(ImageBuffer* imageBuffer) {
  if (!outTexture) {
    return false;
  }
  // 直通态输出是 PQ 码 rgba10, 回读出来是脏图(非 SDR 口径), 拒绝并提示
  // 走 SDR 口径截图(临时 follow 抽帧)
  if (bTargetPassthrough) {
    LOGFLF(LogLevel::warn, "fetchFrame refused in hdr passthrough (pq code)");
    return false;
  }
  Dx11Context context = {};
  context.setDevice(device);
  context.setTexture(outTexture->texture.Get());
  return fetchTexture(&context, imageBuffer);
}

bool Dx11CSVideoRender::getCpuFrameBuffer(IImageBuffer** buffer,
                                          YuvType& yuvType, int64_t* pts) {
  // CPU输入(软解)不经过GPU,交基类packed视图
  if (cpuIn) {
    return VideoRender::getCpuFrameBuffer(buffer, yuvType, pts);
  }
  if (!bOutCpuYuv || !device || !d3dcontext || !gpuFrame.buffer) {
    return false;
  }
  // 本帧未回读过才做staging拷贝+Map,一帧最多一次
  if (publishedTick != renderTick && !mapStagingFrame()) {
    return false;
  }
  publishedTick = renderTick;
  *buffer = &stagingBuffer;
  // 交付解码直出格式: 硬解10bit是p010(高位对齐), 8bit是nv12
  yuvType = (yuvDesc.Format == DXGI_FORMAT_P010) ? YuvType::p010
                                                 : YuvType::nv12;
  if (pts) {
    *pts = gpuFrame.pts;
  }
  return true;
}

bool Dx11CSVideoRender::mapStagingFrame() {
  ID3D11Texture2D* src = (ID3D11Texture2D*)gpuFrame.buffer;
  D3D11_TEXTURE2D_DESC desc = {};
  src->GetDesc(&desc);
  bool bP010 = desc.Format == DXGI_FORMAT_P010;
  if (desc.Format != DXGI_FORMAT_NV12 && !bP010) {
    LOGFLF(LogLevel::warn, "cpu yuv out not support dxgi format:",
           (int32_t)desc.Format);
    return false;
  }
  // 解码 surface 是按对齐扩过的(如 1280x720 -> 1280x768),padding 行解码器
  // 从不写入(Y/U/V=0,转RGB呈绿带)。CPU 交付必须按显示尺寸裁剪。
  const int32_t outW = gpuFrame.format.width;
  const int32_t outH = gpuFrame.format.height;
  if (outW <= 0 || outH <= 0 || outW > (int32_t)desc.Width ||
      outH > (int32_t)desc.Height) {
    LOGFLF(LogLevel::warn, "cpu yuv out bad display size:", outW, "x", outH,
           " surface:", (int32_t)desc.Width, "x", (int32_t)desc.Height);
    return false;
  }
  // 解上一帧映射(发布指针随Unmap失效,消费者须在当帧窗口内使用)
  if (bStagingMapped) {
    d3dcontext->Unmap(stagingTexture.Get(), 0);
    bStagingMapped = false;
  }
  if (!stagingTexture || stagingWidth != outW || stagingHeight != outH) {
    stagingTexture.Reset();
    D3D11_TEXTURE2D_DESC sdesc = desc;
    sdesc.Width = (UINT)outW;
    sdesc.Height = (UINT)outH;
    sdesc.MipLevels = 1;
    sdesc.ArraySize = 1;
    sdesc.Usage = D3D11_USAGE_STAGING;
    sdesc.BindFlags = 0;
    sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sdesc.MiscFlags = 0;
    if (FAILED(device->CreateTexture2D(&sdesc, nullptr,
                                       stagingTexture.GetAddressOf()))) {
      LOGFLF(LogLevel::warn, "create nv12 staging texture failed");
      return false;
    }
    stagingWidth = outW;
    stagingHeight = outH;
  }
  const UINT srcSub =
      D3D11CalcSubresource(0, (UINT)gpuFrame.queueIndex, desc.MipLevels);
  // 平面格式源侧用box z选平面, 目的侧用DstZ: Y面DstZ=0, UV面DstZ=1
  D3D11_BOX yBox = {0, 0, 0, (UINT)outW, (UINT)outH, 1};
  D3D11_BOX uvBox = {0, 0, 1, (UINT)outW / 2, (UINT)outH / 2, 2};
  d3dcontext->CopySubresourceRegion(stagingTexture.Get(), 0, 0, 0, 0, src,
                                    srcSub, &yBox);
  d3dcontext->CopySubresourceRegion(stagingTexture.Get(), 0, 0, 0, 1, src,
                                    srcSub, &uvBox);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(d3dcontext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                             &mapped))) {
    LOGFLF(LogLevel::warn, "map nv12 staging texture failed");
    return false;
  }
  bStagingMapped = true;
  // packed布局约定: r8/r16 + height*3/2 + rowPitch(字节), UV起始=rowPitch*height
  // NV12: 8bit Y面+8bit交错UV; P010: 16bit(高10位)Y面+16bit交错UV
  ImageFormat fmt = {};
  fmt.width = outW;
  fmt.height = outH * 3 / 2;
  fmt.imageType = bP010 ? ImageType::r16 : ImageType::r8;
  fmt.rowPitch = (int32_t)mapped.RowPitch;
  stagingBuffer.setData((uint8_t*)mapped.pData, fmt, false);
  return true;
}

}
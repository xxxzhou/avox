#include "EglVideoRender.hpp"

#include "GLES3/gl3.h"
#include "GLESContext.hpp"
#include "GlesCanvasLayer.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/HighClock.hpp"
#include "avox/player/VideoTrack.hpp"
#include "avox_egl/EglWindow.hpp"

#ifdef __ANDROID__
#include "avox_android/AndVDecoder.hpp"
#endif

namespace avox {
// 渲染到窗口,需要y倒置
static const char* VertexShaderString = R"(
precision mediump float;
attribute vec4 position;
attribute vec2 uv;
varying mediump vec2 textureCoordinate;
void main()
{
    gl_Position = position;
    // android里y倒置
    textureCoordinate = vec2(uv.x, uv.y);   
}
)";
// 渲染到EGL用于与vulkan交互，y不需要倒置
static const char* VertexShaderString1 = R"(
precision mediump float;
attribute vec4 position;
attribute vec2 uv;
varying mediump vec2 textureCoordinate;
void main()
{
    gl_Position = position;    
    textureCoordinate = vec2(uv.x, 1.0 - uv.y);   
}
)";

// OES采样输出已是驱动隐式转换后的RGB(HDR内容为BT.2020+PQ编码, [0,1]),
// HDR链与 Dx11CSVideoRender/MetalRender 同源: PQ EOTF->ACES->BT.2020->BT.709
static const char* FragmentShaderString = R"(
#extension GL_OES_EGL_image_external : require
precision highp float;
varying mediump vec2 textureCoordinate;
uniform samplerExternalOES oes_texture;
uniform int uHdrMode;
uniform int uTransfer;
uniform float uPeakNits;
uniform float uSdrWhite;

vec3 pqToLinear(vec3 n) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    vec3 p = pow(clamp(n, 0.0, 1.0), vec3(1.0 / m2));
    vec3 num = max(p - c1, 0.0);
    return pow(num / (c2 - c3 * p), vec3(1.0 / m1));
}

vec3 hlgToLinear(vec3 e) {
    vec3 t = clamp(e, 0.0, 1.0);
    vec3 lo = t * t / 3.0;
    vec3 hi = (exp((t - 0.55991073) / 0.17883277) + 0.28466892) / 12.0;
    vec3 scene = mix(lo, hi, step(vec3(0.5), t));
    float ys = dot(scene, vec3(0.2627, 0.6780, 0.0593));
    return scene * pow(vec3(max(ys, 1e-6)), vec3(0.2));
}

vec3 toneMap(vec3 lin) {
    float xScale = 10000.0 / uSdrWhite;
    vec3 x = max(lin * xScale, 0.0);
    vec3 a = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    float peakX = max(uPeakNits, uSdrWhite) / uSdrWhite;
    float peak = (peakX * (2.51 * peakX + 0.03)) / (peakX * (2.43 * peakX + 0.59) + 0.14);
    return clamp(a / peak, 0.0, 1.0);
}

vec3 bt2020ToBt709(vec3 c) {
    return max(vec3(dot(c, vec3(1.6605, -0.5876, -0.0728)),
                    dot(c, vec3(-0.1246, 1.1329, -0.1006)),
                    dot(c, vec3(-0.0182, -0.1006, 1.1187))), 0.0);
}

vec3 linearToBt709(vec3 c) {
    vec3 lo = c * 4.5;
    vec3 hi = 1.099 * pow(max(c, 0.0), vec3(0.45)) - 0.099;
    return mix(lo, hi, step(vec3(0.018), c));
}

vec3 processColor(vec3 rgb) {
    if (uHdrMode == 2) {
        return rgb;
    }
    if (uTransfer == 2) {
        vec3 lin = pqToLinear(rgb);
        lin = toneMap(lin);
        lin = bt2020ToBt709(lin);
        return linearToBt709(lin);
    }
    if (uTransfer == 3) {
        vec3 lin = hlgToLinear(rgb) * 0.1;
        lin = toneMap(lin);
        lin = bt2020ToBt709(lin);
        return linearToBt709(lin);
    }
    return rgb;
}

void main() {
    vec3 rgb = texture2D(oes_texture, textureCoordinate).rgb;
    gl_FragColor = vec4(clamp(processColor(rgb), 0.0, 1.0), 1.0);
}
)";

static const GLfloat verts[] = {
    -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
};
static const GLfloat uvs[] = {
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
};

// ---------- DV 变体程序(1005): Y2Y 采样 + shader 内 DV 整形链 ----------
// 常规 OES 采样拿到的已是驱动 YUV→RGB 的结果(YCbCr 域丢失, reshape 无从谈起);
// GL_EXT_YUV_target 的 `__samplerExternal2DY2YEXT` 采样**不做色彩转换**, shader
// 直接拿到原始 YUV(Y,Cb,Cr) → 与 VK yuv2rgbaV5.comp / DX11 CS 同一套 DV 数学
// (reshape→ycc_to_rgb→PQ 线性→LMS→回编码 PQ BT.2020) 后接 tone map。
// 需 ESSL3('#version 300 es' + in/out + UBO);无该扩展的设备编译失败 → 回落常规程序
static const char* DvVertexShaderString = R"(#version 300 es
in vec2 position;
in vec2 uv;
out highp vec2 textureCoordinate;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    // 与 VertexShaderString 同口径(有窗口)
    textureCoordinate = uv;
}
)";
static const char* DvVertexShaderString1 = R"(#version 300 es
in vec2 position;
in vec2 uv;
out highp vec2 textureCoordinate;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    // 与 VertexShaderString1 同口径(渲染到 FBO/对接面, y 倒置)
    textureCoordinate = vec2(uv.x, 1.0 - uv.y);
}
)";

// std140 布局与 ColorYuvUBO(2848B, ColorSpace.hpp 有 static_assert)逐字段对齐;
// DV 区字段名/展平法一律照抄 yuv2rgbaV5.comp(同一 packDoviUbo 打包)
static const char* DvFragmentShaderString = R"(#version 300 es
#extension GL_EXT_YUV_target : require
precision highp float;
in highp vec2 textureCoordinate;
uniform __samplerExternal2DY2YEXT oesYuv;
layout(std140) uniform DvUbo
{
	int width;
	int height;
	int yuvType;
	int transfer;
	mat4 colorMat;
	float maxLuminance;
	float sdrWhiteNits;
	int hdrMode;
	int _pad;
	int doviEnable;
	int _dv0;
	int _dv1;
	int _dv2;
	vec4 dvPivots[7];
	vec4 dvPoly[18];
	vec4 dvMmr[132];
	ivec4 dvIdc[6];
	vec4 dvNumPivots;
	vec4 dvNl[3];
	vec4 dvNlOff;
	vec4 dvLm[3];
} ubo;
out vec4 fragColor;

vec3 pqToLinear(vec3 n) {
	const float m1 = 0.1593017578125;
	const float m2 = 78.84375;
	const float c1 = 0.8359375;
	const float c2 = 18.8515625;
	const float c3 = 18.6875;
	vec3 p = pow(clamp(n, vec3(0.0), vec3(1.0)), vec3(1.0 / m2));
	vec3 num = max(p - c1, vec3(0.0));
	return pow(num / (c2 - c3 * p), vec3(1.0 / m1));
}

vec3 linearToPq(vec3 lin) {
	const float m1 = 0.1593017578125;
	const float m2 = 78.84375;
	const float c1 = 0.8359375;
	const float c2 = 18.8515625;
	const float c3 = 18.6875;
	vec3 p = pow(max(lin, vec3(0.0)), vec3(m1));
	vec3 e = (vec3(c1) + vec3(c2) * p) / (vec3(1.0) + vec3(c3) * p);
	return pow(e, vec3(m2));
}

vec3 hlgToLinear(vec3 e) {
	vec3 t = clamp(e, vec3(0.0), vec3(1.0));
	vec3 lo = t * t / 3.0;
	vec3 hi = (exp((t - 0.55991073) / 0.17883277) + 0.28466892) / 12.0;
	vec3 scene = mix(lo, hi, step(vec3(0.5), t));
	float ys = dot(scene, vec3(0.2627, 0.6780, 0.0593));
	return scene * pow(max(ys, 1e-6), 0.2);
}

// BT.2390 观感取向(与 V5 同源): 锚(sdrWhite)下近似线性, 超锚按内容峰值软压
vec3 toneMap(vec3 lin) {
	float dstN = max(ubo.sdrWhiteNits, 1.0);
	float white = max(ubo.maxLuminance, dstN) / dstN;
	vec3 d = max(lin * 10000.0 / dstN, vec3(0.0));
	vec3 t = d * (1.0 + d / (white * white)) / (1.0 + d);
	return clamp(t, vec3(0.0), vec3(1.0));
}

vec3 bt2020ToBt709(vec3 c) {
	return max(mat3(
		1.6605, -0.1246, -0.0182,
		-0.5876, 1.1329, -0.1006,
		-0.0728, -0.0083, 1.1187) * c, vec3(0.0));
}

vec3 linearToBt709(vec3 c) {
	vec3 lo = c * 4.5;
	vec3 hi = 1.099 * pow(max(c, vec3(0.0)), vec3(0.45)) - 0.099;
	return mix(lo, hi, step(vec3(0.018), c));
}

float dvPivot(int idx) { return ubo.dvPivots[idx >> 2][idx & 3]; }
float dvPoly(int idx)  { return ubo.dvPoly[idx >> 2][idx & 3]; }
float dvMmr(int idx)   { return ubo.dvMmr[idx >> 2][idx & 3]; }
int   dvIdc(int idx)   { return ubo.dvIdc[idx >> 2][idx & 3]; }

// 单组件曲线: pivot 段选 → poly Horner / mmr 交叉项展开(与 V5 逐行同源)
float dvReshapeComp(int c, vec3 sig, float s) {
	int base = c * 8;
	int sel = -1;
	for (int i = 0; i < 8; i++) {
		if (dvIdc(base + i) == 0) break;
		if (i == 7 || s < dvPivot(c * 9 + i + 1)) { sel = i; break; }
	}
	if (sel < 0) {
		return s;
	}
	int idc = dvIdc(base + sel);
	if (idc == 1) {
		int pf = (base + sel) * 3;
		return (dvPoly(pf + 2) * s + dvPoly(pf + 1)) * s + dvPoly(pf);
	}
	int order = idc - 16;
	int p = (base + sel) * 22;
	int np = int(ubo.dvNumPivots[c]);
	float acc = dvMmr(p);
	vec4 sigX = vec4(sig.x * sig.y, sig.x * sig.z, sig.y * sig.z,
	                 sig.x * sig.y * sig.z);
	acc += dot(vec3(dvMmr(p + 1), dvMmr(p + 2), dvMmr(p + 3)), sig);
	acc += dot(vec4(dvMmr(p + 4), dvMmr(p + 5), dvMmr(p + 6), dvMmr(p + 7)), sigX);
	if (order >= 2) {
		vec3 sig2 = sig * sig;
		vec4 sigX2 = sigX * sigX;
		acc += dot(vec3(dvMmr(p + 8), dvMmr(p + 9), dvMmr(p + 10)), sig2);
		acc += dot(vec4(dvMmr(p + 11), dvMmr(p + 12), dvMmr(p + 13), dvMmr(p + 14)), sigX2);
		if (order >= 3) {
			acc += dot(vec3(dvMmr(p + 15), dvMmr(p + 16), dvMmr(p + 17)), sig2 * sig);
			acc += dot(vec4(dvMmr(p + 18), dvMmr(p + 19), dvMmr(p + 20), dvMmr(p + 21)), sigX2 * sigX);
		}
	}
	return clamp(acc, dvPivot(c * 9), dvPivot(c * 9 + np - 1));
}

// DV 全链: reshape → ycc_to_rgb(PQ 域) → PQ 线性 → LMS 合成阵 → 回编码 PQ BT.2020
vec3 dvProcess(vec3 yuv) {
	vec3 sig = clamp(yuv, vec3(0.0), vec3(1.0));
	sig = vec3(dvReshapeComp(0, sig, sig.r), dvReshapeComp(1, sig, sig.g),
	           dvReshapeComp(2, sig, sig.b));
	mat3 nl = mat3(ubo.dvNl[0].xyz, ubo.dvNl[1].xyz, ubo.dvNl[2].xyz);
	vec3 rgb = nl * (sig - ubo.dvNlOff.xyz);
	vec3 lin = pqToLinear(rgb);
	mat3 lm = mat3(ubo.dvLm[0].xyz, ubo.dvLm[1].xyz, ubo.dvLm[2].xyz);
	return linearToPq(lm * lin);
}

// DV 链输出恒为 PQ BT.2020, 与容器标签无关(P5 无标签 transfer=0)
vec3 processColor(vec3 rgb) {
	if (ubo.hdrMode == 2) {
		return rgb;
	}
	int xfer = (ubo.doviEnable == 1) ? 2 : ubo.transfer;
	if (xfer == 2) {
		vec3 lin = pqToLinear(rgb);
		lin = toneMap(lin);
		lin = bt2020ToBt709(lin);
		return linearToBt709(lin);
	}
	if (xfer == 3) {
		vec3 lin = hlgToLinear(rgb) * 0.1;
		lin = toneMap(lin);
		lin = bt2020ToBt709(lin);
		return linearToBt709(lin);
	}
	return rgb;
}

void main() {
	// Y2Y: 原始 YUV(驱动不做 YUV→RGB), 直接进 DV 整形链
	vec3 yuv = texture(oesYuv, textureCoordinate).rgb;
	fragColor = vec4(clamp(processColor(dvProcess(yuv)), 0.0, 1.0), 1.0);
}
)";

// 字幕画布第二 draw 程序(字幕画布多后端渲染计划 §5.3): GLES 腿无 HDR 呈现面,
// 恒 SDR gamma 域, 语义与 VK canvasBlend.comp 同源。顶点布局与主 program 同构
// (position vec4 + uv vec2, 复用 verts/uvs 指针); premultiplied source-over
// 交固定管线混合(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
static const char* CanvasVertexShaderString = R"(
attribute vec4 position;
attribute vec2 uv;
varying mediump vec2 canvasUv;
void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);
    canvasUv = uv;
}
)";

static const char* CanvasFragmentShaderString = R"(
precision mediump float;
varying mediump vec2 canvasUv;
uniform sampler2D canvasTex;
uniform vec4 uRect;   // center.xy, size.xy(帧归一化)
uniform vec4 uXform;  // origin.xy, invScale, opacity
void main() {
    if (uXform.w <= 0.0) {
        gl_FragColor = vec4(0.0);
        return;
    }
    vec2 rmin = uRect.xy - uRect.zw * 0.5;
    vec2 rmax = uRect.xy + uRect.zw * 0.5;
    if (any(lessThan(canvasUv, rmin)) || any(greaterThanEqual(canvasUv, rmax))) {
        gl_FragColor = vec4(0.0);
        return;
    }
    // 反算采样点; 越界显式置空(clamp-to-edge 会把画布边缘 alpha 拉花)
    vec2 suv = uXform.xy + canvasUv * uXform.z;
    if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) {
        gl_FragColor = vec4(0.0);
        return;
    }
    vec4 o = texture2D(canvasTex, suv);
    // opacity<1 时 rgb 随 alpha 同乘防白漂(与 VK canvasBlend 同规)
    gl_FragColor = vec4(o.rgb * uXform.w, o.a * uXform.w);
}
)";

void regEglRender() {
  RegFunc eglRenderReg = {
      "egl render init", []() {
        VRenderDesc renderDesc = {};
        renderDesc.name = "Egl Render";
        AvoxManager::Get().vRender.regInitFunc(
            RenderType::OpenGLES, renderDesc,
            []() -> VideoRender* { return new EglVideoRender(); });
      }};
  AvoxManager::Get().initFuncs.push_back(eglRenderReg);
}

EglVideoRender::EglVideoRender() {
  renderType = RenderType::OpenGLES;
  canvasRender = std::make_unique<CanvasRender>();
}

EglVideoRender::~EglVideoRender() { closeProgram(); }

// 字幕画布挂口(字幕画布多后端渲染计划 §5.3)
ICanvasLayer* EglVideoRender::enableRenderCanvas() {
  // lane=0 本腿输出是 VK 对接面: 禁挂(双重字幕+字幕被超分), 由 VK canvas 层负责
  if (bVkOutput) {
    return nullptr;
  }
  bCanvasWanted = true;
  return canvasRender.get();
}

void EglVideoRender::disableRenderCanvas() { bCanvasWanted = false; }

// 渲染线程: 挂/摘同步(宿主侧只置 wanted, 层实例归渲染线程)
void EglVideoRender::syncCanvasLayer() {
  if (bCanvasWanted && !canvasLayer) {
    canvasLayer = std::make_unique<GlesCanvasLayer>();
    canvasRender->setCanvasLayer(canvasLayer.get());
  } else if (!bCanvasWanted && canvasLayer) {
    canvasRender->setCanvasLayer(nullptr);
    canvasLayer.reset();
  }
}

// canvas 第二 draw 程序(调用点 context current)
bool EglVideoRender::ensureCanvasProgram() {
  if (glCanvasProgram) {
    return true;
  }
  glCanvasProgram =
      createGLProgram(CanvasVertexShaderString, CanvasFragmentShaderString);
  if (!glCanvasProgram) {
    LOGFLF(LogLevel::warn, "create canvas gl program failed");
    return false;
  }
  canvasPosAttr = glGetAttribLocation(glCanvasProgram, "position");
  canvasUvAttr = glGetAttribLocation(glCanvasProgram, "uv");
  canvasTexAttr = glGetUniformLocation(glCanvasProgram, "canvasTex");
  canvasRectAttr = glGetUniformLocation(glCanvasProgram, "uRect");
  canvasXformAttr = glGetUniformLocation(glCanvasProgram, "uXform");
  return true;
}

void EglVideoRender::onSetSurface() {
  LOGFLF(LogLevel::info, "surface:", surface);
}

bool EglVideoRender::vaildAndInitGraph() {
#ifdef __ANDROID__
  // 统一检查点(§4.2): 呈现面直通实态翻转 → 置 bResetFlag 重建输出端。
  // EglWindow 无 HDR 呈现面(setHdrPassthrough 恒 false), 本步恒不触发;
  // 接线目的是让三平台走同一检查路径(§4.2「检查路径同源」), 避免日后
  // 加 EGL HDR 口时漏挂
  if (checkTargetPassthrough()) {
    bResetFlag = true;
  }
  // 不支持CPU数据输入
  if (cpuIn) {
    return false;
  }
  GLESContext* tempRCtx = static_cast<GLESContext*>(gpuFrame.context);
  // 如果frame.context改变了
  if (tempRCtx != frameRCtx) {
    LOGFLF(LogLevel::info, "gles context changed,new gles context:", tempRCtx,
           " old gles context:", frameRCtx);
    frameRCtx = tempRCtx;
    // 释放旧的
    closeProgram();
  }
  if (frameRCtx && frameCtx != frameRCtx->getContext()) {
    // GLESContext没变，但是里面的EGLContext变了，一样要释放
    LOGFLF(LogLevel::info,
           "egl context changed,new egl context:", frameRCtx->getContext(),
           " old egl context:", frameCtx);
    closeProgram();
  }
  // 最新值是空的
  if (!frameRCtx) {
    log(LogLevel::warn, "initGraph failed, frameContext is null");
    return false;
  }
  // 如果有窗口，输入大小变了不用管
  // 因为这里是VS+PS，自动把大小转成窗口大小或是PBO大小
  // 如果没窗口，输入大小变化后需要重置资源
  // 原子读+清重置标志: 释放决策用捕获值, 只清本次读到的值 —— 读-清分离期间宿主
  // 新置的请求不会被盲写抹掉, 留到下一帧再重建一次(见 VideoRender.hpp 契约)
  const bool bNeedReset = bResetFlag.exchange(false);
  if (!surface && bNeedReset) {
    closeProgram();
  }
#else
  // 非 Android 车道没有基于标志的释放决策, 仍消费一次(契约: 各后端必须消费)
  bResetFlag = false;
#endif
  // 建不建由 glProgram == 0 驱动, 不依赖本标志
  if (glProgram > 0) {
    return true;
  }
  // 如果是EGL窗口，则应该有值，如果Vulkan窗口，则需要有ImageFormat
  if (!surface && (imageFormat.width == 0 || imageFormat.height == 0)) {
    LOGFLF(LogLevel::warn, "surface is null and imageFormat is invalid");
    return false;
  }
#ifdef __ANDROID__
  frameCtx = frameRCtx->getContext();
  if (!frameCtx) {
    log(LogLevel::warn, "initGraph failed, frameCtx is null");
    return false;
  }
  if (surface) {
    aspect = (float)gpuFrame.format.width / (float)gpuFrame.format.height;
    // 如果有窗口，窗口大小就是需要输出的大小，和输入大小无关
    // 需不需要考虑窗口大小变化了，重新生成？
    imageFormat.width = ANativeWindow_getWidth(surface);
    imageFormat.height = ANativeWindow_getHeight(surface);
    LOGFLF(LogLevel::info, "surface width:", imageFormat.width,
           " height:", imageFormat.height, " aspect:", aspect);
  } else {
    LOGFLF(LogLevel::info,
           "surface is null,image format width:", imageFormat.width,
           " height:", imageFormat.height);
  }
#endif
  closeProgram();
  // 渲染使用frameCtx解码上下文做共享
  // 这样可以使用解码后的OES纹理
  // window如果为空,则表明渲染到FBO对应的AHardwareBuffer上
  initContext(frameCtx);
  if (surface) {
    createSurface(surface);
  } else {
    createSurface(imageFormat);
  }
  createProgram();
#ifdef __ANDROID__
  // if (!window) {
  //   sharedBuffer = std::make_unique<SharedGpuBuffer>();
  //   sharedBuffer->createAndroidBuffer(imageFormat);
  //   sharedBuffer->bindGL(textureId);
  //   LOGFLF(LogLevel::info, "create shardbuffer success");
  // }
#endif
  LOGFLF(LogLevel::info, "success");
  return glProgram > 0;
}

void EglVideoRender::releaseGraph() { closeProgram(); }

void EglVideoRender::renderGpuFrame(const GpuFrame& frame) {
#ifdef __ANDROID__
  // 参数true,表明MediaCodec队列的数据压入到OES纹理中,否则直接释放
  frameRCtx->onFrameRelease(true, frame);
  if (frameRCtx && frameRCtx->getImage() > 0) {
    useProgram(frameRCtx->getImage());
  } else {
    LOGFLF(LogLevel::warn, "glesContext is null or image is 0");
    return;
  }
#endif
}

IRenderContext* EglVideoRender::getGpuContext() { return this; }

// 颜色/HDR参数: 每帧渲染时直接读取成员, 无需脏标记
void EglVideoRender::setColorSpace(const ColorSpaceDesc& c) {
  // 探针走 stderr(与 [hdr]/[yuv2rgba] 同口径): 渲染腿 transfer 链的取证点,
  // 锚点/差分直通形态时先看这行是否到位、值是否为流侧语义
  fprintf(stderr, "[egl] setColorSpace std=%d range=%d transfer=%d\n",
          (int)c.standard, (int)c.range, (int)c.transfer);
  // 基类存档, checkShot CPU兜底转换取用(本腿 GPU 腿拒 CPU 输入, Android
  // 截图恒走该兜底; 漏这行则兜底恒按默认 {bt601, full} 转, 见 VideoRender.hpp)
  VideoRender::setColorSpace(c);
  if (c.standard == cs.standard && c.range == cs.range &&
      c.transfer == cs.transfer) {
    return;
  }
  cs = c;
}

void EglVideoRender::setHdrMeta(const HdrMeta& meta) {
  // 探针走 stderr: HDR10/DV 的 peak 链取证点(锚点与差分判据都消费它)
  fprintf(stderr, "[egl] setHdrMeta valid=%d peak=%u l1max=%.1f\n",
          (int)meta.valid, hdrPeakNits(meta), (double)meta.l1MaxNits);
  if (!meta.valid) {
    return;
  }
  hdrMeta = meta;
}

void EglVideoRender::setDoviMeta(const DoviMeta& meta) {
  // 探针走 stderr: DV 整形链取证点(元数据到达 + 有效态)
  fprintf(stderr, "[egl] setDoviMeta valid=%d pivots=%d/%d/%d\n",
          (int)meta.valid, (int)meta.comp[0].numPivots,
          (int)meta.comp[1].numPivots, (int)meta.comp[2].numPivots);
  {
    std::lock_guard<std::mutex> lk(dvMtx);
    doviMeta = meta;
  }
  // 有效态翻转驱动程序选择(Y2Y 变体 ↔ 常规); 场景级更新只需重传 UBO
  bDovValid.store(meta.valid);
  bDvUboDirty.store(true);
}

// DV 变体程序: 首见 DV 帧才建(零 DV 会话零 GL 对象, 与 canvas 程序同策略)。
// 必须在渲染线程且 context current 时调用。
bool EglVideoRender::ensureDvProgram() {
  if (glDvProgram > 0) {
    return true;
  }
  if (bDvUnsupported) {
    return false;
  }
  glDvProgram = createGLProgram(surface ? DvVertexShaderString
                                        : DvVertexShaderString1,
                                DvFragmentShaderString);
  if (glDvProgram == 0) {
    // 无 GL_EXT_YUV_target(或 Y2Y 采样不被支持): 回落常规 OES 程序, 不反复重试
    bDvUnsupported = true;
    LOGFLF(LogLevel::warn,
           "[egl] DV(Y2Y) program failed, fallback to OES path (no render shape)");
    return false;
  }
  dvPosAttr = glGetAttribLocation(glDvProgram, "position");
  dvUvAttr = glGetAttribLocation(glDvProgram, "uv");
  dvTexAttr = glGetUniformLocation(glDvProgram, "oesYuv");
  dvUboBlock = glGetUniformBlockIndex(glDvProgram, "DvUbo");
  glUniformBlockBinding(glDvProgram, dvUboBlock, 0);
  glGenBuffers(1, &dvUboBuf);
  glBindBuffer(GL_UNIFORM_BUFFER, dvUboBuf);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(ColorYuvUBO), nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, dvUboBuf);
  LOGFLF(LogLevel::info, "[egl] DV(Y2Y) program ready, uboBlock:",
         (int32_t)dvUboBlock, " size:", (int32_t)sizeof(ColorYuvUBO));
  return true;
}

// DV UBO 上传(场景级): 与 VK/DX11 共用 packDoviUbo, 故三腿同源
void EglVideoRender::uploadDvUbo() {
  DoviMeta meta;
  {
    std::lock_guard<std::mutex> lk(dvMtx);
    meta = doviMeta;
  }
  // 2848B: 场景级(非每帧), 栈上构造无妨
  ColorYuvUBO uboData = {};
  uboData.transfer = (int32_t)cs.transfer;
  uboData.hdrMode = (int32_t)hdrMode;
  uboData.maxLuminance = (float)hdrPeakNits(hdrMeta);
  uboData.sdrWhiteNits = 100.0f;
  packDoviUbo(uboData, meta);
  glBindBuffer(GL_UNIFORM_BUFFER, dvUboBuf);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(ColorYuvUBO), &uboData,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, dvUboBuf);
  LOGFLF(LogLevel::info, "[egl] DV ubo upload doviEnable:", uboData.doviEnable,
         " peak:", uboData.maxLuminance, " hdrMode:", uboData.hdrMode);
}

void EglVideoRender::setHdrMode(HdrMode mode) { hdrMode = mode; }

void EglVideoRender::createProgram() {
#ifndef WIN32
  if (glProgram == 0) {
    const char* vertexString =
        surface ? VertexShaderString : VertexShaderString1;
    glProgram = createGLProgram(vertexString, FragmentShaderString);
  }
  if (glProgram == 0) {
    AVOX_GL_LOG("create gl program failed");
    return;
  }
  // 创建RGBA纹理
  glGenTextures(1, &textureId);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageFormat.width, imageFormat.height,
               0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  // 生成FBO
  glGenFramebuffers(1, &fboId);
  glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textureId, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  posAttr = glGetAttribLocation(glProgram, "position");
  uvAttr = glGetAttribLocation(glProgram, "uv");
  extAttr = glGetUniformLocation(glProgram, "oes_texture");
  hdrModeAttr = glGetUniformLocation(glProgram, "uHdrMode");
  transferAttr = glGetUniformLocation(glProgram, "uTransfer");
  peakNitsAttr = glGetUniformLocation(glProgram, "uPeakNits");
  sdrWhiteAttr = glGetUniformLocation(glProgram, "uSdrWhite");
  LOGFLF(LogLevel::info, "create program success,textureId:", textureId,
         " width:", imageFormat.width, " height:", imageFormat.height);
  // 禁用垂直同步以减少交换缓冲区延迟
  eglSwapInterval(display, 0);
#endif
}

void EglVideoRender::useProgram(uint32_t oesId) {
  // 没有初始化
  if (getContext() == EGL_NO_CONTEXT) {
    return;
  }
  if (glProgram == 0) {
    return;
  }
  if (!eglsurface) {
    return;
  }
  int32_t width = imageFormat.width;
  int32_t height = imageFormat.height;
#if __ANDROID__
  // 检查Surface是否有效,如果相关的EGLSurface无效，则不渲染
  if (!eglQuerySurface(display, eglsurface, EGL_WIDTH, &width) || width <= 0) {
    return;
  }
  if (!eglQuerySurface(display, eglsurface, EGL_HEIGHT, &height) ||
      height <= 0) {
    return;
  }
#endif
#ifndef WIN32
  // 如果没有设置窗口，则渲染到FBO上
  if (!surface) {
    glBindFramebuffer(GL_FRAMEBUFFER, fboId);
    glViewport(0, 0, width, height);
  } else {
    // 如果当前线程有多surface,切换到当前线程的surface
    makeCurrent();
    vec4i viewRect = {0, 0, width, height};
    if (!bFullScreen && aspect > 0.0f) {
      // 视频的aspect
      viewRect = getViewRect(width, height, aspect);
    }
    glViewport(viewRect.x, viewRect.y, viewRect.z, viewRect.w);
  }
  // glClearColor(1, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // 激活纹理
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesId);
  // DV 流走 Y2Y 变体(原始 YUV 采样 + shader 内 DV 整形链, 与 VK/DX11/Metal 同源);
  // 无 EXT_YUV_target 的设备 ensureDvProgram 失败 → 回落常规 OES 程序(旧行为)
  uint32_t videoProgram = glProgram;
  int32_t vPosAttr = posAttr;
  int32_t vUvAttr = uvAttr;
  const bool bDvDraw = bDovValid.load() && ensureDvProgram();
  if (bDvDraw) {
    videoProgram = glDvProgram;
    vPosAttr = dvPosAttr;
    vUvAttr = dvUvAttr;
  }
  // 设置可编程管线参数
  glUseProgram(videoProgram);
  if (bDvDraw) {
    if (bDvUboDirty.exchange(false)) {
      uploadDvUbo();
    }
    glUniform1i(dvTexAttr, 0);
  } else {
    glUniform1i(extAttr, 0);
    // 颜色/HDR参数每帧下发(免脏标记); SDR内容 uTransfer=gamma 走直通, 行为零变化
    glUniform1i(hdrModeAttr, (int)hdrMode);
    glUniform1i(transferAttr, (int)cs.transfer);
    glUniform1f(peakNitsAttr, (float)hdrPeakNits(hdrMeta));
    glUniform1f(sdrWhiteAttr, 100.0f);
  }
  glEnableVertexAttribArray(vPosAttr);
  glVertexAttribPointer(vPosAttr, 2, GL_FLOAT, false, 0, (void*)(verts));
  glEnableVertexAttribArray(vUvAttr);
  glVertexAttribPointer(vUvAttr, 2, GL_FLOAT, false, 0, (void*)(uvs));
  // 渲染
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  // 字幕画布第二 draw(lane=1 呈现腿, 字幕画布多后端渲染计划 §5.3):
  // GLES 无 HDR 呈现面恒 SDR gamma 域; 无内容整跳(零字幕零影响)。
  // 同 viewport(视频矩形, canvas 归一化坐标与帧归一化一致)
  syncCanvasLayer();
  if (canvasLayer &&
      canvasLayer->ensureTexture(imageFormat.width, imageFormat.height)) {
    canvasLayer->uploadIfNeeded();
    if (canvasLayer->visible() && ensureCanvasProgram()) {
      const CanvasBlendParamet p = canvasLayer->computeParamet();
      glUseProgram(glCanvasProgram);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, canvasLayer->texture());
      glUniform1i(canvasTexAttr, 0);
      glUniform4f(canvasRectAttr, p.centerX, p.centerY, p.width, p.height);
      glUniform4f(canvasXformAttr, p.originX, p.originY, p.invScale,
                  p.opacity);
      glEnableVertexAttribArray(canvasPosAttr);
      glVertexAttribPointer(canvasPosAttr, 2, GL_FLOAT, false, 0,
                            (void*)(verts));
      glEnableVertexAttribArray(canvasUvAttr);
      glVertexAttribPointer(canvasUvAttr, 2, GL_FLOAT, false, 0, (void*)(uvs));
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glDisable(GL_BLEND);
      glDisableVertexAttribArray(canvasPosAttr);
      glDisableVertexAttribArray(canvasUvAttr);
      glBindTexture(GL_TEXTURE_2D, 0);
      glUseProgram(videoProgram);
    }
  }
  //
  glDisableVertexAttribArray(vPosAttr);
  glDisableVertexAttribArray(vUvAttr);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  // 如果没有设置窗口，则渲染到FBO上
  if (!surface) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
#endif
#ifdef __ANDROID__
  if (surface) {
    // 则交换缓冲区,有类似glFinish同步的作用
    eglSwapBuffers(display, eglsurface);
    unMakeCurrent();
  }
  // 读取纹理数据到EGLImage
  // if (!window && sharedBuffer) {
  //   // 此时EGLImage应该有数据？检查HarderBuffer是否有数据了
  //   sharedBuffer->logData();
  // }
#endif
}

void EglVideoRender::closeProgram() {
#ifndef WIN32
  // canvas GL 对象随 context 释放; 层解绑, contentStale 走宿主重传
  if (canvasLayer) {
    canvasLayer->releaseGL();
    canvasRender->setCanvasLayer(nullptr);
    canvasLayer.reset();
  }
  if (glCanvasProgram) {
    glDeleteProgram(glCanvasProgram);
    glCanvasProgram = 0;
  }
  if (glProgram) {
    glDeleteProgram(glProgram);
    glProgram = 0;
  }
  // DV 变体程序与 UBO 随 context 释放; bDvUnsupported 复位(新 context 重新判定)
  if (glDvProgram) {
    glDeleteProgram(glDvProgram);
    glDvProgram = 0;
  }
  if (dvUboBuf) {
    glDeleteBuffers(1, &dvUboBuf);
    dvUboBuf = 0;
  }
  bDvUnsupported = false;
  bDvUboDirty.store(true);
  if (textureId) {
    glDeleteTextures(1, &textureId);
    textureId = 0;
  }
  if (fboId) {
    glDeleteFramebuffers(1, &fboId);
    fboId = 0;
  }
  if (display != EGL_NO_DISPLAY || eglsurface != EGL_NO_SURFACE) {
    unInit();
  }
#endif
}

bool EglVideoRender::fetchFrame(ImageBuffer* imageBuffer) {
  // §3.4 CPU 读回防护: 与 Dx11CSVideoRender/MetalRender 同口径 —— 直通态输出
  // 非 SDR 码, 回读是脏图。EGL 腿无 HDR 呈现面, 此闸恒不触发; 接线目的是
  // 统一三腿义务, 防日后补 EGL HDR 口时漏挂
  if (bTargetPassthrough) {
    LOGFLF(LogLevel::warn, "fetchFrame refused in hdr passthrough");
    return false;
  }
  ImageFormat format = imageFormat;
  format.imageType = ImageType::rgba8;
  if (format.width == 0 || format.height == 0) {
    LOGFLF(LogLevel::warn, "imageFormat is invalid");
    return false;
  }
#ifndef WIN32
  // 取证探针: 函数入口即打印(早退路径也要可见), 判定抓图分流与状态
  fprintf(stderr,
          "[egl] fetch: win=%d program=%u fbo=%u oesImg=%u transfer=%d "
          "hdrMode=%d attrT=%d attrH=%d\n",
          (int)(surface != nullptr), glProgram, fboId,
          (frameRCtx ? frameRCtx->getImage() : 0u), (int)cs.transfer,
          (int)hdrMode, transferAttr, hdrModeAttr);
  // 窗口模式进本函数时 useProgram 已 swap 且把 draw surface 切去了 preSurface,
  // back buffer 内容随交换不再可靠 —— 自持 makeCurrent 用当前 program 往常驻
  // FBO 重画一遍再读, 与显示链解耦(对齐 Dx11CSVideoRender::fetchFrame 读
  // outTexture 的独立取证模式); OES 纹理在 onFrameRelease(true) 后本帧内有效
  const bool bWindow = (surface != nullptr);
  if (bWindow) {
    if (!makeCurrent()) {
      LOGFLF(LogLevel::warn, "fetchFrame makeCurrent failed");
      return false;
    }
  }
  // 保存当前的FBO绑定状态
  GLint prevFBO = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
  // 统一读 FBO: 窗口模式重画后读, 非窗口模式(与 Vulkan 交互)画的本来就是它
  glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  // 检查帧缓冲区完整性
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    LOGFLF(LogLevel::warn, "Framebuffer is not complete:", status);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);  // 恢复之前的FBO
    if (bWindow) {
      unMakeCurrent();
    }
    return false;
  }
  if (bWindow && glProgram > 0 && frameRCtx && frameRCtx->getImage() > 0) {
    // 复刻 useProgram 的画法(同 program/同几何/同 HDR uniform), 仅目标换成
    // FBO: 取证图与显示腿同帧同参, letterbox 视口也按显示语义复刻
    vec4i viewRect = {0, 0, imageFormat.width, imageFormat.height};
    if (!bFullScreen && aspect > 0.0f) {
      viewRect = getViewRect(imageFormat.width, imageFormat.height, aspect);
    }
    glViewport(viewRect.x, viewRect.y, viewRect.z, viewRect.w);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, frameRCtx->getImage());
    glUseProgram(glProgram);
    glUniform1i(extAttr, 0);
    glUniform1i(hdrModeAttr, (int)hdrMode);
    glUniform1i(transferAttr, (int)cs.transfer);
    glUniform1f(peakNitsAttr, (float)hdrPeakNits(hdrMeta));
    glUniform1f(sdrWhiteAttr, 100.0f);
    glEnableVertexAttribArray(posAttr);
    glVertexAttribPointer(posAttr, 2, GL_FLOAT, false, 0, (void*)(verts));
    glEnableVertexAttribArray(uvAttr);
    glVertexAttribPointer(uvAttr, 2, GL_FLOAT, false, 0, (void*)(uvs));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(posAttr);
    glDisableVertexAttribArray(uvAttr);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  }
  imageBuffer->setImageFormat(format);
  // 从当前绑定的帧缓冲区读取数据
  glReadPixels(0, 0, format.width, format.height, GL_RGBA,
               GL_UNSIGNED_BYTE, imageBuffer->getPointer());
  // PNG的Y与opengl是反的,需要上下翻转,rgba
  int rowSize = format.width * 4;
  uint8_t* data = (uint8_t*)imageBuffer->getPointer();
  std::vector<uint8_t> tempRow(rowSize);
  for (int i = 0; i < format.height / 2; ++i) {
    uint8_t* rowTop = data + i * rowSize;
    uint8_t* rowBottom = data + (format.height - 1 - i) * rowSize;
    // 交换
    memcpy(tempRow.data(), rowTop, rowSize);
    memcpy(rowTop, rowBottom, rowSize);
    memcpy(rowBottom, tempRow.data(), rowSize);
  }
  // 检查OpenGL错误
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    LOGFLF(LogLevel::warn, "glReadPixels failed with error:", error);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);  // 恢复之前的FBO
    if (bWindow) {
      unMakeCurrent();
    }
    return false;
  }
  // 恢复之前的FBO绑定状态
  glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
  if (bWindow) {
    // 与进函数时对称(useProgram 每帧画完也 unMakeCurrent): 下一帧
    // useProgram 的 surface 分支自己会 makeCurrent 重建, 无残留状态
    unMakeCurrent();
  }
#endif
  return true;
}

}

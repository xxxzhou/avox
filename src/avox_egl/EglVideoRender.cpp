#include "EglVideoRender.hpp"

#include "GLES3/gl3.h"
#include "GLESContext.hpp"
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

EglVideoRender::EglVideoRender() { renderType = RenderType::OpenGLES; }

EglVideoRender::~EglVideoRender() { closeProgram(); }

void EglVideoRender::onSetSurface() {
  LOGFLF(LogLevel::info, "surface:", surface);
}

bool EglVideoRender::vaildAndInitGraph() {
#ifdef __ANDROID__
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
  if (c.standard == cs.standard && c.range == cs.range &&
      c.transfer == cs.transfer) {
    return;
  }
  cs = c;
}

void EglVideoRender::setHdrMeta(const HdrMeta& meta) {
  if (!meta.valid) {
    return;
  }
  hdrMeta = meta;
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
  // 设置可编程管线参数
  glUseProgram(glProgram);
  glUniform1i(extAttr, 0);
  // 颜色/HDR参数每帧下发(免脏标记); SDR内容 uTransfer=gamma 走直通, 行为零变化
  glUniform1i(hdrModeAttr, (int)hdrMode);
  glUniform1i(transferAttr, (int)cs.transfer);
  glUniform1f(peakNitsAttr, (float)hdrPeakNits(hdrMeta));
  glUniform1f(sdrWhiteAttr, 100.0f);
  glEnableVertexAttribArray(posAttr);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, false, 0, (void*)(verts));
  glEnableVertexAttribArray(uvAttr);
  glVertexAttribPointer(uvAttr, 2, GL_FLOAT, false, 0, (void*)(uvs));
  // 渲染
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  //
  glDisableVertexAttribArray(posAttr);
  glDisableVertexAttribArray(uvAttr);
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
  if (glProgram) {
    glDeleteProgram(glProgram);
    glProgram = 0;
  }
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
  ImageFormat format = imageFormat;
  format.imageType = ImageType::rgba8;
  if (format.width == 0 || format.height == 0) {
    LOGFLF(LogLevel::warn, "imageFormat is invalid");
    return false;
  }
  // imageBuffer->setImageFormat(format);
#ifndef WIN32
  // 保存当前的FBO绑定状态
  GLint prevFBO = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
  if (surface) {
    // 渲染到 Native Window：读取默认帧缓冲区
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  } else {
    // 渲染到 FBO：读取 FBO 的颜色附件
    glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  }
  // 检查帧缓冲区完整性
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    LOGFLF(LogLevel::warn, "Framebuffer is not complete:", status);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);  // 恢复之前的FBO
    return false;
  }
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  format.width = viewport[2];
  format.height = viewport[3];
  imageBuffer->setImageFormat(format);
  // 从当前绑定的帧缓冲区读取数据
  glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA,
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
    return false;
  }
  // 恢复之前的FBO绑定状态
  glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
#endif
  return true;
}

}

#include "EglVideoYuv.hpp"

namespace avox {

// 简单的纹理复制顶点着色器
static const char* vertexShaderSource = R"(
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

// 简单的纹理复制片段着色器
static const char* fragmentShaderSource = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
varying mediump vec2 textureCoordinate;
uniform samplerExternalOES oes_texture;
void main() {
    gl_FragColor = texture2D(oes_texture, textureCoordinate);
}
)";
// harderBuffer 纹理复制片段着色器
static const char* bufferFragmentShaderSource = R"(
precision mediump float;
varying mediump vec2 textureCoordinate;
uniform sampler2D texture2d;
void main() {
    gl_FragColor = texture2D(texture2d, textureCoordinate);
}
)";

static const GLfloat verts[] = {
    -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
};
static const GLfloat uvs[] = {
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
};

EglVideoYuv::EglVideoYuv() {
#ifdef __ANDROID__
  eglPresentationTimeANDROID =
      (PFNEGLPRESENTATIONTIMEANDROIDPROC)eglGetProcAddress(
          "eglPresentationTimeANDROID");
#endif
}

EglVideoYuv::~EglVideoYuv() { releaseGraph(); }

bool EglVideoYuv::vaildAndInitGraph(const GpuFrame& frame) {
  if (glProgram) {
    // 检查context是否已经变了
    GLESContext* newContext = static_cast<GLESContext*>(frame.context);
    if (newContext == inContext) {
      return true;
    }
    LOGFLF(LogLevel::info, "frame context change");
  }
  inContext = static_cast<GLESContext*>(frame.context);
#ifdef __ANDROID__
  hardwareBuffer = (SharedGpuBuffer*)(frame.buffer);
#endif
  LOGFLF(LogLevel::info, "inContext:", inContext);
  if (!inContext) {
    LOGFLF(LogLevel::warn, "eglContext is null");
    return false;
  }
  // 如果输入的EGL上下文是本身,则直接用
  // 否则用传入的EGL上下文作为共享上下文
  // 不管如何,当前EGL环境都可以访问传入的GPU数据
  initContext(inContext->getContext());
  eglSize.width = frame.format.width;
  eglSize.height = frame.format.height;
#ifdef __ANDROID__
  // surface是android原生编码的输入,需要把数据输出到对应的surface
  createSurface(surface);
#endif
  createProgram();
  return true;
}

void EglVideoYuv::releaseGraph() {
  closeProgram();
  unInit();
}

void EglVideoYuv::renderGpuFrame(const GpuFrame& frame) {
  if (!glProgram) {
    return;
  }
  uint32_t inTexId = inContext->getImage();
  if (inTexId == 0) {
    return;
  }
  //  LOGFLF(LogLevel::info, "inTexId:", inTexId, " egl surface:", eglsurface,
  //       " egl size:", eglSize.width, "x", eglSize.height,"
  //       buffer:",frame.buffer);
  // 指定当前编码的Surface
  makeCurrent();
#ifdef __ANDROID__
  // 设置Surface时间戳 单位微秒,编码器需要时间戳
  eglPresentationTimeANDROID(display, eglsurface, frame.pts * 1000);
  glViewport(0, 0, eglSize.width, eglSize.height);
  glClear(GL_COLOR_BUFFER_BIT);
  // 激活纹理
  glActiveTexture(GL_TEXTURE0);
  // 如果是VkImage映射的纹理,则绑定对应textureId
  if (hardwareBuffer) {
    hardwareBuffer->renderGL(textureId, GL_TEXTURE_2D);
  } else {
    // 如果是OES纹理
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, inTexId);
  }
  // 设置可编程管线参数
  glUseProgram(glProgram);
  glUniform1i(extAttr, 0);
  glEnableVertexAttribArray(posAttr);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, false, 0, (void*)(verts));
  glEnableVertexAttribArray(uvAttr);
  glVertexAttribPointer(uvAttr, 2, GL_FLOAT, false, 0, (void*)(uvs));
  // 渲染
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(posAttr);
  glDisableVertexAttribArray(uvAttr);
  if (hardwareBuffer) {
    glBindTexture(GL_TEXTURE_2D, 0);
  } else {
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  }
  // 则交换缓冲区
  eglSwapBuffers(display, eglsurface);
  unMakeCurrent();
#endif
}

void EglVideoYuv::createProgram() {
  if (glProgram == 0) {
    std::string fragmentStr = fragmentShaderSource;
#ifdef __ANDROID__
    if (hardwareBuffer) {
      fragmentStr = bufferFragmentShaderSource;
    }
#endif
    glProgram = createGLProgram(vertexShaderSource, fragmentStr.c_str());
  }
  if (glProgram == 0) {
    AVOX_GL_LOG("create gl program failed");
    return;
  }
  posAttr = glGetAttribLocation(glProgram, "position");
  uvAttr = glGetAttribLocation(glProgram, "uv");
  extAttr = glGetUniformLocation(glProgram, "oes_texture");
#ifdef __ANDROID__
  // hardwareBuffer对应vulkan处理后的GPU的RGBA数据
  // 绑定一个相应的纹理上,通过hardwareBuffer,映射vkimage->gltexture
  if (hardwareBuffer) {
    extAttr = glGetUniformLocation(glProgram, "texture2d");
    // 创建RGBA纹理
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, eglSize.width, eglSize.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    hardwareBuffer->bindGL(textureId, GL_TEXTURE_2D);
  }
#endif
}

void EglVideoYuv::closeProgram() {
  if (glProgram != 0) {
    glDeleteProgram(glProgram);
    glProgram = 0;
  }
#ifdef __ANDROID__
  if (textureId != 0) {
    glDeleteTextures(1, &textureId);
    textureId = 0;
  }
  if (hardwareBuffer) {
  }
#endif
}

}
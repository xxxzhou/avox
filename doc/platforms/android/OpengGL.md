# OpenGL

## 文档

[OpenGL ES 共享上下文实现多线程渲染](https://cloud.tencent.com/developer/article/1831382)

[OpenGL ES EGL 简介](https://www.codersrc.com/archives/17486.html)

[使用 HardwareBuffer 实现 Android 多进程渲染](https://zhuanlan.zhihu.com/p/576373572)

## 开发

### MediaCodec解码与渲染

现在MediaCodec的硬解直接渲染到Surface限制大，先看流程。

1. 拿到Surface对应的ANativeWindow,然后启动渲染线程，初始化EGL上下文，GL相应资源。
2. 解码线程初始化MediaCodec，绑定渲染EGL上下文做共享。
3. 解码线程解码后，拿到解码后帧数据,压入队列，主要是outQueueIndex.
4. 渲染线程从队列中取数据,与音频同步，确定帧数据是放弃渲染还是渲染，对应AMediaCodec_releaseOutputBuffer最后参数true/false.
5. 渲染线程在上面先得到OES纹理，然后把OES纹理渲染到ANativeWindow

android窗口使用opengl渲染，需要EGL上下文，EGL上下文初始化时需要绑定ANativeWindow,这个切后台就会关闭，相应的EGL上下文就不能用了，并且因为EGL初始化与opengl渲染需要在一个线程中。这样就需要在窗口的主线程初始化然后Run.

而MediaCodec解码是在一个线程中，所以MediaCodec需要一个单独的线程，为了能在EGL窗口使用，必需初始化MediaCodec时绑定渲染EGL上下文做共享，这样MediaCodec解码后的数据就可以在EGL窗口渲染了。

但是现在问题是，对应的窗口关闭后，MediaCodec解码线程还在运行，MediaCodec解码后的数据就会崩溃，因为窗口EGL上下文已经无效。

现在来看，只有在窗口关闭时，先的播放器关闭，然后窗口创建时，重新开始播放器流程。

## 问题记录

1. gl相关函数调用都没反应，如glGetTextureImage返回0，相应的glGetError也返回0.

相应的EGL上下文还没建立，需要先建立EGL上下文，包含GLSurface,无渲染窗口eglCreatePbufferSurface，有窗口使用eglCreateWindowSurface,在调用eglMakeCurrent后才可在相应线程使用gl相关函数。

2. bindTextureImage: clearing GL error: 0x502

确保EGL上下文建立，相应GL资源能正确生成，渲染线程调用glFinish，eglSwapBuffers完成流程。

3. MediaCodec硬解时，绑定SurfaceTexture到GLTexture时，会绑定一个EGL上下文，后面SurfaceTexture在别的线程调用updateTexImage时会崩溃。

绑定一个EGL上下文时，选择updateTexImage线程里EGL作为共享上下文，这样SurfaceTexture就能在共享的EGL上下文里调用updateTexImage了。

4. MediaCodec直接输出到GL纹理画面不正常，有混合，多乱点。

帧缓冲区比较大，导致GPU显存队列大，从100改为10大致正常。也有可能是PTS包不对。



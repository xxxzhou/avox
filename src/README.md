# AVOX

## 注意

带crate开关的API，是你申请的内存，需要你自己释放。带get的API，不需要你管理对象。

推送: git push gitlab_remote

## avox_zlmediakit

主要是针对MediaKit封装,其ZLTookit模块封装TCP连接,其子模块media-server包含多文件解析。

有良好的C++封装，使用简单，扩展方便。后期私有协议的解析可以在MediaKit的基础上扩展。

移植到android平台，使用C++引用的方式，其头文件直接引用有会很多重定义，还是改C的方式引用。后面可以用zlmediakit的C++调用方式，但是需要在C++文件里调用，其头文件不要直接引用zlmediakit的C++头文件。

## avox_ffmpeg

主要是针对ffmpeg封装,包含网络协议/多媒体解封装以及市面大部分的编解码实现。

注意这里把ffmepg解协议与解码分开，简单来说，可以只用ffmepg解协议，也可以只用ffmpeg解码。
对应私有协议以及集成原生平台的解码实现。

## avox_vulkan

主要是针对vulkan封装,包含Vulkan下各种GPU图像处理.

不同平台下的窗口实现，如windows的dx11/dx12,android的opengl,ios的metal。

对接不同平台的渲染，在使用vulkan处理完图像后，如何对接不同平台的渲染。

## avox_windows

windows下的GPU资源封装，如dx11/dx12封装。
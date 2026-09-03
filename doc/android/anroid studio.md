# Android Studio

## Mediacodec 硬解码

1. 得到SurfaceTexture 里的纹理，Surface。GLWindow需要包含Surface,GLContext.

2. C++

## 快捷键

- `Ctrl + Alt + L` 格式化代码
- `Ctrl + Alt + O` 优化导入的类和包
- `Ctrl + Alt + T` 生成代码块
- `Ctrl + Alt + I` 自动缩进

## 日志

// 清除日志
adb logcat -c 

adb logcat > D://log.txt

## adb重启服务

adb kill-server && adb start-server

## 查看类链接

NDK=C:/Users/admin/AppData/Local/Android/Sdk/ndk/21.0.6113669

// 使用 Android NDK 中的 readelf（Windows 路径）
$NDK/toolchains/aarch64-linux-android-4.9/prebuilt/windows-x86_64/bin/aarch64-linux-android-readelf -d your_library.so

// 或直接使用完整路径
C:/Users/admin/AppData/Local/Android/Sdk/ndk/25.0.8775105/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe -d your_library.so

# 查看所有动态符号
llvm-readelf --dyn-syms your_library.so

// android avox so 路径
D:\Work\github\avplay\platform\android\AvoxJava\avox\build\intermediates\library_jni\debug\jni\arm64-v8a

D:\Work\github\avplay\platform\android\AvoxJava\avox\build\intermediates\library_jni\debug\jni\arm64-v8a>C:/Users/admin/AppData/Local/Android/Sdk/ndk/25.0.8775105/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe -d libavox.so

## 查看android手机正在运行的进程

C:\Users\admin>adb shell ps | findstr avox
u0_a72       28274  1869   15853584 191384 0                   0 S avox.samples.mediaplayer
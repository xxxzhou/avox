# 编译FFmpeg

## windows

[在Windows上编译FFmpeg库](https://zhuanlan.zhihu.com/p/16550406805)

[NDK编译ffmpeg包含硬件加速vulkan和mediacodec](https://blog.csdn.net/flyfish1986/article/details/131555008)

[windows系统下编译FFMPEG并开启硬件加速（INTEL，NVIDIA，AMD）](https://blog.csdn.net/selivert/article/details/126370178)

[fmpeg-gpu-compile-guide](https://github.com/zshnb/ffmpeg-gpu-compile-guide)

1. 检查是否安装了MSYS2
安装msys2 把 C:\msys64 加入环境变量
2. cmd启动msys2
msys2
3. msys2中执行pacman命令
// ffmpeg的vulkan解码vulkan-headers/vulkan-loader，qsa需要intel-media-sdk 
// pacman默认已经有dx11的环境
pacman -Syu
pacman -S base-devel
pacman -S yasm nasm
pacman -S mingw-w64-x86_64-toolchain
pacman -S mingw-w64-x86_64-pkg-config
pacman -S mingw-w64-x86_64-zlib
pacman -S mingw-w64-x86_64-vulkan-headers mingw-w64-x86_64-vulkan-loader
pacman -S mingw-w64-x86_64-intel-media-sdk
4. cmd启动mingw64，把 C:\msys64\mingw64\bin 加入环境变量
mingw64
5. 切换到ffmpeg 目录
cd D:/Work/github/avplay/3rdparty/FFmpeg
6. 配置编译选项 用来生成 Makefile

> ⚠️ **发行合规**: `--enable-nonfree` 产物任何渠道都不可分发; `--enable-gpl` 产物只能进 AGPL 渠道。
> 请用 `script/ffmpeg/build_ffmpeg.py` 按渠道出包: `--flavor gpl`(AGPL 渠道, libx264/libx265, 无 nonfree) / `--flavor lgpl`(商业渠道), 或直接用 BtbN LGPL 预编译, 交付前 `--verify` 扫一遍。
> 商业渠道构建 avox 时配 `python build_windows.py --flavor=commercial` (CMake AVOX_DIST_FLAVOR, 软编注入 h264_mf/hevc_mf)。

./configure --prefix=../../build/windows/ffmpeg --enable-shared --disable-static --enable-version3 --enable-ffmpeg --enable-hwaccels --enable-gpl --enable-nonfree --enable-vulkan --enable-dxva2 --enable-d3d12va --enable-d3d11va 
// 带调试信息
./configure --enable-shared --disable-static --enable-ffmpeg --enable-hwaccels --enable-gpl --enable-nonfree --enable-gpl --extra-cflags="-g -ggdb -O0" --extra-ldflags="-g -ggdb"
7. 开始编译 Makefile 4线程
make -j4
8. 编译完成
make install -j4
9. 清理
make clean

注意：可能没有安装到D:/Work/github/avplay/build/windows/ffmpeg目录下，需要手动到C:\msys64\usr\local\bin拷贝。

## 查看当前ffmpeg支持的硬件解码

cd D:\Work\github\avplay\build\windows\ffmpeg\bin

ffmpeg -hwaccels

ffmpeg -decoders >> decoders.txt

// 列出所有H264相关解码器
ffmpeg -hide_banner -decoders | findstr h264

ffmpeg -hwaccel d3d11va -i D:/Back/美好.mp4 -c:v h264_qsv output1.mp4

## android

[交叉编译](../android/交叉编译.md)